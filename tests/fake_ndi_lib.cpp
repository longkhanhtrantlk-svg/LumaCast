// Thu vien "NDI gia" cho test: xuat dung cac ham C ma NdiRuntime.cpp tim (cung ABI tu khai bao) de kiem chung DUONG NAP DONG + chuyen doi dinh dang.
// KHONG phai NDI that: khong kiem chung duoc ABI voi header chinh thuc cua NDI SDK.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define EXP extern "C" __declspec(dllexport)
#else
#define EXP extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct source_t { const char* p_ndi_name; const char* p_url_address; };
struct find_create_t { bool show_local_sources; const char* p_groups; const char* p_extra_ips; };
struct recv_create_v3_t { source_t source; int color_format; int bandwidth; bool allow_video_fields; const char* name; };
struct video_frame_v2_t { int xres, yres; uint32_t FourCC; int frame_rate_N, frame_rate_D; float aspect; int format_type; int64_t timecode; uint8_t* p_data; int stride; const char* p_metadata; int64_t timestamp; };
struct audio_frame_v2_t { int sample_rate, no_channels, no_samples; int64_t timecode; float* p_data; int channel_stride; const char* p_metadata; int64_t timestamp; };
struct send_create_t { const char* name; const char* groups; bool clock_video; bool clock_audio; };
constexpr uint32_t cc(char a, char b, char c, char d) { return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24; }

source_t g_sources[2] = {{"FAKE-PC (Camera 1)", "10.0.0.5:5961"}, {"FAKE-PC (Camera 2)", nullptr}};
struct Recv { int step = 0; std::string name; };
struct Send { std::string name; };
uint8_t g_pix[2 * 2 * 4];
float g_aud[2 * 4];
int g_videoSent = 0, g_audioSent = 0, g_lastW = 0, g_lastH = 0, g_lastAudioFrames = 0, g_lastAudioCh = 0, g_lastFourCC = 0;
uint8_t g_firstPixel[4];
float g_firstAudio[4];
int g_mode = 0;       // 0 = BGRA, 1 = RGBX, 2 = RGBA
}

EXP bool NDIlib_initialize() { return true; }
EXP void NDIlib_destroy() {}
EXP const char* NDIlib_version() { return "fake-ndi 1.0 (test)"; }
EXP void* NDIlib_find_create_v2(const find_create_t*) { return new int(1); }
EXP void NDIlib_find_destroy(void* p) { delete static_cast<int*>(p); }
EXP bool NDIlib_find_wait_for_sources(void*, uint32_t) { return true; }
EXP const source_t* NDIlib_find_get_current_sources(void*, uint32_t* n) { *n = 2; return g_sources; }
EXP void* NDIlib_recv_create_v3(const recv_create_v3_t* c) { auto* r = new Recv; r->name = c->source.p_ndi_name ? c->source.p_ndi_name : ""; return r; }
EXP void NDIlib_recv_destroy(void* p) { delete static_cast<Recv*>(p); }
EXP int NDIlib_recv_capture_v2(void* p, video_frame_v2_t* v, audio_frame_v2_t* a, void*, uint32_t) {
    auto* r = static_cast<Recv*>(p);
    const int step = r->step++ % 4;
    if (step == 0 || step == 2) {                    // khung hinh 2x2 (stride lon hon de thu dung stride): pixel (0,0) = (R10,G20,B30,A40) theo thu tu byte cua dinh dang
        static uint8_t buf[2 * 16];
        std::memset(buf, 0, sizeof buf);
        const uint8_t p[4] = {g_mode == 0 ? uint8_t(30) : uint8_t(10), 20, g_mode == 0 ? uint8_t(10) : uint8_t(30), 40};   // BGRA: B,G,R,A ; RGBA/RGBX: R,G,B,A
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) std::memcpy(buf + y * 16 + x * 4, p, 4);
        v->xres = 2; v->yres = 2;
        v->FourCC = g_mode == 0 ? cc('B', 'G', 'R', 'A') : g_mode == 1 ? cc('R', 'G', 'B', 'X') : cc('R', 'G', 'B', 'A');
        v->p_data = buf; v->stride = 16;
        return 1;
    }
    if (step == 1) {                                 // am thanh PLANAR: 2 kenh x 4 mau, stride 8 float (de thu dung channel_stride)
        static float planar[2 * 8];
        for (int i = 0; i < 4; ++i) { planar[i] = 0.1f * float(i + 1); planar[8 + i] = -0.1f * float(i + 1); }
        a->sample_rate = 44100; a->no_channels = 2; a->no_samples = 4; a->p_data = planar; a->channel_stride = 8 * int(sizeof(float));
        return 2;
    }
    return 0;
}
EXP void NDIlib_recv_free_video_v2(void*, const video_frame_v2_t*) {}
EXP void NDIlib_recv_free_audio_v2(void*, const audio_frame_v2_t*) {}
EXP void* NDIlib_send_create(const send_create_t* c) { auto* s = new Send; s->name = c->name; g_videoSent = g_audioSent = 0; return s; }
EXP void NDIlib_send_destroy(void* p) { delete static_cast<Send*>(p); }
EXP void NDIlib_send_send_video_v2(void*, const video_frame_v2_t* v) {
    ++g_videoSent; g_lastW = v->xres; g_lastH = v->yres; g_lastFourCC = int(v->FourCC);
    std::memcpy(g_firstPixel, v->p_data, 4);
}
EXP void NDIlib_send_send_audio_v2(void*, const audio_frame_v2_t* a) {
    ++g_audioSent; g_lastAudioFrames = a->no_samples; g_lastAudioCh = a->no_channels;
    g_firstAudio[0] = a->p_data[0]; g_firstAudio[1] = a->p_data[1];                                  // kenh 0: mau 0,1
    g_firstAudio[2] = a->p_data[a->channel_stride / int(sizeof(float))]; g_firstAudio[3] = a->p_data[a->channel_stride / int(sizeof(float)) + 1];   // kenh 1
}
EXP int NDIlib_send_get_no_connections(void*, uint32_t) { return 3; }

// ---- ham diu khien/kiem tra rieng cua thu vien gia ----
EXP void fake_ndi_set_mode(int m) { g_mode = m; }
EXP void fake_ndi_stats(int* v, int* a, int* w, int* h, int* alen, int* ach, int* fourcc, uint8_t* px, float* au) {
    *v = g_videoSent; *a = g_audioSent; *w = g_lastW; *h = g_lastH; *alen = g_lastAudioFrames; *ach = g_lastAudioCh; *fourcc = g_lastFourCC;
    std::memcpy(px, g_firstPixel, 4); std::memcpy(au, g_firstAudio, 4 * sizeof(float));
}
