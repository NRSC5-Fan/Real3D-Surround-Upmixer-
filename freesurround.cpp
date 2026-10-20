/*
Copyright (C) 2007-2010 Christian Kothe

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "stdafx.h"
#include "resource1.h"
#ifndef BOOST_BIND_GLOBAL_PLACEHOLDERS
#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#endif
#include "../SDK/foobar2000.h"
#include "stream_chunker.h"
#include "freesurround_decoder.h"
#include <boost/bind.hpp>
#include <boost/assign.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/version.hpp>
#include <boost/format.hpp>
#include <functional>
#include <sstream>
#include <string>
#include <streambuf>
#include <numeric>
#include <algorithm>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#include <vector>

DECLARE_COMPONENT_VERSION("Real3D","1.3","Real3D DSP\n\nmodified and enhanced by NRSC5-Fan\n\nOriginally developed by pro_optimizer");

// {F856C6D5-1888-4de3-8761-0A4CD132A33A}
// persisted UI preference for the dialog theme: 0 = follow the Windows app theme (default), 1 = light, 2 = dark
static const GUID guid_cfg_theme = { 0x2e91b6c4, 0x58a3, 0x4f07, { 0xb1, 0x6d, 0x0a, 0x93, 0x4e, 0x7c, 0x25, 0xf8 } };
static cfg_int cfg_theme_pref(guid_cfg_theme, 0);

// whether Windows is set to dark mode for apps
static bool system_uses_dark() {
	DWORD v = 1, sz = sizeof(v);
	if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS) return false;
	return v == 0;
}

static const GUID fs_guid = { 0xf856c6d5, 0x1888, 0x4de3, { 0x87, 0x61, 0xa, 0x4c, 0xd1, 0x32, 0xa3, 0x3a } };



// selectable FFT block sizes (samples per channel). 4096 is the original behaviour and stays the default.
static const unsigned g_block_sizes[] = { 1024, 2048, 4096, 8192, 16384 };
static const int g_num_block_sizes = (int)(sizeof(g_block_sizes)/sizeof(g_block_sizes[0]));
static const unsigned g_default_blocksize = 4096;
// index of a block size in the list above (falls back to the default's index)
static inline int block_index(unsigned b) { for (int pass=0; pass<2; pass++) for (int i=0; i<g_num_block_sizes; i++) if (g_block_sizes[i] == (pass ? g_default_blocksize : b)) return i; return 0; }

// preset format version: 0 = original fields, 1 = + center_spread, blocksize, 2 = + LFE cutoff/level/copy/method/slope
// (declared before the class so that the in-class (de)serialization code sees it)
struct freesurround_params;
BOOST_CLASS_VERSION(freesurround_params, 2)

// read-only stream buffer over an existing memory block (no copy, no dependence on std::strstream quirks)
struct membuf : std::streambuf {
	membuf(const char *p, size_t n) { char *b = const_cast<char*>(p); setg(b, b, b + n); }
};

// holds the user-configurable parameters of the FreeSurround DSP
struct freesurround_params {
	// the user-configurable parameters
	float center_image, shift, depth, circular_wrap, focus, front_sep, rear_sep, bass_lo, bass_hi;
	bool use_lfe;
	float center_spread;			// 0 = off; how much of the front center channel is spread into front left/right
	unsigned blocksize;				// FFT block size in samples per channel (4096 = original behaviour)
	float lfe_cutoff, lfe_level;	// LFE low-pass cutoff in Hz (0..200) and LFE/copy gain (1 = unity)
	bool lfe_copy_lr, lfe_copy_c;	// also copy the split bass to front left/right, front center
	int lfe_mode, lfe_slope;		// 0 = time-domain crossover, 1 = soft FFT fade; slope 0/1/2 = 12/24/48 dB/oct
	channel_setup channels_fs;		// FreeSurround channel setup	
	std::vector<unsigned> chanmap;	// FreeSurround -> fb2k channel index translation (derived data for faster lookup)

	// construct with defaults
	freesurround_params(): center_image(0.7), shift(0), depth(1), circular_wrap(90), focus(0), front_sep(1), rear_sep(1), 
		bass_lo(40), bass_hi(90), use_lfe(false), center_spread(0), blocksize(g_default_blocksize),
		lfe_cutoff(90), lfe_level(1), lfe_copy_lr(false), lfe_copy_c(false), lfe_mode(0), lfe_slope(1) { set_channels_fs(cs_5point1); }

	// keep loaded values inside what the dialog/decoder support
	void sanitize() {
		if (!(center_spread >= 0)) center_spread = 0;	// also catches NaN
		if (center_spread > 1) center_spread = 1;
		if (g_block_sizes[block_index(blocksize)] != blocksize) blocksize = g_default_blocksize;
		if (!(lfe_cutoff >= 0)) lfe_cutoff = 0;
		if (lfe_cutoff > 200) lfe_cutoff = 200;
		if (!(lfe_level >= 0)) lfe_level = 0;
		if (lfe_level > 2) lfe_level = 2;
		if (lfe_mode < 0 || lfe_mode > 1) lfe_mode = 0;
		if (lfe_slope < 0 || lfe_slope > 2) lfe_slope = 1;
	}

	// construct from DSP preset
	// (fields missing from older preset versions must start at their defaults, hence the initializers)
	freesurround_params(const dsp_preset &in): center_spread(0), blocksize(g_default_blocksize),
		lfe_cutoff(90), lfe_level(1), lfe_copy_lr(false), lfe_copy_c(false), lfe_mode(0), lfe_slope(1) {
		try {
			membuf mb((const char*)in.get_data(),(size_t)in.get_data_size());
			std::istream ss(&mb);
			boost::archive::binary_iarchive ar(ss);
			ar >> *this;
			sanitize();
		} catch(const std::exception &e) { console::warning((std::string("FreeSurround: unreadable DSP config (")+e.what()+"); using defaults.").c_str()); *this = freesurround_params(); }
		catch(...) { console::warning("FreeSurround: unreadable DSP config; using defaults."); *this = freesurround_params(); }
	}

	// convert to DSP preset
	void to_preset(dsp_preset &out) const {
		std::ostringstream s(std::ios::out | std::ios::binary); { boost::archive::binary_oarchive ar(s); ar << *this; }
		const std::string data = s.str();
		out.set_data(data.data(),data.size()); out.set_owner(fs_guid);
	}

	// compute the fb2k version of the channel setup code
	unsigned channels_fb() { unsigned res = 0; for (unsigned i=0; i<chanmap.size(); res |= chanmap[i++]); return res; }

	// assign a channel setup & recompute derived data
	void set_channels_fs(channel_setup setup) {
		channels_fs = setup;	
		chanmap.clear();
		typedef audio_chunk ac;
		using namespace boost::assign;
		// Note: Because fb2k does not define a few of the more exotic channels (side front left&right, side rear left&right, back center left&right), 
		// the side front/back channel pairs (both left and right sides, resp.) are mapped here onto foobar's top front/back channel pairs and the 
		// back (off-)center left/right channels are mapped onto foobar's top front center and top back center, respectively.
		// Therefore, these speakers should be connected to those outputs instead.
		std::map<channel_id,unsigned> fs2fb; insert(fs2fb)(ci_front_left,ac::channel_front_left)(ci_front_center_left,ac::channel_front_center_left)
			(ci_front_center,ac::channel_front_center)(ci_front_center_right,ac::channel_front_center_right)(ci_front_right,ac::channel_front_right)
			(ci_side_front_left,ac::channel_top_front_left)(ci_side_front_right,ac::channel_top_front_right)(ci_side_center_left,ac::channel_side_left)
			(ci_side_center_right,ac::channel_side_right)(ci_side_back_left,ac::channel_top_back_left)(ci_side_back_right,ac::channel_top_back_right)
			(ci_back_left,ac::channel_back_left)(ci_back_center_left,ac::channel_top_front_center)(ci_back_center,ac::channel_back_center)
			(ci_back_center_right,ac::channel_top_back_center)(ci_back_right,ac::channel_back_right)(ci_lfe,ac::channel_lfe);
		for (unsigned i=0;i<freesurround_decoder::num_channels(channels_fs);i++)
			chanmap += fs2fb[freesurround_decoder::channel_at(channels_fs,i)];
	}

	// the actual serialization/deserialization
	template<class A> void serialize(A &ar, const unsigned v) { ar & center_image & shift & depth & circular_wrap & focus & front_sep & rear_sep & channels_fs & chanmap & bass_lo & bass_hi & use_lfe; if (v >= 1) ar & center_spread & blocksize;
		if (v >= 2) ar & lfe_cutoff & lfe_level & lfe_copy_lr & lfe_copy_c & lfe_mode & lfe_slope;
		else if (A::is_loading::value) { lfe_mode = 1; lfe_cutoff = (float)sqrt((double)bass_lo*bass_hi); }	// old presets: soft FFT fade around the old transition band
	}
};



// feed fb2k samples into the float rechunker, whatever audio_sample is (float or double)
static inline void append_samples(stream_chunker<float> &r, float *p, unsigned n) { r.append(p,n); }
static inline void append_samples(stream_chunker<float> &r, double *p, unsigned n) { std::vector<float> t(p,p+n); r.append(&t[0],n); }

// the FreeSurround DSP plugin class
class freesurround_dsp: public dsp_impl_base {
public:
	// construct the plugin instance from a preset
	freesurround_dsp(const dsp_preset &in): params(in), rechunker(boost::bind(&freesurround_dsp::process_chunk,this,_1), params.blocksize*2), 
		decoder(params.channels_fs, params.blocksize), srate(44100), chan_code(params.channels_fb()), chunk_size(params.blocksize)		
	{
		// set up decoder parameters according to preset params
		decoder.circular_wrap(params.circular_wrap);
		decoder.shift(params.shift);
		decoder.depth(params.depth);
		decoder.focus(params.focus);
		decoder.center_image(params.center_image);
		decoder.front_separation(params.front_sep);
		decoder.rear_separation(params.rear_sep);
		decoder.center_spread(params.center_spread);
		apply_lfe();
	}

	// (re)send the bass management settings, they depend on the sampling rate
	void apply_lfe() {
		decoder.lfe(params.use_lfe, params.lfe_cutoff, params.lfe_level, params.lfe_copy_lr, params.lfe_copy_c, params.lfe_mode, params.lfe_slope, (float)srate);
	}

	// receive a chunk from foobar and buffer it
	bool on_chunk(audio_chunk *chunk, abort_callback &) {
		srate = chunk->get_srate();
		if (chunk->get_channel_config() == audio_chunk::channel_config_stereo) {
			append_samples(rechunker, chunk->get_data(), (unsigned)chunk->get_data_length());
			return false;
		}
		return true;
	}

	// process and emit a chunk (called by the rechunker when it's time)
	void process_chunk(float *stereo) {
		// append a new output chunk in foobar
		audio_chunk *chunk = insert_chunk();
		chunk->set_channels((unsigned)params.chanmap.size(),chan_code);
		chunk->set_sample_rate(srate);
		chunk->pad_with_silence(chunk_size);
		// set sampling rate dependent parameters
		apply_lfe();
		// decode original chunk into discrete multichannel
		float *src = decoder.decode(stereo);
		audio_sample *dst = chunk->get_data();
		// copy the data into the output chunk (respecting the different channel orders in fb2k and FS)
		for (unsigned c=0,num=(unsigned)params.chanmap.size();c<num;c++)
			for (unsigned s=0,idx=audio_chunk::g_channel_index_from_flag(chan_code,params.chanmap[c]); s<num*chunk_size; s += num)
				dst[idx+s] = (audio_sample)src[c+s];
	}

	// misc plugin interface
	void on_endoftrack(abort_callback &) { }
	bool need_track_change_mark() { return false; }
	void on_endofplayback(abort_callback &) { std::vector<float> tmp(chunk_size*2); process_chunk(&tmp[0]); }
	double get_latency() { return srate ? (rechunker.buffered()/2 + decoder.buffered()) / srate : 0; }
	void flush() { rechunker.flush(); decoder.flush(); }
	static void g_get_name(pfc::string_base &n) { n = "Real3D"; }
	static bool g_get_default_preset(dsp_preset &p) { freesurround_params().to_preset(p); return true; }
	static void g_show_config_popup(const dsp_preset &p, HWND wnd, dsp_preset_edit_callback &cbf);
	static bool g_have_config_popup() { return true; }
	static GUID g_get_guid() { return fs_guid; }

private:
	freesurround_params params;			// parameters
	stream_chunker<float> rechunker;	// gathers/splits the inbound data stream into equally-sized chunks
	freesurround_decoder decoder;		// the surround decoder
	unsigned srate, chan_code, chunk_size;			// last known sampling rate & fb2k channel setup code
};

static dsp_factory_t<freesurround_dsp> g_dsp_sample_factory;



// --- code for the config UI dialog ---

// a logo variant as premultiplied BGRA pixels (top-down)
struct logo_image { int w, h; std::vector<unsigned> px; logo_image(): w(0), h(0) {} };

class CConfigDialog : public CDialog {
	DECLARE_DYNAMIC(CConfigDialog)
public:
	CConfigDialog(freesurround_params &s, dsp_preset_edit_callback &cbf, CWnd* pParent = NULL);
	virtual ~CConfigDialog() { end_fade(); if (logo_bmp) ::DeleteObject(logo_bmp); }
	enum { IDD = IDD_DIALOG1 };
protected:
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) { refresh(); }
	afx_msg void OnCbnSelchangeConfig() { refresh(); }
	afx_msg void OnBnClickedRedir() { refresh(); }
	void refresh() { CDataExchange DX(this,true); DoDataExchange(&DX); }
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	void set_logo(), load_logos(), update_brush(), apply_control_themes(bool dark), begin_fade(), end_fade();
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *pResult);
	COLORREF bg_color() const; COLORREF text_color() const;
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR id);
	afx_msg void OnDarkToggle();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()
public:
	freesurround_params s;
	dsp_preset_edit_callback &cbf;
	HBITMAP logo_bmp;				// current composed logo
	logo_image logo_l, logo_d;		// scaled normal / dark logo variants
	int logo_w, logo_h; bool logo_ok;
	HWND fade_wnd; HBITMAP fade_bmp; bool fade_timer;	// snapshot overlay used for the theme cross-fade
	CBrush bg_brush;
	double theme_t, theme_from, theme_to;		// 0 = normal theme, 1 = dark theme (fades in between)
	DWORD anim_start;
	bool classic_applied;
	int slider_wrap, slider_shift, slider_depth, slider_focus;
	int combo_config, slider_frontsep, slider_rearsep, check_redir;
	int slider_lfecut, slider_lfelevel, check_copylr, check_copyc, combo_lfemode, combo_lfeslope;
	int slider_spread, combo_block;
};

IMPLEMENT_DYNAMIC(CConfigDialog, CDialog)

CConfigDialog::CConfigDialog(freesurround_params &s, dsp_preset_edit_callback &cbf, CWnd* pParent): 
	CDialog(CConfigDialog::IDD, pParent), s(s), cbf(cbf), logo_bmp(NULL), logo_w(0), logo_h(0), logo_ok(false), fade_wnd(NULL), fade_bmp(NULL), fade_timer(false), theme_t(0), theme_from(0), theme_to(0), anim_start(0), classic_applied(false), slider_wrap(s.circular_wrap*100/360), slider_shift(s.shift*50 + 50),
	slider_depth(s.depth*25), slider_focus(s.focus*50 + 50), slider_frontsep(s.front_sep*50), slider_rearsep(s.rear_sep*50),
	check_redir(s.use_lfe), slider_lfecut((int)(s.lfe_cutoff/2 + 0.5)), slider_lfelevel((int)(s.lfe_level*50 + 0.5)),
		check_copylr(s.lfe_copy_lr), check_copyc(s.lfe_copy_c), combo_lfemode(s.lfe_mode), combo_lfeslope(s.lfe_slope),
		slider_spread((int)(s.center_spread*100 + 0.5)), combo_block(block_index(s.blocksize))
{
	switch (s.channels_fs) {
		case cs_stereo: combo_config = 0; break;			
		case cs_3stereo: combo_config = 1; break;
		case cs_4point1: combo_config = 2; break;
		case cs_5point1: combo_config = 3; break;
		case cs_5stereo: combo_config = 4; break;
		case cs_legacy: combo_config = 5; break;
		case cs_6point1: combo_config = 6; break;
		case cs_7point1: combo_config = 7; break;
		case cs_7point1_panorama: combo_config = 8; break;
		case cs_7point1_tricenter: combo_config = 9; break;
		case cs_8point1: combo_config = 10; break;
		case cs_9point1_wrap: combo_config = 11; break;
		case cs_9point1_densepanorama: combo_config = 12; break;
		case cs_11point1_densewrap: combo_config = 13; break;
		case cs_13point1_totalwrap: combo_config = 14; break;
		case cs_16point1: combo_config = 15; break;
		default:
			combo_config = 0;
	}
}

BOOL CConfigDialog::OnInitDialog() {
	// fill the combo boxes before the first data exchange selects an entry
	::SendDlgItemMessageW(m_hWnd, IDC_LFEMODE, CB_ADDSTRING, 0, (LPARAM)L"Time-domain crossover (low ringing)");
	::SendDlgItemMessageW(m_hWnd, IDC_LFEMODE, CB_ADDSTRING, 0, (LPARAM)L"Soft FFT fade");
	::SendDlgItemMessageW(m_hWnd, IDC_LFESLOPE, CB_ADDSTRING, 0, (LPARAM)L"12 dB/octave");
	::SendDlgItemMessageW(m_hWnd, IDC_LFESLOPE, CB_ADDSTRING, 0, (LPARAM)L"24 dB/octave");
	::SendDlgItemMessageW(m_hWnd, IDC_LFESLOPE, CB_ADDSTRING, 0, (LPARAM)L"48 dB/octave");
	BOOL res = CDialog::OnInitDialog();
	load_logos();
	bool dark = cfg_theme_pref == 1 ? false : (cfg_theme_pref == 2 ? true : system_uses_dark());
	theme_t = dark ? 1.0 : 0.0; update_brush();
	if (dark) apply_control_themes(true);
	set_logo();
	return res;
}

// ---- dark mode -------------------------------------------------------------------------------------


static COLORREF lerp_color(COLORREF a, COLORREF b, double t) {
	return RGB((int)(GetRValue(a) + (GetRValue(b)-GetRValue(a))*t + 0.5), (int)(GetGValue(a) + (GetGValue(b)-GetGValue(a))*t + 0.5), (int)(GetBValue(a) + (GetBValue(b)-GetBValue(a))*t + 0.5));
}
static const COLORREF dark_bg = RGB(32,32,32), dark_text = RGB(235,235,235);
static const double fade_ms = 450;
static inline double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

COLORREF CConfigDialog::bg_color() const { return lerp_color(::GetSysColor(COLOR_BTNFACE), dark_bg, theme_t); }
COLORREF CConfigDialog::text_color() const { return lerp_color(::GetSysColor(COLOR_BTNTEXT), dark_text, theme_t); }

HBRUSH CConfigDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
	if (theme_t <= 0 && !classic_applied) return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	pDC->SetTextColor(text_color());
	pDC->SetBkColor(bg_color());
	return (HBRUSH)bg_brush.GetSafeHandle();
}

BOOL CConfigDialog::OnEraseBkgnd(CDC* pDC) {
	if (theme_t <= 0 && !classic_applied) return CDialog::OnEraseBkgnd(pDC);
	CRect rc; GetClientRect(&rc); pDC->FillSolidRect(&rc, bg_color());
	return TRUE;
}

void CConfigDialog::update_brush() { bg_brush.DeleteObject(); bg_brush.CreateSolidBrush(bg_color()); }

// switch the system-drawn parts of the controls between the dark and the normal visual style
void CConfigDialog::apply_control_themes(bool dark) {
	classic_applied = dark;
	for (HWND h = ::GetWindow(m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT)) {
		wchar_t cls[64] = L""; ::GetClassNameW(h, cls, 63);
		if (!dark) { ::SetWindowTheme(h, NULL, NULL); continue; }
		if (!_wcsicmp(cls, L"Button")) {
			LONG type = (LONG)::GetWindowLongPtr(h, GWL_STYLE) & BS_TYPEMASK;
			if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) ::SetWindowTheme(h, L"DarkMode_Explorer", NULL);
			else ::SetWindowTheme(h, L"", L"");		// check boxes / group boxes: classic drawing so that our text color is used
		} else if (!_wcsicmp(cls, L"ComboBox")) ::SetWindowTheme(h, L"DarkMode_CFD", L"ComboBox");
		else if (!_wcsicmp(cls, L"msctls_trackbar32")) ::SetWindowTheme(h, L"DarkMode_Explorer", NULL);
	}
	BOOL on = dark; ::DwmSetWindowAttribute(m_hWnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &on, sizeof(on));
	::RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// read a 32 bit bitmap resource and scale it (box filter) to fit w*h, aspect kept, left aligned, centered vertically
static bool load_logo(int id, int w, int h, logo_image &out) {
	HRSRC rs = ::FindResourceW(AfxGetResourceHandle(), MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(2) /*RT_BITMAP*/);
	if (!rs) return false;
	HGLOBAL hg = ::LoadResource(AfxGetResourceHandle(), rs); if (!hg) return false;
	const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER*)::LockResource(hg);
	if (!bih || bih->biBitCount != 32 || bih->biCompression != BI_RGB || bih->biWidth <= 0 || bih->biHeight == 0 || w <= 0 || h <= 0) return false;
	const int sw = bih->biWidth, sh = bih->biHeight < 0 ? -bih->biHeight : bih->biHeight; const bool bottom_up = bih->biHeight > 0;
	const unsigned *sp = (const unsigned*)((const BYTE*)bih + bih->biSize);
	int dw = w, dh = (int)((double)w*sh/sw + 0.5);
	if (dh > h) { dh = h; dw = (int)((double)h*sw/sh + 0.5); }
	if (dw < 1) dw = 1; if (dh < 1) dh = 1;
	out.w = w; out.h = h; out.px.assign((size_t)w*h, 0);
	const int oy = (h - dh)/2;
	for (int y = 0; y < dh; y++) {
		int y0 = (int)((double)y*sh/dh), y1 = (int)((double)(y+1)*sh/dh); if (y1 <= y0) y1 = y0+1; if (y1 > sh) y1 = sh;
		for (int x = 0; x < dw; x++) {
			int x0 = (int)((double)x*sw/dw), x1 = (int)((double)(x+1)*sw/dw); if (x1 <= x0) x1 = x0+1; if (x1 > sw) x1 = sw;
			unsigned long long s[4] = { 0, 0, 0, 0 }; int n = 0;
			for (int yy = y0; yy < y1; yy++) {
				const unsigned *row = sp + (size_t)(bottom_up ? sh-1-yy : yy)*sw;
				for (int xx = x0; xx < x1; xx++) { unsigned p = row[xx]; s[0] += p & 255; s[1] += (p >> 8) & 255; s[2] += (p >> 16) & 255; s[3] += p >> 24; n++; }
			}
			if (n) out.px[(size_t)(y+oy)*w + x] = (unsigned)(s[0]/n) | ((unsigned)(s[1]/n) << 8) | ((unsigned)(s[2]/n) << 16) | ((unsigned)(s[3]/n) << 24);
		}
	}
	return true;
}

// load both logo variants (black for the normal theme, colored for the dark theme) scaled to the logo control
void CConfigDialog::load_logos() {
	HWND ctl = ::GetDlgItem(m_hWnd, IDC_LOGO); if (!ctl) return;
	RECT rc; ::GetClientRect(ctl, &rc); logo_w = rc.right - rc.left; logo_h = rc.bottom - rc.top;
	logo_ok = load_logo(IDB_LOGO_LIGHT, logo_w, logo_h, logo_l) && load_logo(IDB_LOGO_DARK, logo_w, logo_h, logo_d);
}

// draw the logo variant of the current theme over the current background color into the logo control
void CConfigDialog::set_logo() {
	HWND ctl = ::GetDlgItem(m_hWnd, IDC_LOGO);
	if (!ctl || !logo_ok) return;
	const logo_image &img = theme_t >= 0.5 ? logo_d : logo_l;
	BITMAPINFO bmi = {}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = img.w; bmi.bmiHeader.biHeight = -img.h; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
	void *bits = NULL; HDC scr = ::GetDC(NULL);
	HBITMAP dst = ::CreateDIBSection(scr, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	::ReleaseDC(NULL, scr);
	if (!dst) return;
	const COLORREF bg = bg_color(); const unsigned br = GetRValue(bg), bgc = GetGValue(bg), bb = GetBValue(bg);
	unsigned *o = (unsigned*)bits;
	for (size_t i = 0; i < img.px.size(); i++) {
		unsigned p = img.px[i], a = p >> 24, ia = 255 - a;
		unsigned b = (p & 255) + (bb*ia + 127)/255, g = ((p >> 8) & 255) + (bgc*ia + 127)/255, r = ((p >> 16) & 255) + (br*ia + 127)/255;
		o[i] = (b > 255 ? 255 : b) | ((g > 255 ? 255 : g) << 8) | ((r > 255 ? 255 : r) << 16) | 0xFF000000u;
	}
	HBITMAP old = (HBITMAP)::SendMessage(ctl, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)dst);
	if (old) ::DeleteObject(old);
	if (logo_bmp && logo_bmp != old) ::DeleteObject(logo_bmp);
	logo_bmp = dst;
}

// --- cross-fade on theme change: a snapshot of the old look is laid over the dialog and faded out ---
void CConfigDialog::end_fade() {
	if (fade_timer) { KillTimer(1); fade_timer = false; }
	if (fade_wnd && ::IsWindow(fade_wnd)) ::DestroyWindow(fade_wnd);
	fade_wnd = NULL;
	if (fade_bmp) { ::DeleteObject(fade_bmp); fade_bmp = NULL; }
}

void CConfigDialog::begin_fade() {
	end_fade();
	RECT wr; ::GetWindowRect(m_hWnd, &wr); int w = wr.right - wr.left, h = wr.bottom - wr.top;
	if (w <= 0 || h <= 0) return;
	HDC scr = ::GetDC(NULL), mem = ::CreateCompatibleDC(scr);
	fade_bmp = ::CreateCompatibleBitmap(scr, w, h);
	if (!fade_bmp) { ::DeleteDC(mem); ::ReleaseDC(NULL, scr); return; }
	HGDIOBJ o = ::SelectObject(mem, fade_bmp);
	::BitBlt(mem, 0, 0, w, h, scr, wr.left, wr.top, SRCCOPY | CAPTUREBLT);
	fade_wnd = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
		wr.left, wr.top, w, h, m_hWnd, NULL, AfxGetInstanceHandle(), NULL);
	if (fade_wnd) {
		// hand the finished snapshot to the window before it is ever shown (no first paint that could flash blank/white)
		POINT pos = { wr.left, wr.top }, src0 = { 0, 0 }; SIZE sz = { w, h };
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, 0 };
		if (!::UpdateLayeredWindow(fade_wnd, scr, &pos, &sz, mem, &src0, 0, &bf, ULW_ALPHA)) { ::DestroyWindow(fade_wnd); fade_wnd = NULL; }
	}
	::SelectObject(mem, o); ::DeleteDC(mem); ::ReleaseDC(NULL, scr);
	if (!fade_wnd) { ::DeleteObject(fade_bmp); fade_bmp = NULL; return; }
	::ShowWindow(fade_wnd, SW_SHOWNOACTIVATE);
}

// hand cursor over the logo (it is the theme toggle)
BOOL CConfigDialog::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
	if (pWnd && pWnd->GetDlgCtrlID() == IDC_LOGO) { ::SetCursor(::LoadCursor(NULL, IDC_HAND)); return TRUE; }
	return CDialog::OnSetCursor(pWnd, nHitTest, message);
}

void CConfigDialog::OnDarkToggle() {
	bool dark = theme_t < 0.5;	// the logo toggles the theme
	cfg_theme_pref = dark ? 2 : 1;	// an explicit choice overrides following the system
	begin_fade();
	theme_t = dark ? 1.0 : 0.0; update_brush();
	apply_control_themes(dark);
	set_logo();
	::RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
	if (fade_wnd) { anim_start = 0; fade_timer = true; SetTimer(1, 15, NULL); }
}

void CConfigDialog::OnTimer(UINT_PTR id) {
	if (id != 1) { CDialog::OnTimer(id); return; }
	if (!anim_start) anim_start = ::GetTickCount();	// the clock starts at the first tick, after the (slow) first redraw
	double p = (::GetTickCount() - anim_start) / fade_ms;
	if (p >= 1 || !fade_wnd) { end_fade(); return; }
	double e = p*p*p*(p*(6*p - 15) + 10);	// smootherstep: gentle start and end
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)(255*(1-e) + 0.5), 0 };
	::UpdateLayeredWindow(fade_wnd, NULL, NULL, NULL, NULL, NULL, 0, &bf, ULW_ALPHA);
}

// dark drawing of the sliders (the system draws them with a light background and light track)
BOOL CConfigDialog::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *pResult) {
	NMHDR *hdr = (NMHDR*)lParam;
	if (hdr && hdr->code == NM_CUSTOMDRAW && classic_applied) {
		wchar_t cls[32] = L""; ::GetClassNameW(hdr->hwndFrom, cls, 31);
		if (!_wcsicmp(cls, L"msctls_trackbar32")) {
			NMCUSTOMDRAW *cd = (NMCUSTOMDRAW*)lParam;
			switch (cd->dwDrawStage) {
			case CDDS_PREPAINT:
				::FillRect(cd->hdc, &cd->rc, (HBRUSH)bg_brush.GetSafeHandle());
				*pResult = CDRF_NOTIFYITEMDRAW; return TRUE;
			case CDDS_ITEMPREPAINT:
				if (cd->dwItemSpec == TBCD_CHANNEL) {
					RECT r = cd->rc; int mid = (r.top + r.bottom)/2; r.top = mid - 2; r.bottom = mid + 2;
					HBRUSH b = ::CreateSolidBrush(RGB(85,85,85)); ::FillRect(cd->hdc, &r, b); ::DeleteObject(b);
					*pResult = CDRF_SKIPDEFAULT; return TRUE;
				}
				if (cd->dwItemSpec == TBCD_THUMB) {
					HBRUSH b = ::CreateSolidBrush(RGB(0,120,215)); ::FillRect(cd->hdc, &cd->rc, b); ::DeleteObject(b);
					*pResult = CDRF_SKIPDEFAULT; return TRUE;
				}
				if (cd->dwItemSpec == TBCD_TICS) {
					HWND tb = hdr->hwndFrom; RECT ch, th; ::SendMessage(tb, TBM_GETCHANNELRECT, 0, (LPARAM)&ch); ::SendMessage(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
					int n = (int)::SendMessage(tb, TBM_GETNUMTICS, 0, 0), tw = th.right - th.left;
					HBRUSH b = ::CreateSolidBrush(RGB(140,140,140));
					for (int i = -1; i <= n - 1; i++) {
						int x = i < 0 ? ch.left + tw/2 : (i == n - 1 ? ch.right - tw/2 : (int)::SendMessage(tb, TBM_GETTICPOS, i, 0));
						RECT t1 = { x, ch.top - 8, x + 1, ch.top - 3 }, t2 = { x, ch.bottom + 3, x + 1, ch.bottom + 8 };
						::FillRect(cd->hdc, &t1, b); ::FillRect(cd->hdc, &t2, b);
					}
					::DeleteObject(b);
					*pResult = CDRF_SKIPDEFAULT; return TRUE;
				}
				break;
			}
			*pResult = CDRF_DODEFAULT; return TRUE;
		}
	}
	return CDialog::OnNotify(wParam, lParam, pResult);
}
void CConfigDialog::DoDataExchange(CDataExchange* pDX) {
	channel_setup remap[] = 
		{cs_stereo, cs_3stereo, cs_4point1, cs_5point1, cs_5stereo, cs_legacy, cs_6point1, cs_7point1, 
		 cs_7point1_panorama, cs_7point1_tricenter, cs_8point1, cs_9point1_wrap, cs_9point1_densepanorama, 
	     cs_11point1_densewrap, cs_13point1_totalwrap, cs_16point1};
	// get / set data
	CDialog::DoDataExchange(pDX);
	DDX_Slider(pDX, IDC_WRAP, slider_wrap);
	DDX_Slider(pDX, IDC_SHIFT, slider_shift);
	DDX_Slider(pDX, IDC_DEPTH, slider_depth);
	DDX_Slider(pDX, IDC_FOCUS, slider_focus);
	DDX_CBIndex(pDX, IDC_CONFIG, combo_config);
	DDX_Slider(pDX, IDC_FRONTSEP, slider_frontsep);
	DDX_Slider(pDX, IDC_REARSEP, slider_rearsep);
	DDX_Check(pDX, IDC_REDIR, check_redir);
	DDX_Slider(pDX, IDC_LFECUT, slider_lfecut);
	DDX_Slider(pDX, IDC_LFELEVEL, slider_lfelevel);
	DDX_Check(pDX, IDC_COPYLR, check_copylr);
	DDX_Check(pDX, IDC_COPYC, check_copyc);
	DDX_CBIndex(pDX, IDC_LFEMODE, combo_lfemode);
	DDX_CBIndex(pDX, IDC_LFESLOPE, combo_lfeslope);
	DDX_Slider(pDX, IDC_SPREAD, slider_spread);
	DDX_CBIndex(pDX, IDC_BLOCKSIZE, combo_block);
	// copy to settings
	s.circular_wrap = slider_wrap*360.0/100.0;
	s.shift = slider_shift/50.0-1.0;
	s.depth = slider_depth/25.0;
	s.focus = slider_focus/50.0-1.0;
	s.front_sep = slider_frontsep/50.0;
	s.rear_sep = slider_rearsep/50.0;
	s.lfe_cutoff = slider_lfecut*2.0f;
	s.lfe_level = slider_lfelevel/50.0f;
	s.lfe_copy_lr = check_copylr != 0;
	s.lfe_copy_c = check_copyc != 0;
	if (combo_lfemode >= 0 && combo_lfemode <= 1) s.lfe_mode = combo_lfemode;
	if (combo_lfeslope >= 0 && combo_lfeslope <= 2) s.lfe_slope = combo_lfeslope;
	s.use_lfe = check_redir;
	s.center_spread = slider_spread/100.0f;
	if (combo_block >= 0 && combo_block < g_num_block_sizes) s.blocksize = g_block_sizes[combo_block];
	s.set_channels_fs(remap[combo_config]);
	// update display
	::uSetDlgItemText(*this,IDC_WRAPT, (boost::format("(%.0f)") % s.circular_wrap).str().c_str());
	::uSetDlgItemText(*this,IDC_SHIFTT, (boost::format("(%+.2f)") % s.shift).str().c_str());
	::uSetDlgItemText(*this,IDC_DEPTHT, (boost::format("(%.2fx)") % s.depth).str().c_str());
	::uSetDlgItemText(*this,IDC_FOCUST, (boost::format("(%+.2f)") % s.focus).str().c_str());
	::uSetDlgItemText(*this,IDC_FRONTSEPT, (boost::format("(%.2fx)") % s.front_sep).str().c_str());
	::uSetDlgItemText(*this,IDC_REARSEPT, (boost::format("(%.2fx)") % s.rear_sep).str().c_str());
	::uSetDlgItemText(*this,IDC_LFECUTT, s.lfe_cutoff < 1 ? "(off)" : (boost::format("(%.0f Hz)") % s.lfe_cutoff).str().c_str());
	::uSetDlgItemText(*this,IDC_LFELEVELT, (boost::format("(%.0f%%)") % (s.lfe_level*100)).str().c_str());
	::EnableWindow(::GetDlgItem(m_hWnd,IDC_LFESLOPE), s.lfe_mode == 0);
	::uSetDlgItemText(*this,IDC_SPREADT, (boost::format("(%.0f%%)") % (s.center_spread*100)).str().c_str());
	// update preset
	dsp_preset_impl tmp; s.to_preset(tmp);
	cbf.on_preset_changed(tmp);
}

BEGIN_MESSAGE_MAP(CConfigDialog, CDialog)
	ON_WM_HSCROLL()
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_STN_CLICKED(IDC_LOGO, &CConfigDialog::OnDarkToggle)
	ON_WM_SETCURSOR()
	ON_CBN_SELCHANGE(IDC_CONFIG, &CConfigDialog::OnCbnSelchangeConfig)
	ON_BN_CLICKED(IDC_REDIR, &CConfigDialog::OnBnClickedRedir)
	ON_BN_CLICKED(IDC_COPYLR, &CConfigDialog::OnBnClickedRedir)
	ON_BN_CLICKED(IDC_COPYC, &CConfigDialog::OnBnClickedRedir)
	ON_CBN_SELCHANGE(IDC_LFEMODE, &CConfigDialog::OnCbnSelchangeConfig)
	ON_CBN_SELCHANGE(IDC_LFESLOPE, &CConfigDialog::OnCbnSelchangeConfig)
	ON_CBN_SELCHANGE(IDC_BLOCKSIZE, &CConfigDialog::OnCbnSelchangeConfig)
END_MESSAGE_MAP()

void freesurround_dsp::g_show_config_popup(const dsp_preset &p, HWND wnd, dsp_preset_edit_callback &cbf) { 
	freesurround_params fp(p);
	CConfigDialog popup(fp,cbf,NULL);
	if (popup.DoModal() != IDOK) 
		cbf.on_preset_changed(p);
}
