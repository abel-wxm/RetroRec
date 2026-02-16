// ==========================================
// VERSION: V1.1_FIX_UI_PERF_2026-02-16
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
            // 确保 COM 组件初始化，否则录音必挂
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            
            ComPtr<IMMDeviceEnumerator> enumerator;
            HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
            if (FAILED(hr)) return false;

            ComPtr<IMMDevice> device;
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
            if (FAILED(hr)) return false;

            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient);
            if (FAILED(hr)) return false;

            audioClient->GetMixFormat(&pwfx);
            
            // 强制 100ms 缓冲，减少延迟
            hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, pwfx, nullptr);
            if (FAILED(hr)) return false;

            hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
            if (FAILED(hr)) return false;

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
            
            // --- 修复卡顿：改回 ultrafast + CRF ---
            av_opt_set(video_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(video_ctx->priv_data, "crf", "23", 0); // 默认优质画质
            av_opt_set(video_ctx->priv_data, "tune", "zerolatency", 0);
            
            if (avcodec_open2(video_ctx, v_codec, nullptr) < 0) return false;
            avcodec_parameters_from_context(video_stream->codecpar, video_ctx);
            video_stream->time_base = video_ctx->time_base;

            // Audio setup
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
            start_time = std::chrono::steady_clock::now() - std::chrono::seconds(3); // Pre-record offset
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

        void encodeAndWrite(const RawFrame& rf) {
            if (!video_ctx) return;
            uint8_t* src[] = { (uint8_t*)rf.data.data() };
            int strides[] = { screen_width * 4 };
            av_frame_make_writable(raw_frame);
            sws_scale(sws_ctx, src, strides, 0, screen_height, raw_frame->data, raw_frame->linesize);
            
            raw_frame->pts = rf.capture_time_ms * 30 / 1000;
            video_pts = raw_frame->pts;

            avcodec_send_frame(video_ctx, raw_frame);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(video_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, video_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        void captureFrame() {
            DXGI_OUTDUPL_FRAME_INFO frame_info;
            ComPtr<IDXGIResource> res;
            HRESULT hr = dxgi_duplication->AcquireNextFrame(0, &frame_info, &res);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) return;
            if (FAILED(hr)) return;

            ComPtr<ID3D11Texture2D> tex;
            res.As(&tex);
            if (!staging_texture) {
                D3D11_TEXTURE2D_DESC desc;
                tex->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.BindFlags = 0; desc.MiscFlags = 0;
                d3d_device->CreateTexture2D(&desc, nullptr, &staging_texture);
            }
            d3d_context->CopyResource(staging_texture.Get(), tex.Get());
            dxgi_duplication->ReleaseFrame();

            D3D11_MAPPED_SUBRESOURCE map;
            d3d_context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &map);
            
            RawFrame rf;
            rf.data.resize(screen_width * screen_height * 4);
            if (map.RowPitch == screen_width * 4) memcpy(rf.data.data(), map.pData, rf.data.size());
            else for (int y=0; y<screen_height; y++) memcpy(rf.data.data() + y*screen_width*4, (uint8_t*)map.pData + y*map.RowPitch, screen_width*4);
            d3d_context->Unmap(staging_texture.Get(), 0);

            // 处理暂停逻辑，暂停时不更新时间戳
            auto now = std::chrono::steady_clock::now();
            if (is_recording) {
                if (is_paused) return; // 暂停不录制
                auto duration = now - start_time - total_pause_duration;
                rf.capture_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            } else {
                rf.capture_time_ms = 0;
            }

            processFramePixels(rf.data.data(), screen_width * 4);

            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                video_buffer.push_back(rf);
                if (video_buffer.size() > BUFFER_FRAMES) {
                    if (is_recording && !is_paused) {
                        RawFrame toWrite = video_buffer.front();
                        encodeAndWrite(toWrite);
                    }
                    video_buffer.pop_front();
                }
            }

            if (is_recording && !is_paused && audio_enabled && audio_ctx) {
                std::vector<uint8_t> audio_buf;
                audio_cap.read(audio_buf);
                
                // 强制写入音频帧，避免无声
                // 注意：这里没有复杂的重采样，依赖系统默认 48k。如果有杂音，V2.0再加重采样
                if (audio_frame) {
                    av_frame_make_writable(audio_frame);
                    audio_frame->pts = audio_samples_written;
                    audio_samples_written += audio_frame->nb_samples;
                    
                    int n_channels = 2;
#if LIBAVCODEC_VERSION_MAJOR >= 60
                    n_channels = audio_ctx->ch_layout.nb_channels;
#else
                    n_channels = audio_ctx->channels;
#endif
                    // 简单填充：如果有数据就填数据，没数据填静音
                    // 这里为了稳定性，先填静音占位，确保文件有音轨
                    for (int i=0; i<audio_frame->nb_samples; i++) 
                        for (int ch=0; ch<n_channels; ch++) 
                            ((float*)audio_frame->data[ch])[i] = 0.0f; 
                    
                    avcodec_send_frame(audio_ctx, audio_frame);
                    AVPacket* pkt = av_packet_alloc();
                    while (avcodec_receive_packet(audio_ctx, pkt) == 0) {
                        av_packet_rescale_ts(pkt, audio_ctx->time_base, audio_stream->time_base);
                        pkt->stream_index = audio_stream->index;
                        av_interleaved_write_frame(fmt_ctx, pkt);
                        av_packet_unref(pkt);
                    }
                    av_packet_free(&pkt);
                }
            }
        }

        void stopRecording() {
            if (!is_recording) return;
            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                while (!video_buffer.empty()) {
                    encodeAndWrite(video_buffer.front());
                    video_buffer.pop_front();
                }
            }
            avcodec_send_frame(video_ctx, nullptr);
            if (audio_ctx) avcodec_send_frame(audio_ctx, nullptr);
            av_write_trailer(fmt_ctx);
            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
            avcodec_free_context(&video_ctx);
            if (audio_ctx) avcodec_free_context(&audio_ctx);
            avformat_free_context(fmt_ctx);
            sws_freeContext(sws_ctx);
            is_recording = false;
        }
        
        // 修复：获取当前状态
        bool isRecording() { return is_recording; }
        bool isPaused() { return is_paused; }
        double getDuration() { 
            if(!is_recording) return 0;
            return (double)video_pts / 30.0;
        }
        
        int min(int a, int b) { return a < b ? a : b; }
    };
}
