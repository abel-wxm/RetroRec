// ==========================================
// VERSION: 2026-02-16_22-56_FIX_FINAL
// ==========================================
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <mutex>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

using Microsoft::WRL::ComPtr;

namespace retrorec {

    struct Point { int x, y; };
    struct RectArea { int x, y, w, h; };

    struct RawFrame {
        std::vector<uint8_t> data;
        int64_t capture_time_ms;
    };

    class AudioCapture {
    public:
        ComPtr<IAudioClient> audioClient;
        ComPtr<IAudioCaptureClient> captureClient;
        WAVEFORMATEX* pwfx = nullptr;
        bool initialized = false;

        bool init() {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ComPtr<IMMDeviceEnumerator> enumerator;
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return false;
            ComPtr<IMMDevice> device;
            if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return false;
            if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient))) return false;
            audioClient->GetMixFormat(&pwfx);
            if (FAILED(audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, pwfx, nullptr))) return false;
            if (FAILED(audioClient->GetService(IID_PPV_ARGS(&captureClient)))) return false;
            audioClient->Start();
            initialized = true;
            return true;
        }
        void read(std::vector<uint8_t>& buffer) {
            if (!initialized) return;
            UINT32 pktLen = 0; captureClient->GetNextPacketSize(&pktLen);
            while (pktLen != 0) {
                BYTE* pData; UINT32 nFrames; DWORD flags;
                captureClient->GetBuffer(&pData, &nFrames, &flags, nullptr, nullptr);
                if (nFrames > 0) buffer.insert(buffer.end(), pData, pData + (nFrames * pwfx->nBlockAlign));
                captureClient->ReleaseBuffer(nFrames);
                captureClient->GetNextPacketSize(&pktLen);
            }
        }
        ~AudioCapture() { if (audioClient) audioClient->Stop(); if (pwfx) CoTaskMemFree(pwfx); }
    };

    class RecorderEngine {
    private:
        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        ComPtr<IDXGIOutputDuplication> dxgi_duplication;
        ComPtr<ID3D11Texture2D> staging_texture;
        DXGI_OUTPUT_DESC output_desc;

        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* video_ctx = nullptr;
        AVStream* video_stream = nullptr;
        AVCodecContext* audio_ctx = nullptr;
        AVStream* audio_stream = nullptr;
        AVFrame* raw_frame = nullptr;
        AVFrame* audio_frame = nullptr;
        SwsContext* sws_ctx = nullptr;

        AudioCapture audio_cap;
        bool audio_enabled = false;
        bool is_initialized = false;
        bool is_recording = false;
        bool is_paused = false;
        
        bool paint_mode = false;
        bool mosaic_mode = false;
        std::vector<Point> strokes;
        std::vector<RectArea> mosaic_zones;
        std::mutex draw_mutex;

        std::deque<RawFrame> video_buffer;
        const int BUFFER_FRAMES = 90; 
        std::mutex buffer_mutex;

        int screen_width = 0;
        int screen_height = 0;
        
        int64_t video_pts = 0; // 核心：修复 undeclared identifier
        int64_t audio_samples_written = 0;
        
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point pause_start_time;
        std::chrono::duration<double> total_pause_duration;

    public:
        RecorderEngine() : total_pause_duration(0) {}
        ~RecorderEngine() { stopRecording(); }

        bool initialize() {
            if (is_initialized) return true;
            if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context))) return false;
            ComPtr<IDXGIDevice> dxgi_dev; d3d_device.As(&dxgi_dev);
            ComPtr<IDXGIAdapter> dxgi_adp; dxgi_dev->GetAdapter(&dxgi_adp);
            ComPtr<IDXGIOutput> dxgi_out; dxgi_adp->EnumOutputs(0, &dxgi_out);
            ComPtr<IDXGIOutput1> dxgi_out1; dxgi_out.As(&dxgi_out1);
            if (FAILED(dxgi_out1->DuplicateOutput(d3d_device.Get(), &dxgi_duplication))) return false;
            dxgi_out->GetDesc(&output_desc);
            screen_width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
            screen_height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
            if (screen_width % 2 != 0) screen_width--;
            if (screen_height % 2 != 0) screen_height--;
            audio_enabled = audio_cap.init();
            is_initialized = true;
            return true;
        }

        void togglePaintMode() { std::lock_guard<std::mutex> l(draw_mutex); paint_mode = !paint_mode; mosaic_mode = false; }
        void toggleMosaicMode() { std::lock_guard<std::mutex> l(draw_mutex); mosaic_mode = !mosaic_mode; paint_mode = false; }
        bool isPaintMode() { return paint_mode; }
        bool isMosaicMode() { return mosaic_mode; }
        void addStroke(int x, int y) { std::lock_guard<std::mutex> l(draw_mutex); strokes.push_back({x,y}); }
        void addMosaic(int x, int y, int w, int h) { std::lock_guard<std::mutex> l(draw_mutex); mosaic_zones.push_back({x,y,w,h}); }
        void clearEffects() { std::lock_guard<std::mutex> l(draw_mutex); strokes.clear(); mosaic_zones.clear(); }
        std::vector<Point> getStrokes() { std::lock_guard<std::mutex> l(draw_mutex); return strokes; }
        std::vector<RectArea> getMosaicZones() { std::lock_guard<std::mutex> l(draw_mutex); return mosaic_zones; }

        void applyRetroactiveMosaic() {
            std::lock_guard<std::mutex> l(buffer_mutex);
            std::lock_guard<std::mutex> dl(draw_mutex);
            for (auto& f : video_buffer) {
                uint8_t* d = f.data.data(); int ls = screen_width * 4;
                for (const auto& r : mosaic_zones) {
                    for (int y=r.y; y<r.y+r.h; y+=15) {
                        for (int x=r.x; x<r.x+r.w; x+=15) {
                            if (y>=screen_height || x>=screen_width) continue;
                            uint8_t b=d[y*ls+x*4], g=d[y*ls+x*4+1], rv=d[y*ls+x*4+2];
                            for (int by=y; by<(std::min)(y+15, r.y+r.h); by++)
                                for (int bx=x; bx<(std::min)(x+15, r.x+r.w); bx++)
                                    if (by<screen_height && bx<screen_width) { d[by*ls+bx*4]=b; d[by*ls+bx*4+1]=g; d[by*ls+bx*4+2]=rv; }
                        }
                    }
                }
            }
        }

        bool startRecording() {
            if (!is_initialized || is_recording) return false;
            char fn[64]; time_t t = time(0); tm l; localtime_s(&l, &t); strftime(fn, 64, "Rec_%Y%m%d_%H%M%S.mp4", &l);
            avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, fn);
            const AVCodec* vc = avcodec_find_encoder(AV_CODEC_ID_H264);
            video_stream = avformat_new_stream(fmt_ctx, vc);
            video_ctx = avcodec_alloc_context3(vc);
            video_ctx->width = screen_width; video_ctx->height = screen_height; video_ctx->time_base = {1, 30}; video_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            av_opt_set(video_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(video_ctx->priv_data, "crf", "23", 0);
            av_opt_set(video_ctx->priv_data, "tune", "zerolatency", 0);
            avcodec_open2(video_ctx, vc, nullptr);
            avcodec_parameters_from_context(video_stream->codec
