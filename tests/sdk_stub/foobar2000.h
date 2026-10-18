// minimal stand-in for the foobar2000 SDK + MFC, only to type-check freesurround.cpp
#pragma once
#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>
#include <map>
#ifndef FB2K_SAMPLE_T
#define FB2K_SAMPLE_T float
#endif
typedef FB2K_SAMPLE_T audio_sample;
typedef size_t t_size; typedef unsigned UINT; typedef void* HWND;
struct GUID { unsigned a; unsigned short b, c; unsigned char d[8]; };
#define DECLARE_COMPONENT_VERSION(a,b,c)
namespace pfc { struct string_base { string_base &operator=(const char*) { return *this; } }; }
namespace console { inline void warning(const char*) {} }
struct abort_callback {};
struct audio_chunk {
  enum { channel_front_left=1, channel_front_right=2, channel_front_center=4, channel_lfe=8, channel_back_left=16, channel_back_right=32,
    channel_front_center_left=64, channel_front_center_right=128, channel_back_center=256, channel_side_left=512, channel_side_right=1024,
    channel_top_center=2048, channel_top_front_left=4096, channel_top_front_center=8192, channel_top_front_right=16384,
    channel_top_back_left=32768, channel_top_back_center=65536, channel_top_back_right=131072, channel_config_stereo=3 };
  unsigned get_srate() { return 44100; } unsigned get_channel_config() { return 3; }
  audio_sample *get_data() { return 0; } t_size get_data_length() { return 0; }
  void set_channels(unsigned, unsigned) {} void set_sample_rate(unsigned) {} void pad_with_silence(t_size) {}
  static unsigned g_channel_index_from_flag(unsigned, unsigned) { return 0; }
};
struct dsp_preset { const void *get_data() const { return 0; } t_size get_data_size() const { return 0; }
  void set_data(const void*, t_size) {} void set_owner(const GUID&) {} };
struct dsp_preset_impl : dsp_preset {};
struct dsp_preset_edit_callback { virtual void on_preset_changed(const dsp_preset&) = 0; };
struct dsp_impl_base { audio_chunk *insert_chunk(t_size = 0) { return 0; } };
template<class T> struct dsp_factory_t {};
// MFC-ish
struct CWnd { operator HWND() const { return 0; } };
struct CScrollBar {}; struct CDataExchange { CDataExchange(CWnd*, bool) {} };
struct CDialog : CWnd { CDialog(UINT, CWnd*) {} virtual ~CDialog() {} virtual void DoDataExchange(CDataExchange*) {} int DoModal() { return 1; } };
enum { IDOK = 1 };
inline void DDX_Slider(CDataExchange*, int, int&) {} inline void DDX_CBIndex(CDataExchange*, int, int&) {} inline void DDX_Check(CDataExchange*, int, int&) {}
inline void uSetDlgItemText(HWND, int, const char*) {}
#define afx_msg
#define DECLARE_DYNAMIC(x)
#define IMPLEMENT_DYNAMIC(a,b)
#define DECLARE_MESSAGE_MAP()
#define BEGIN_MESSAGE_MAP(a,b)
#define END_MESSAGE_MAP()
#define ON_WM_HSCROLL()
#define ON_CBN_SELCHANGE(id,fn)
#define ON_BN_CLICKED(id,fn)
