#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>

// FFmpeg 必须用 extern "C" 包裹
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
        // --- DirectX 资源 (用于抓屏) ---
        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        ComPtr<IDXGIOutputDuplication> dxgi_duplication;
        ComPtr<ID3D11Texture2D> staging_texture; // CPU 可读的纹理
        DXGI_OUTPUT_DESC output_desc;

        // --- FFmpeg 资源 (用于编码) ---
        AVFormatContext* fmt_ctx = nullptr;
        AVCodecContext* codec_ctx = nullptr;
        AVStream* video_stream = nullptr;
        AVFrame* raw_frame = nullptr; // YUV 数据容器
        SwsContext* sws_ctx = nullptr; // 图像转换器
        int64_t frame_count = 0;

        // --- 状态控制 ---
        bool is_initialized = false;
        bool is_recording = false;
        int screen_width = 0;
        int screen_height = 0;

    public:
        RecorderEngine() = default;
        ~RecorderEngine() { stopRecording(); cleanup(); }

        // 1. 初始化显卡 (只做一次)
        bool initialize() {
            if (is_initialized) return true;
            HRESULT hr;

            // 创建 D3D 设备
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context);
            if (FAILED(hr)) return false;

            // 获取 DXGI 接口
            ComPtr<IDXGIDevice> dxgi_device;
            d3d_device.As(&dxgi_device);
            ComPtr<IDXGIAdapter> dxgi_adapter;
            dxgi_device->GetAdapter(&dxgi_adapter);
            ComPtr<IDXGIOutput> dxgi_output;
            dxgi_adapter->EnumOutputs(0, &dxgi_output);

            // 创建桌面副本 (核心步骤)
            ComPtr<IDXGIOutput1> dxgi_output1;
            hr = dxgi_output.As(&dxgi_output1);
            hr = dxgi_output1->DuplicateOutput(d3d_device.Get(), &dxgi_duplication);
            
            if (FAILED(hr)) return false; // 通常是因为没有显示器或权限不足

            dxgi_output->GetDesc(&output_desc);
            screen_width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
            screen_height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
            
            // 确保宽高是 2 的倍数 (FFmpeg 要求)
            if (screen_width % 2 != 0) screen_width--;
            if (screen_height % 2 != 0) screen_height--;

            is_initialized = true;
            return true;
        }

        // 2. 开始录制
        bool startRecording(const std::string& filename) {
            if (!is_initialized) return false;
            if (is_recording) return false;

            // --- 配置 FFmpeg ---
            avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, filename.c_str());
            const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            video_stream = avformat_new_stream(fmt_ctx, codec);
            
            codec_ctx = avcodec_alloc_context3(codec);
            codec_ctx->width = screen_width;
            codec_ctx->height = screen_height;
            codec_ctx->time_base = { 1, 30 }; // 30 FPS
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            codec_ctx->gop_size = 10;
            codec_ctx->max_b_frames = 1;
            
            // 打开编码器
            if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return false;
            
            avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);
            video_stream->time_base = codec_ctx->time_base;

            // 打开文件
            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&fmt_ctx->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) return false;
            }

            // 写文件头
            if (avformat_write_header(fmt_ctx, nullptr) < 0) return false;

            // 准备图像转换器 (BGRA -> YUV420P)
            sws_ctx = sws_getContext(screen_width, screen_height, AV_PIX_FMT_BGRA,
                                     screen_width, screen_height, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
            
            // 准备帧容器
            raw_frame = av_frame_alloc();
            raw_frame->format = codec_ctx->pix_fmt;
            raw_frame->width = screen_width;
            raw_frame->height = screen_height;
            av_frame_get_buffer(raw_frame, 32);

            frame_count = 0;
            is_recording = true;
            return true;
        }

        // 3. 抓取并编码一帧 (核心循环)
        void captureFrame() {
            if (!is_recording) return;

            DXGI_OUTDUPL_FRAME_INFO frame_info;
            ComPtr<IDXGIResource> desktop_resource;
            HRESULT hr = dxgi_duplication->AcquireNextFrame(0, &frame_info, &desktop_resource);

            if (hr == DXGI_ERROR_WAIT_TIMEOUT) return; // 没新画面，跳过
            if (FAILED(hr)) return;

            // 获取 GPU 纹理
            ComPtr<ID3D11Texture2D> gpu_texture;
            desktop_resource.As(&gpu_texture);

            // 如果暂存纹理未创建，则创建
            if (!staging_texture) {
                D3D11_TEXTURE2D_DESC desc;
                gpu_texture->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING; // 允许 CPU 读取
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                d3d_device->CreateTexture2D(&desc, nullptr, &staging_texture);
            }

            // 显卡 -> 内存拷贝
            d3d_context->CopyResource(staging_texture.Get(), gpu_texture.Get());
            dxgi_duplication->ReleaseFrame(); // 释放，让显卡去抓下一帧

            // 映射内存，读取像素
            D3D11_MAPPED_SUBRESOURCE map;
            d3d_context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &map);

            // 格式转换 + 编码
            uint8_t* src_data[1] = { (uint8_t*)map.pData };
            int src_linesize[1] = { (int)map.RowPitch };
            
            av_frame_make_writable(raw_frame);
            sws_scale(sws_ctx, src_data, src_linesize, 0, screen_height,
                      raw_frame->data, raw_frame->linesize);

            d3d_context->Unmap(staging_texture.Get(), 0);

            // 发送给编码器
            raw_frame->pts = frame_count++;
            avcodec_send_frame(codec_ctx, raw_frame);

            // 接收编码后的包并写入文件
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                pkt->stream_index = video_stream->index;
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        // 4. 停止录制
        void stopRecording() {
            if (!is_recording) return;

            // 冲刷编码器剩余数据
            avcodec_send_frame(codec_ctx, nullptr);
            AVPacket* pkt = av_packet_alloc();
            while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
                av_packet_rescale_ts(pkt, codec_ctx->time_base, video_stream->time_base);
                av_interleaved_write_frame(fmt_ctx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);

            // 写文件尾
            av_write_trailer(fmt_ctx);

            // 清理 FFmpeg 资源
            if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&fmt_ctx->pb);
            avcodec_free_context(&codec_ctx);
            avformat_free_context(fmt_ctx);
            av_frame_free(&raw_frame);
            sws_freeContext(sws_ctx);

            is_recording = false;
        }

        void cleanup() {
            staging_texture.Reset();
        }

        bool isReady() const { return is_initialized; }
        bool isRecording() const { return is_recording; }
    };
}
