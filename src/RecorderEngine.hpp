// ==========================================
// VERSION: 2026-02-16_14-45 (Fix Build)
// ==========================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <mutex>
#include <algorithm> // 必须包含这个

// FFmpeg 必须用 extern "C"
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
        AVFrame* raw_frame = nullptr;
        SwsContext* sws_ctx = nullptr;

        // 音频占位 (防止空指针)
        AVCodecContext* audio_ctx = nullptr;
        AVStream* audio_stream = nullptr;
        AVFrame* audio_frame = nullptr;

        bool is_initialized = false;
        bool is_recording = false;
        bool is_paused = false;
        
        // 交互状态
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
        }

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
            
            // 无损配置
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
                audio_
