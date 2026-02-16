// ==========================================
// VERSION: 2026-02-16_15-40 (Structure Fix)
// ==========================================
#pragma once

// 强制定义宏，防止 Windows.h 里的 min/max 干扰标准库
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <mutex>
#include <algorithm> 

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

using Microsoft::WRL::ComPtr;

namespace retrorec {

    struct Point { int x, y; };
    struct RectArea { int x, y, w, h; };

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
        AVFrame* audio_frame = nullptr;
        
        AVFrame* raw_frame = nullptr;
        SwsContext* sws_ctx = nullptr;

        bool is_initialized = false;
        bool is_recording = false;
        bool is_paused = false;
        
        bool paint_mode = false;
        bool mosaic_mode = false;
        
        std::vector<Point> strokes;
        std::vector<RectArea> mosaic_zones;
        std::mutex draw_mutex;

        int screen_width = 0;
        int screen_height = 0;
        int64_t video_pts = 0;
        int64_t audio_pts = 0;

        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point pause_start_time;
        std::chrono::duration<double> total_pause_duration;

    public:
        RecorderEngine() : total_pause_duration(0) {}
        ~RecorderEngine() { stopRecording(); cleanup(); }

        bool initialize() {
            if (is_initialized) return true;
            HRESULT hr;

            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context);
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

            is_initialized = true;
            return true;
        } // END initialize

        // --- 控制接口 ---
        void togglePaintMode() { paint_mode = !paint_mode; mosaic_mode = false; }
        void toggleMosaicMode() { mosaic_mode = !mosaic_mode; paint_mode = false; }
        bool isPaintMode() const { return paint_mode; }
        bool isMosaicMode() const { return mosaic_mode; }
        
        void clearEffects() { 
            std::lock_guard<std::mutex> lock(draw_mutex); 
            strokes.clear(); 
            mosaic_zones.clear(); 
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
            video_ctx->gop_size = 10;
            video_ctx->max_b_frames = 0;
            
            // 无损参数
            av_opt_set(video_ctx->priv_data, "preset", "ultrafast", 0);
            av_opt_set(video_ctx->priv_data, "crf", "18", 0);
            av_opt_set(video_ctx->priv_data, "tune", "zerolatency", 0);
            
            if (avcodec_open2(video_ctx, v_codec, nullptr) < 0) return false;
            avcodec_parameters_from_context(video_stream->codecpar, video_ctx);
            video_stream->time_base = video_ctx->time_base;

            // 音频占位
            const AVCodec* a_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (a_codec) {
                audio_stream = avformat_new_stream(fmt_ctx, a_codec);
                audio_ctx = avcodec_alloc_context3(a_codec);
                audio_ctx->sample_fmt = a_codec->sample_fmts ? a_codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
                audio_ctx->bit_rate = 128000;
                audio_ctx->sample_rate = 44100;
                audio_ctx->channels = 2;
                audio_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
                if (avcodec_open2(audio_ctx, a_codec, nullptr) >= 0) {
                    avcodec_parameters_from_context(audio_stream->codecpar, audio_ctx);
                    audio_stream->time_base = { 1, audio_ctx->sample_rate };
                    audio_frame = av_frame_alloc();
                    audio_frame->nb_samples = audio_ctx->frame_size;
                    audio_frame->format = audio_ctx->sample_fmt;
                    audio_frame->channel_layout = audio_ctx->channel_layout;
                    av_frame_get_buffer(audio_frame, 0);
                }
            }

            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return false;
            }

            avformat_write_header(fmt_ctx, nullptr);

            sws_ctx = sws_getContext(screen_width, screen_height, AV_PIX_FMT_BGRA,
                                     screen_width, screen_height, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
            
            raw_frame = av_frame_alloc();
            raw_frame->format = video_ctx->pix_fmt;
            raw_frame->width = screen_width;
            raw_frame->height = screen_height;
            av_frame_get_buffer(raw_frame, 32);

            video_pts = 0;
            audio_pts = 0;
            is_recording = true;
            is_paused = false;
            total_pause_duration = std::chrono::duration<double>(0);
            start_time = std::chrono::steady_clock::now();

            return true;
        } // END startRecording

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

        void processFrame(uint8_t* data, int linesize) {
            std::lock_guard<std::mutex> lock(draw_mutex);

            // 1. 马赛克
            int blockSize = 15; 
            for (const auto& rect : mosaic_zones) {
                int startX = std::max(0, rect.x);
                int startY = std::max(0, rect.y);
                int endX = std::min(screen_width, rect.x + rect.w);
                int endY = std::min(screen_height, rect.y + rect.h);

                for (int y = startY; y < endY; y += blockSize) {
                    for (int x = startX; x < endX; x += blockSize) {
                        int bEndX = std::min(x + blockSize, endX);
                        int bEndY = std::min(y + blockSize, endY);
                        int offset = y * linesize + x * 4;
                        uint8_t b = data[offset];
                        uint8_t g = data[offset+1];
                        uint8_t r = data[offset+2];

                        for (int by = y; by < bEndY; by++) {
                            for (int bx = x; bx < bEndX; bx++) {
                                int bOffset = by * linesize + bx * 4;
                                data[bOffset] = b;
                                data[bOffset+1] = g;
                                data[bOffset+2] = r;
                            }
                        }
                    }
                }
            }

            // 2. 涂鸦
            for (const auto& p : strokes) {
                for (int dy = -2; dy <= 2; dy++) {
                    for (int dx = -2; dx <= 2; dx++) {
                        int px = p.x + dx;
                        int py = p.y + dy;
                        if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                            int offset = py * linesize + px * 4;
                            data[offset] = 0;     // B
                            data[offset+1] = 0;   // G
                            data[offset+2] = 255; // R
                        }
                    }
                }
            }
        } // END processFrame

        void handleInput() {
            if (!is_recording || is_paused) return;
            POINT pt;
            GetCursorPos(&pt);
            int mx = pt.x;
            int my = pt.y;
            bool lbtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

            std::lock_guard<std::mutex> lock(draw_mutex);
            if (lbtn && paint_mode) {
                strokes.push_back({mx, my});
            }
            // 为了安全，暂时移除复杂的拖拽逻辑，只保留点按涂鸦
        } // END handleInput

        void captureFrame() {
            if (!is_recording || is_paused) return;

            handleInput();

            DXGI_OUTDUPL_FRAME_INFO frame_info;
            ComPtr<IDXGIResource> desktop_resource;
            HRESULT hr = dxgi_duplication->AcquireNextFrame(0, &frame_info, &desktop_resource);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) return;
            if (FAILED(hr)) return;

            ComPtr<ID3D11Texture2D> gpu_texture;
            desktop_resource.As(&gpu_texture);
            if (!staging_texture) {
                D3D11_TEXTURE2D_DESC desc;
                gpu_texture->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                d3d_device->CreateTexture2D(&desc, nullptr, &staging_texture);
            }

            d3d_context->CopyResource(staging_texture.Get(), gpu_texture.Get());
            dxgi_duplication->ReleaseFrame();

            D3D11_MAPPED_SUBRESOURCE map;
            d3d_context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &map);

            processFrame((uint8_t*)map.pData, map.RowPitch);

            const uint8_t* src_slices[1] = { (uint8_t*)map.pData };
            const int src_strides[1] = { (int)map.RowPitch };
            
            av_frame_make_writable(raw_frame);
            sws_scale(sws_ctx, src_slices, src_strides, 0, screen_height,
                      raw_frame->data, raw_frame->linesize);

            d3d_context->Unmap(staging_texture.Get(), 0);

            raw_frame->pts = video_pts++;
            avcodec_send_frame(video_ctx, raw_frame);

            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(video_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, video_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);

            if (audio_ctx && audio_frame) {
                av_frame_make_writable(audio_frame);
                audio_frame->pts = audio_pts;
                audio_pts += audio_frame->nb_samples;
                avcodec_send_frame(audio_ctx, audio_frame);
                while (avcodec_receive_packet(audio_ctx, pkt) == 0) {
                    av_packet_rescale_ts(pkt, audio_ctx->time_base, audio_stream->time_base);
                    pkt->stream_index = audio_stream->index;
                    av_interleaved_write_frame(fmt_ctx, pkt);
                    av_packet_unref(pkt);
                }
            }
        } // END captureFrame

        void stopRecording() {
            if (!is_recording) return;
            avcodec_send_frame(video_ctx, nullptr);
            if (audio_ctx) avcodec_send_frame(audio_ctx, nullptr);
            av_write_trailer(fmt_ctx);
            if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
            avcodec_free_context(&video_ctx);
            if (audio_ctx) avcodec_free_context(&audio_ctx);
            avformat_free_context(fmt_ctx);
            av_frame_free(&raw_frame);
            av_frame_free(&audio_frame);
            sws_freeContext(sws_ctx);
            is_recording = false;
        }

        double getDuration() const {
            if (!is_recording) return 0.0;
            auto now = is_paused ? pause_start_time : std::chrono::steady_clock::now();
            std::chrono::duration<double> diff = now - start_time - total_pause_duration;
            return diff.count();
        }

        bool isRecording() const { return is_recording; }
        bool isPaused() const { return is_paused; }
        void cleanup() { staging_texture.Reset(); }
    };
} // END namespace
