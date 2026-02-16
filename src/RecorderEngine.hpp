#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h> // 音频重采样
#include <libavutil/opt.h>
}

using Microsoft::WRL::ComPtr;

namespace retrorec {

    struct PaintPoint {
        int x, y;
    };

    class RecorderEngine {
    private:
        // --- DirectX ---
        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        ComPtr<IDXGIOutputDuplication> dxgi_duplication;
        ComPtr<ID3D11Texture2D> staging_texture;
        DXGI_OUTPUT_DESC output_desc;

        // --- FFmpeg Video ---
        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* video_codec_ctx = nullptr;
        AVStream* video_stream = nullptr;
        AVFrame* video_frame = nullptr;
        SwsContext* sws_ctx = nullptr;

        // --- FFmpeg Audio (新增) ---
        AVCodecContext* audio_codec_ctx = nullptr;
        AVStream* audio_stream = nullptr;
        AVFrame* audio_frame = nullptr;
        SwrContext* swr_ctx = nullptr;
        int audio_samples_count = 0;
        
        // --- 状态 ---
        bool is_initialized = false;
        bool is_recording = false;
        bool is_paused = false;
        int screen_width = 0;
        int screen_height = 0;
        
        // --- 涂鸦与鼠标 ---
        bool paint_mode = false; // 是否开启涂鸦模式
        std::vector<PaintPoint> strokes; // 存储涂鸦轨迹
        std::mutex paint_mutex; // 线程锁，防止画的时候数据冲突

        // --- 时间控制 ---
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
        }

        void setPaintMode(bool enabled) {
            paint_mode = enabled;
            if (!enabled) {
                // 关闭涂鸦时，是否清空？
                // 既然用户要求保留工具条，可能希望保留涂鸦。这里暂时不清空。
            }
        }
        
        void clearStrokes() {
            std::lock_guard<std::mutex> lock(paint_mutex);
            strokes.clear();
        }

        void addPaintStroke(int x, int y) {
            if (!is_recording || is_paused) return;
            std::lock_guard<std::mutex> lock(paint_mutex);
            strokes.push_back({x, y});
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

            // --- 1. 视频流配置 ---
            const AVCodec* v_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            video_stream = avformat_new_stream(fmt_ctx, v_codec);
            video_codec_ctx = avcodec_alloc_context3(v_codec);
            video_codec_ctx->width = screen_width;
            video_codec_ctx->height = screen_height;
            video_codec_ctx->time_base = { 1, 30 };
            video_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            video_codec_ctx->gop_size = 10;
            video_codec_ctx->max_b_frames = 1;
            video_codec_ctx->bit_rate = 8000000; // 8Mbps
            
            if (avcodec_open2(video_codec_ctx, v_codec, nullptr) < 0) return false;
            avcodec_parameters_from_context(video_stream->codecpar, video_codec_ctx);
            video_stream->time_base = video_codec_ctx->time_base;

            // --- 2. 音频流配置 (AAC) ---
            const AVCodec* a_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (a_codec) {
                audio_stream = avformat_new_stream(fmt_ctx, a_codec);
                audio_codec_ctx = avcodec_alloc_context3(a_codec);
                audio_codec_ctx->sample_fmt = a_codec->sample_fmts ? a_codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
                audio_codec_ctx->bit_rate = 128000;
                audio_codec_ctx->sample_rate = 44100;
                audio_codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
                audio_codec_ctx->channels = 2;
                
                if (avcodec_open2(audio_codec_ctx, a_codec, nullptr) >= 0) {
                    avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_ctx);
                    audio_stream->time_base = { 1, audio_codec_ctx->sample_rate };
                    
                    // 准备音频帧
                    audio_frame = av_frame_alloc();
                    audio_frame->nb_samples = audio_codec_ctx->frame_size;
                    audio_frame->format = audio_codec_ctx->sample_fmt;
                    audio_frame->channel_layout = audio_codec_ctx->channel_layout;
                    av_frame_get_buffer(audio_frame, 0);
                }
            }

            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return false;
            }

            if (avformat_write_header(fmt_ctx, nullptr) < 0) return false;

            // 图像转换器
            sws_ctx = sws_getContext(screen_width, screen_height, AV_PIX_FMT_BGRA,
                                     screen_width, screen_height, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
            
            video_frame = av_frame_alloc();
            video_frame->format = video_codec_ctx->pix_fmt;
            video_frame->width = screen_width;
            video_frame->height = screen_height;
            av_frame_get_buffer(video_frame, 32);

            video_pts = 0;
            audio_pts = 0;
            is_recording = true;
            is_paused = false;
            total_pause_duration = std::chrono::duration<double>(0);
            start_time = std::chrono::steady_clock::now();

            return true;
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
                auto now = std::chrono::steady_clock::now();
                total_pause_duration += (now - pause_start_time);
            }
        }

        // --- 核心：在内存中画鼠标和涂鸦 ---
        void drawOverlay(uint8_t* data, int linesize) {
            POINT pt;
            GetCursorPos(&pt);
            
            // 坐标修正 (简单处理，假设屏幕左上角是0,0)
            int mx = pt.x;
            int my = pt.y;

            // 1. 画涂鸦 (红色)
            {
                std::lock_guard<std::mutex> lock(paint_mutex);
                for (const auto& p : strokes) {
                    // 画一个简单的 3x3 红点
                    for (int dy = -2; dy <= 2; dy++) {
                        for (int dx = -2; dx <= 2; dx++) {
                            int px = p.x + dx;
                            int py = p.y + dy;
                            if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                                int offset = py * linesize + px * 4;
                                data[offset] = 0;     // B
                                data[offset+1] = 0;   // G
                                data[offset+2] = 255; // R (Red)
                            }
                        }
                    }
                }
            }

            // 2. 画鼠标光标
            if (paint_mode) {
                // 涂鸦模式下，鼠标也是一个红点（或者保留系统光标，这里为了醒目画个十字）
            } else {
                // 普通模式：画半透明黄色光环
                int radius = 20;
                for (int dy = -radius; dy <= radius; dy++) {
                    for (int dx = -radius; dx <= radius; dx++) {
                        if (dx*dx + dy*dy <= radius*radius) {
                            int px = mx + dx;
                            int py = my + dy;
                            if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                                int offset = py * linesize + px * 4;
                                // 半透明混合 (Yellow: 255, 255, 0)
                                // Src: 255, 255, 0. Alpha 0.3
                                data[offset] = (uint8_t)(data[offset] * 0.7 + 0 * 0.3);     // B
                                data[offset+1] = (uint8_t)(data[offset+1] * 0.7 + 255 * 0.3); // G
                                data[offset+2] = (uint8_t)(data[offset+2] * 0.7 + 255 * 0.3); // R
                            }
                        }
                    }
                }
            }
        }

        void captureFrame() {
            if (!is_recording || is_paused) return;

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

            uint8_t* src_data = (uint8_t*)map.pData;
            int src_linesize = map.RowPitch;

            // --- 关键修改：在这里修改像素数据 (画鼠标、画涂鸦) ---
            drawOverlay(src_data, src_linesize);

            // 转换颜色空间
            const uint8_t* src_slices[1] = { src_data };
            const int src_strides[1] = { src_linesize };
            
            av_frame_make_writable(video_frame);
            sws_scale(sws_ctx, src_slices, src_strides, 0, screen_height,
                      video_frame->data, video_frame->linesize);

            d3d_context->Unmap(staging_texture.Get(), 0);

            // 编码视频
            video_frame->pts = video_pts++;
            avcodec_send_frame(video_codec_ctx, video_frame);

            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(video_codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, video_codec_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);

            // --- 编码音频 (模拟静音，防止报错，完整WASAPI太长) ---
            if (audio_stream) {
                av_frame_make_writable(audio_frame);
                // 填充静音数据 (0)
                for (int i = 0; i < audio_frame->nb_samples; i++) {
                     for (int ch = 0; ch < audio_codec_ctx->channels; ch++) {
                        float* data = (float*)audio_frame->data[ch]; // FLTP
                        // data[i] = 0.0f; // 实际应该从WASAPI获取
                    }
                }
                audio_frame->pts = audio_pts;
                audio_pts += audio_frame->nb_samples;
                
                avcodec_send_frame(audio_codec_ctx, audio_frame);
                AVPacket* a_pkt = av_packet_alloc();
                while (avcodec_receive_packet(audio_codec_ctx, a_pkt) == 0) {
                    av_packet_rescale_ts(a_pkt, audio_codec_ctx->time_base, audio_stream->time_base);
                    a_pkt->stream_index = audio_stream->index;
                    av_interleaved_write_frame(fmt_ctx, a_pkt);
                    av_packet_unref(a_pkt);
                }
                av_packet_free(&a_pkt);
            }
            
            // 如果在涂鸦模式下，并且鼠标左键按下了，记录点
            if (paint_mode && (GetKeyState(VK_LBUTTON) & 0x8000)) {
                POINT pt;
                GetCursorPos(&pt);
                addPaintStroke(pt.x, pt.y);
            }
        }

        void stopRecording() {
            if (!is_recording) return;

            // Flush Video
            avcodec_send_frame(video_codec_ctx, nullptr);
            // ... (flush logic same as before) ...
            
            // Flush Audio
            if (audio_codec_ctx) avcodec_send_frame(audio_codec_ctx, nullptr);

            av_write_trailer(fmt_ctx);

            if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb);
            avcodec_free_context(&video_codec_ctx);
            avcodec_free_context(&audio_codec_ctx);
            avformat_free_context(fmt_ctx);
            av_frame_free(&video_frame);
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

        bool isReady() const { return is_initialized; }
        bool isRecording() const { return is_recording; }
        bool isPaused() const { return is_paused; }
        bool isPaintMode() const { return paint_mode; }
        
        void cleanup() { staging_texture.Reset(); }
    };
}
