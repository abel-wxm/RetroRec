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

// FFmpeg 必须用 extern "C"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

using Microsoft::WRL::ComPtr;

namespace retrorec {

    class RecorderEngine {
    private:
        // --- DirectX ---
        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        ComPtr<IDXGIOutputDuplication> dxgi_duplication;
        ComPtr<ID3D11Texture2D> staging_texture;
        DXGI_OUTPUT_DESC output_desc;

        // --- FFmpeg ---
        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        AVStream* video_stream = nullptr;
        AVFrame* raw_frame = nullptr;
        SwsContext* sws_ctx = nullptr;
        
        // --- 状态 ---
        bool is_initialized = false;
        bool is_recording = false;
        bool is_paused = false;     // 新增：暂停状态
        int screen_width = 0;
        int screen_height = 0;
        
        // --- 时间控制 ---
        int64_t frame_count = 0;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point pause_start_time;
        std::chrono::duration<double> total_pause_duration; // 记录总共暂停了多久

    public:
        RecorderEngine() : total_pause_duration(0) {}
        ~RecorderEngine() { stopRecording(); cleanup(); }

        bool initialize() {
            if (is_initialized) return true;
            HRESULT hr;

            // 1. D3D Device
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context);
            if (FAILED(hr)) return false;

            // 2. DXGI Output
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
            
            // 宽高偶数修正
            if (screen_width % 2 != 0) screen_width--;
            if (screen_height % 2 != 0) screen_height--;

            is_initialized = true;
            return true;
        }

        // 修改：不需要传文件名，内部自动生成
        bool startRecording() {
            if (!is_initialized) return false;
            if (is_recording) return false;

            // 1. 自动生成时间戳文件名 (Rec_20260215_123000.mp4)
            time_t now = time(0);
            tm ltm;
            localtime_s(&ltm, &now);
            char time_buffer[64];
            strftime(time_buffer, 64, "Rec_%Y%m%d_%H%M%S.mp4", &ltm);
            std::string filename = std::string(time_buffer);

            // 2. FFmpeg 初始化
            avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename.c_str());
            const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            video_stream = avformat_new_stream(fmt_ctx, codec);
            
            codec_ctx = avcodec_alloc_context3(codec);
            codec_ctx->width = screen_width;
            codec_ctx->height = screen_height;
            codec_ctx->time_base = { 1, 30 };
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            codec_ctx->gop_size = 10;
            codec_ctx->max_b_frames = 1;
            
            // **核心修改：8Mbps 高码率 (解决模糊)**
            codec_ctx->bit_rate = 8000000; 
            
            if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return false;
            
            avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);
            video_stream->time_base = codec_ctx->time_base;

            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return false;
            }

            if (avformat_write_header(fmt_ctx, nullptr) < 0) return false;

            // 3. 图像转换 context
            sws_ctx = sws_getContext(screen_width, screen_height, AV_PIX_FMT_BGRA,
                                     screen_width, screen_height, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
            
            raw_frame = av_frame_alloc();
            raw_frame->format = codec_ctx->pix_fmt;
            raw_frame->width = screen_width;
            raw_frame->height = screen_height;
            av_frame_get_buffer(raw_frame, 32);

            // 重置状态
            frame_count = 0;
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

        void captureFrame() {
            if (!is_recording || is_paused) return; // 暂停时不录

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

            uint8_t* src_data[1] = { (uint8_t*)map.pData };
            int src_linesize[1] = { (int)map.RowPitch };
            
            av_frame_make_writable(raw_frame);
            sws_scale(sws_ctx, src_data, src_linesize, 0, screen_height,
                      raw_frame->data, raw_frame->linesize);

            d3d_context->Unmap(staging_texture.Get(), 0);

            raw_frame->pts = frame_count++;
            avcodec_send_frame(codec_ctx, raw_frame);

            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        void stopRecording() {
            if (!is_recording) return;

            avcodec_send_frame(codec_ctx, nullptr);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);

            av_write_trailer(fmt_ctx);

            if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&fmt_ctx->pb);
            avcodec_free_context(&codec_ctx);
            avformat_free_context(fmt_ctx);
            av_frame_free(&raw_frame);
            sws_freeContext(sws_ctx);

            is_recording = false;
            is_paused = false;
        }

        // 计算实际录制时长
        double getRecordingDuration() const {
            if (!is_recording) return 0.0;
            auto now = is_paused ? pause_start_time : std::chrono::steady_clock::now();
            std::chrono::duration<double> diff = now - start_time - total_pause_duration;
            return diff.count();
        }

        void cleanup() { staging_texture.Reset(); }
        bool isReady() const { return is_initialized; }
        bool isRecording() const { return is_recording; }
        bool isPaused() const { return is_paused; }
    };
}
