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
#include <boost/format.hpp>
#include <functional>
#include <strstream>
#include <numeric>
#include <vector>

DECLARE_COMPONENT_VERSION("FreeSurround","0.9.0","FreeSurround DSP\n\nwritten by pro_optimizer");

// {F856C6D5-1888-4de3-8761-0A4CD132A33A}
static const GUID fs_guid = { 0xf856c6d5, 0x1888, 0x4de3, { 0x87, 0x61, 0xa, 0x4c, 0xd1, 0x32, 0xa3, 0x3a } };



// holds the user-configurable parameters of the FreeSurround DSP
struct freesurround_params {
	// the user-configurable parameters
	float center_image, shift, depth, circular_wrap, focus, front_sep, rear_sep, bass_lo, bass_hi;
	bool use_lfe;
	channel_setup channels_fs;		// FreeSurround channel setup	
	std::vector<unsigned> chanmap;	// FreeSurround -> fb2k channel index translation (derived data for faster lookup)

	// construct with defaults
	freesurround_params(): center_image(0.7), shift(0), depth(1), circular_wrap(90), focus(0), front_sep(1), rear_sep(1), 
		bass_lo(40), bass_hi(90), use_lfe(false) { set_channels_fs(cs_5point1); }

	// construct from DSP preset
	freesurround_params(const dsp_preset &in) {
		try {
			std::strstream ss((char*)in.get_data(),(int)in.get_data_size());
			boost::archive::binary_iarchive ar(ss);
			ar >> *this;
		} catch(...) { console::warning("Unsupported DSP config version; using defaults."); *this = freesurround_params(); }
	}

	// convert to DSP preset
	void to_preset(dsp_preset &out) const {
		std::strstream s; { boost::archive::binary_oarchive ar(s); ar << *this; }
		out.set_data(s.str(),(size_t)s.tellp()); out.set_owner(fs_guid);
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
	template<class A> void serialize(A &ar, const unsigned v) { ar & center_image & shift & depth & circular_wrap & focus & front_sep & rear_sep & channels_fs & chanmap & bass_lo & bass_hi & use_lfe; }
};



// feed fb2k samples into the float rechunker, whatever audio_sample is (float or double)
static inline void append_samples(stream_chunker<float> &r, float *p, unsigned n) { r.append(p,n); }
static inline void append_samples(stream_chunker<float> &r, double *p, unsigned n) { std::vector<float> t(p,p+n); r.append(&t[0],n); }

// the FreeSurround DSP plugin class
class freesurround_dsp: public dsp_impl_base {
	enum { chunk_size = 4096 };
public:
	// construct the plugin instance from a preset
	freesurround_dsp(const dsp_preset &in): params(in), rechunker(boost::bind(&freesurround_dsp::process_chunk,this,_1), chunk_size*2), 
		decoder(params.channels_fs), srate(44100), chan_code(params.channels_fb())		
	{
		// set up decoder parameters according to preset params
		decoder.circular_wrap(params.circular_wrap);
		decoder.shift(params.shift);
		decoder.depth(params.depth);
		decoder.focus(params.focus);
		decoder.center_image(params.center_image);
		decoder.front_separation(params.front_sep);
		decoder.rear_separation(params.rear_sep);
		decoder.bass_redirection(params.use_lfe);
		decoder.low_cutoff(params.bass_lo/(srate/2.0));
		decoder.high_cutoff(params.bass_hi/(srate/2.0));
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
		decoder.low_cutoff(params.bass_lo/(srate/2.0));
		decoder.high_cutoff(params.bass_hi/(srate/2.0));
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
	static void g_get_name(pfc::string_base &n) { n = "FreeSurround"; }
	static bool g_get_default_preset(dsp_preset &p) { freesurround_params().to_preset(p); return true; }
	static void g_show_config_popup(const dsp_preset &p, HWND wnd, dsp_preset_edit_callback &cbf);
	static bool g_have_config_popup() { return true; }
	static GUID g_get_guid() { return fs_guid; }

private:
	freesurround_params params;			// parameters
	stream_chunker<float> rechunker;	// gathers/splits the inbound data stream into equally-sized chunks
	freesurround_decoder decoder;		// the surround decoder
	unsigned srate, chan_code;			// last known sampling rate & fb2k channel setup code
};

static dsp_factory_t<freesurround_dsp> g_dsp_sample_factory;



// --- code for the config UI dialog ---

class CConfigDialog : public CDialog {
	DECLARE_DYNAMIC(CConfigDialog)
public:
	CConfigDialog(freesurround_params &s, dsp_preset_edit_callback &cbf, CWnd* pParent = NULL);
	virtual ~CConfigDialog() {}
	enum { IDD = IDD_DIALOG1 };
protected:
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) { refresh(); }
	afx_msg void OnCbnSelchangeConfig() { refresh(); }
	afx_msg void OnBnClickedRedir() { refresh(); }
	void refresh() { CDataExchange DX(this,true); DoDataExchange(&DX); }
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()
public:
	freesurround_params s;
	dsp_preset_edit_callback &cbf;
	int slider_wrap, slider_shift, slider_depth, slider_focus;
	int combo_config, slider_frontsep, slider_rearsep, check_redir;
	int slider_basslo, slider_basshi;
};

IMPLEMENT_DYNAMIC(CConfigDialog, CDialog)

CConfigDialog::CConfigDialog(freesurround_params &s, dsp_preset_edit_callback &cbf, CWnd* pParent): 
	CDialog(CConfigDialog::IDD, pParent), s(s), cbf(cbf), slider_wrap(s.circular_wrap*100/360), slider_shift(s.shift*50 + 50),
	slider_depth(s.depth*25), slider_focus(s.focus*50 + 50), slider_frontsep(s.front_sep*50), slider_rearsep(s.rear_sep*50),
	check_redir(s.use_lfe), slider_basslo(s.bass_lo/1.5), slider_basshi(s.bass_hi/1.5)
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
	DDX_Slider(pDX, IDC_BASSLO, slider_basslo);
	DDX_Slider(pDX, IDC_BASSHI, slider_basshi);
	// copy to settings
	s.circular_wrap = slider_wrap*360.0/100.0;
	s.shift = slider_shift/50.0-1.0;
	s.depth = slider_depth/25.0;
	s.focus = slider_focus/50.0-1.0;
	s.front_sep = slider_frontsep/50.0;
	s.rear_sep = slider_rearsep/50.0;
	s.bass_lo = slider_basslo*1.5;
	s.bass_hi = slider_basshi*1.5;
	s.use_lfe = check_redir;
	s.set_channels_fs(remap[combo_config]);
	// update display
	::uSetDlgItemText(*this,IDC_WRAPT, (boost::format("(%.0f)") % s.circular_wrap).str().c_str());
	::uSetDlgItemText(*this,IDC_SHIFTT, (boost::format("(%+.2f)") % s.shift).str().c_str());
	::uSetDlgItemText(*this,IDC_DEPTHT, (boost::format("(%.2fx)") % s.depth).str().c_str());
	::uSetDlgItemText(*this,IDC_FOCUST, (boost::format("(%+.2f)") % s.focus).str().c_str());
	::uSetDlgItemText(*this,IDC_FRONTSEPT, (boost::format("(%.2fx)") % s.front_sep).str().c_str());
	::uSetDlgItemText(*this,IDC_REARSEPT, (boost::format("(%.2fx)") % s.rear_sep).str().c_str());
	::uSetDlgItemText(*this,IDC_BASSLOT, (boost::format("(%.0fHz)") % s.bass_lo).str().c_str());
	::uSetDlgItemText(*this,IDC_BASSHIT, (boost::format("(%.0fHz)") % s.bass_hi).str().c_str());
	// update preset
	dsp_preset_impl tmp; s.to_preset(tmp);
	cbf.on_preset_changed(tmp);
}

BEGIN_MESSAGE_MAP(CConfigDialog, CDialog)
	ON_WM_HSCROLL()
	ON_CBN_SELCHANGE(IDC_CONFIG, &CConfigDialog::OnCbnSelchangeConfig)
	ON_BN_CLICKED(IDC_REDIR, &CConfigDialog::OnBnClickedRedir)
END_MESSAGE_MAP()

void freesurround_dsp::g_show_config_popup(const dsp_preset &p, HWND wnd, dsp_preset_edit_callback &cbf) { 
	freesurround_params fp(p);
	CConfigDialog popup(fp,cbf,NULL);
	if (popup.DoModal() != IDOK) 
		cbf.on_preset_changed(p);
}
