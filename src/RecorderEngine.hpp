// ==========================================
// VERSION: 2026-02-16_18-45_FIX_COMPILE_ERRORS
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
#include <thread>

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
            CoInitialize(nullptr);
            ComPtr<IMMDeviceEnumerator> enumerator;
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
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
            UINT32 packetLength = 0;
            captureClient->GetNextPacketSize(&packetLength);
            while (packetLength != 0) {
                BYTE* pData;
                UINT32 numFramesAvailable;
                DWORD flags;
                captureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
                if (numFramesAvailable > 0) {
                    int bytes = numFramesAvailable * pwfx->nBlockAlign;
                    buffer.insert(buffer.end(), pData, pData + bytes);
                }
                captureClient->ReleaseBuffer(numFramesAvailable);
                captureClient->GetNextPacketSize(&packetLength);
            }
        }
        ~AudioCapture() {
            if (audioClient) audioClient->Stop();
            if (pwfx) CoTaskMemFree(pwfx);
        }
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
        
        // [FIX 1] 补回 video_pts
        int64_t video_pts = 0;
        int64_t audio_samples_written = 0;
        
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point pause_start_time;
        std::chrono::duration<double> total_pause_duration;

    public:
        RecorderEngine() : total_pause_duration(0) {}
        ~RecorderEngine() { stopRecording(); }

        bool initialize() {
            if (is_initialized) return true;
            HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context);
            if (FAILED(hr)) return false;

            ComPtr<IDXGIDevice> dxgi_device;
            d3d_device.As(&dxgi_device);
            ComPtr<IDXGIAdapter> dxgi_adapter;
            dxgi_device->GetAdapter(&dxgi_adapter);
            ComPtr<IDXGIOutput> dxgi_output;
            dxgi_adapter->EnumOutputs(0, &dxgi_output);

            ComPtr<IDXGIOutput1> dxgi_output1;
            hr = dxgi_output.As(&dxgi_output1);
            hr = dxgi_output1->DuplicateOutput(d3d_device.Get(), &dxgi_duplication);
            if (FAILED(hr)) return false;

            dxgi_output->GetDesc(&output_desc);
            screen_width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
            screen_height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
            
            if (screen_width % 2 != 0) screen_width--;
            if (screen_height % 2 != 0) screen_height--;

            audio_enabled = audio_cap.init();
            is_initialized = true;
            return true;
        }

        void togglePaintMode() { paint_mode = !paint_mode; mosaic_mode = false; }
        void toggleMosaicMode() { mosaic_mode = !mosaic_mode; paint_mode = false; }
        bool isPaintMode() const { return paint_mode; }
        bool isMosaicMode() const { return mosaic_mode; }
        void addStroke(int x, int y) { std::lock_guard<std::mutex> l(draw_mutex); strokes.push_back({x,y}); }
        void addMosaic(int x, int y, int w, int h) { std::lock_guard<std::mutex> l(draw_mutex); mosaic_zones.push_back({x,y,w,h}); }
        void clearEffects() { std::lock_guard<std::mutex> l(draw_mutex); strokes.clear(); mosaic_zones.clear(); }
        std::vector<Point> getStrokes() { std::lock_guard<std::mutex> l(draw_mutex); return strokes; }
        std::vector<RectArea> getMosaicZones() { std::lock_guard<std::mutex> l(draw_mutex); return mosaic_zones; }

        // [FIX 3] 补回 applyRetroactiveMosaic 函数
        void applyRetroactiveMosaic() {
            std::lock_guard<std::mutex> l(buffer_mutex);
            std::lock_guard<std::mutex> dl(draw_mutex);
            if (mosaic_zones.empty()) return;

            // 简单实现：遍历缓冲区，把当前马赛克应用到所有帧
            for (auto& frame : video_buffer) {
                uint8_t* data = frame.data.data();
                int linesize = screen_width * 4;
                int bs = 15;
                for (const auto& r : mosaic_zones) {
                    for (int y = r.y; y < r.y + r.h; y += bs) {
                        for (int x = r.x; x < r.x + r.w; x += bs) {
                            if (y >= screen_height || x >= screen_width) continue;
                            int off = y * linesize + x * 4;
                            uint8_t b = data[off];
                            uint8_t g = data[off+1];
                            uint8_t r_val = data[off+2];
                            for (int by = y; by < min(y+bs, r.y+r.h); by++) {
                                for (int bx = x; bx < min(x+bs, r.x+r.w); bx++) {
                                    if (by < screen_height && bx < screen_width) {
                                        int o = by * linesize + bx * 4;
                                        data[o] = b; data[o+1] = g; data[o+2] = r_val;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        void pauseRecording() {
            if (is_recording && !is_paused) {
                is_paused = true;
                pause_start_time = std::chrono::steady_clock::now();
            }
        }
        void resumeRecording() {
            if (is_recording && is_paused) {
                is_paused = false;
                total_pause_duration += (std::chrono::steady_clock::now() - pause_start_time);
            }
        }

        bool startRecording() {
            if (!is_initialized || is_recording) return false;

            time_t now = time(0);
            tm ltm;
            localtime_s(&ltm, &now);
            char time_buffer[64];
            strftime(time_buffer, 64, "Rec_%Y%m%d_%H%M%S.mp4", &ltm);
            std::string filename = std::string(time_buffer);

            avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename.c_str());

            const AVCodec* v_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            video_stream = avformat_new_stream(fmt_ctx, v_codec);
            video_ctx = avcodec_alloc_context3(v_codec);
            video_ctx->width = screen_width;
            video_ctx->height = screen_height;
            video_ctx->time_base = { 1, 30 };
            video_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            
            av_opt_set(video_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(video_ctx->priv_data, "crf", "23", 0);
            av_opt_set(video_ctx->priv_data, "tune", "zerolatency", 0);
            
            if (avcodec_open2(video_ctx, v_codec, nullptr) < 0) return false;
            avcodec_parameters_from_context(video_stream->codecpar, video_ctx);
            video_stream->time_base = video_ctx->time_base;

            const AVCodec* a_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            audio_stream = avformat_new_stream(fmt_ctx, a_codec);
            audio_ctx = avcodec_alloc_context3(a_codec);
            audio_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
            audio_ctx->bit_rate = 128000;
            audio_ctx->sample_rate = 48000;
            
#if LIBAVCODEC_VERSION_MAJOR >= 60
            AVChannelLayout layout;
            av_channel_layout_default(&layout, 2);
            audio_ctx->ch_layout = layout;
#else
            audio_ctx->channels = 2;
            audio_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
#endif

            if (avcodec_open2(audio_ctx, a_codec, nullptr) >= 0) {
                avcodec_parameters_from_context(audio_stream->codecpar, audio_ctx);
                audio_stream->time_base = { 1, audio_ctx->sample_rate };
                audio_frame = av_frame_alloc();
                audio_frame->nb_samples = audio_ctx->frame_size;
                audio_frame->format = audio_ctx->sample_fmt;
#if LIBAVCODEC_VERSION_MAJOR >= 60
                av_channel_layout_copy(&audio_frame->ch_layout, &audio_ctx->ch_layout);
#else
                audio_frame->channels = 2;
                audio_frame->channel_layout = AV_CH_LAYOUT_STEREO;
#endif
                av_frame_get_buffer(audio_frame, 0);
            }

            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE);
            avformat_write_header(fmt_ctx, nullptr);

            sws_ctx = sws_getContext(screen_width, screen_height, AV_PIX_FMT_BGRA, screen_width, screen_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
            raw_frame = av_frame_alloc();
            raw_frame->format = video_ctx->pix_fmt;
            raw_frame->width = screen_width;
            raw_frame->height = screen_height;
            av_frame_get_buffer(raw_frame, 32);

            audio_samples_written = 0;
            video_pts = 0;
            
            is_recording = true;
            is_paused = false;
            start_time = std::chrono::steady_clock::now() - std::chrono::seconds(3);
            total_pause_duration = std::chrono::duration<double>(0);

            return true;
        }

        void processFramePixels(uint8_t* data, int linesize) {
            std::lock_guard<std::mutex> lock(draw_mutex);
            int bs = 15;
            for (const auto& r : mosaic_zones) {
                for (int y = r.y; y < r.y + r.h; y += bs) {
                    for (int x = r.x; x < r.x + r.w; x += bs) {
                        if (y >= screen_height || x >= screen_width) continue;
                        int off = y * linesize + x * 4;
                        uint8_t b = data[off];
                        uint8_t g = data[off+1];
                        uint8_t r_val = data[off+2];
                        for (int by = y; by < min(y+bs, r.y+r.h); by++) {
                            for (int bx = x; bx < min(x+bs, r.x+r.w); bx++) {
                                if (by < screen_height && bx < screen_width) {
                                    int o = by * linesize + bx * 4;
                                    data[o] = b; data[o+1] = g; data[o+2] = r_val;
                                }
                            }
                        }
                    }
                }
            }
            for (const auto& p : strokes) {
                if (p.x >= 0 && p.x < screen_width && p.y >= 0 && p.y < screen_height) {
                     int off = p.y * linesize + p.x * 4;
                     data[off] = 0; data[off+1] = 0; data[off+2] = 255;
                }
            }
        }

        void encodeAndWrite(const
