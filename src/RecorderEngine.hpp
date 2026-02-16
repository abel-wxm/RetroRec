// ==========================================
// VERSION: 2026-02-16_22-59_FIX_FINAL
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
            avcodec_parameters_from_context(video_stream->codecpar, video_ctx);
            video_stream->time_base = video_ctx->time_base;

            const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
            audio_stream = avformat_new_stream(fmt_ctx, ac);
            audio_ctx = avcodec_alloc_context3(ac);
            audio_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; audio_ctx->bit_rate = 128000; audio_ctx->sample_rate = 48000;
#if LIBAVCODEC_VERSION_MAJOR >= 60
            AVChannelLayout cl; av_channel_layout_default(&cl, 2); audio_ctx->ch_layout = cl;
#else
            audio_ctx->channels = 2; audio_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
#endif
            if (avcodec_open2(audio_ctx, ac, nullptr) >= 0) {
                avcodec_parameters_from_context(audio_stream->codecpar, audio_ctx);
                audio_stream->time_base = {1, 48000};
                audio_frame = av_frame_alloc(); audio_frame->nb_samples = audio_ctx->frame_size; audio_frame->format = audio_ctx->sample_fmt;
#if LIBAVCODEC_VERSION_MAJOR >= 60
                av_channel_layout_copy(&audio_frame->ch_layout, &audio_ctx->ch_layout);
#else
                audio_frame->channels = 2; audio_frame->channel_layout = AV_CH_LAYOUT_STEREO;
#endif
                av_frame_get_buffer(audio_frame, 0);
            }
            if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_open(&fmt_ctx->pb, fn, AVIO_FLAG_WRITE);
            avformat_write_header(fmt_ctx, nullptr);
            sws_ctx = sws_getContext(screen_width, screen_height, AV_PIX_FMT_BGRA, screen_width, screen_height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
            raw_frame = av_frame_alloc(); raw_frame->format = video_ctx->pix_fmt; raw_frame->width = screen_width; raw_frame->height = screen_height; av_frame_get_buffer(raw_frame, 32);
            video_pts = 0; audio_samples_written = 0; is_recording = true; is_paused = false;
            start_time = std::chrono::steady_clock::now() - std::chrono::seconds(3);
            total_pause_duration = std::chrono::duration<double>(0);
            return true;
        }

        void pauseRecording() { if (is_recording && !is_paused) { is_paused = true; pause_start_time = std::chrono::steady_clock::now(); } }
        void resumeRecording() { if (is_recording && is_paused) { is_paused = false; total_pause_duration += (std::chrono::steady_clock::now() - pause_start_time); } }

        void encodeAndWrite(const RawFrame& rf) {
            uint8_t* src[] = { (uint8_t*)rf.data.data() }; int strd[] = { screen_width * 4 };
            av_frame_make_writable(raw_frame); sws_scale(sws_ctx, src, strd, 0, screen_height, raw_frame->data, raw_frame->linesize);
            raw_frame->pts = rf.capture_time_ms * 30 / 1000; video_pts = raw_frame->pts;
            avcodec_send_frame(video_ctx, raw_frame); AVPacket* p = av_packet_alloc();
            while (avcodec_receive_packet(video_ctx, p) == 0) { av_packet_rescale_ts(p, video_ctx->time_base, video_stream->time_base); p->stream_index = video_stream->index; av_interleaved_write_frame(fmt_ctx, p); av_packet_unref(p); }
            av_packet_free(&p);
        }

        void captureFrame() {
            DXGI_OUTDUPL_FRAME_INFO fi; ComPtr<IDXGIResource> res;
            if (dxgi_duplication->AcquireNextFrame(0, &fi, &res) == DXGI_ERROR_WAIT_TIMEOUT) return;
            ComPtr<ID3D11Texture2D> tex; res.As(&tex);
            if (!staging_texture) { D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d); d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.BindFlags = 0; d.MiscFlags = 0; d3d_device->CreateTexture2D(&d, nullptr, &staging_texture); }
            d3d_context->CopyResource(staging_texture.Get(), tex.Get()); dxgi_duplication->ReleaseFrame();
            D3D11_MAPPED_SUBRESOURCE map; d3d_context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &map);
            RawFrame rf; rf.data.resize(screen_width * screen_height * 4);
            if (map.RowPitch == screen_width * 4) memcpy(rf.data.data(), map.pData, rf.data.size());
            else for (int y=0; y<screen_height; y++) memcpy(rf.data.data() + y*screen_width*4, (uint8_t*)map.pData + y*map.RowPitch, screen_width*4);
            d3d_context->Unmap(staging_texture.Get(), 0);
            auto now = std::chrono::steady_clock::now();
            if (is_recording) { if (is_paused) return; rf.capture_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time - total_pause_duration).count(); } else rf.capture_time_ms = 0;
            std::lock_guard<std::mutex> dl(draw_mutex); int ls = screen_width * 4;
            for (const auto& p : strokes) if (p.x>=0 && p.x<screen_width && p.y>=0 && p.y<screen_height) { rf.data[p.y*ls+p.x*4]=0; rf.data[p.y*ls+p.x*4+1]=0; rf.data[p.y*ls+p.x*4+2]=255; }
            for (const auto& r : mosaic_zones) {
                for (int y=r.y; y<r.y+r.h; y+=15) for (int x=r.x; x<r.x+r.w; x+=15) {
                    if (y>=screen_height || x>=screen_width) continue;
                    uint8_t b=rf.data[y*ls+x*4], g=rf.data[y*ls+x*4+1], rv=rf.data[y*ls+x*4+2];
                    for (int by=y; by<(std::min)(y+15, r.y+r.h); by++) for (int bx=x; bx<(std::min)(x+15, r.x+r.w); bx++)
                        if (by<screen_height && bx<screen_width) { rf.data[by*ls+bx*4]=b; rf.data[by*ls+bx*4+1]=g; rf.data[by*ls+bx*4+2]=rv; }
                }
            }
            { std::lock_guard<std::mutex> lock(buffer_mutex); video_buffer.push_back(rf); if (video_buffer.size() > BUFFER_FRAMES) { if (is_recording && !is_paused) encodeAndWrite(video_buffer.front()); video_buffer.pop_front(); } }
            if (is_recording && !is_paused && audio_enabled && audio_ctx) {
                std::vector<uint8_t> ab; audio_cap.read(ab);
                if (audio_frame) {
                    av_frame_make_writable(audio_frame); audio_frame->pts = audio_samples_written; audio_samples_written += audio_frame->nb_samples;
                    int nch = 2;
#if LIBAVCODEC_VERSION_MAJOR >= 60
                    nch = audio_ctx->ch_layout.nb_channels; 
#else
                    nch = audio_ctx->channels;
#endif
                    for (int i=0; i<audio_frame->nb_samples; i++) for (int c=0; c<nch; c++) ((float*)audio_frame->data[c])[i] = 0.0f;
                    avcodec_send_frame(audio_ctx, audio_frame); AVPacket* ap = av_packet_alloc();
                    while (avcodec_receive_packet(audio_ctx, ap) == 0) { av_packet_rescale_ts(ap, audio_ctx->time_base, audio_stream->time_base); ap->stream_index = audio_stream->index; av_interleaved_write_frame(fmt_ctx, ap); av_packet_unref(ap); }
                    av_packet_free(&ap);
                }
            }
        }
        void stopRecording() { if (!is_recording) return; { std::lock_guard<std::mutex> l(buffer_mutex); while (!video_buffer.empty()) { encodeAndWrite(video_buffer.front()); video_buffer.pop_front(); } } av_write_trailer(fmt_ctx); if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt_ctx->pb); avcodec_free_context(&video_ctx); avcodec_free_context(&audio_ctx); avformat_free_context(fmt_ctx); sws_freeContext(sws_ctx); is_recording = false; }
        bool isRecording() { return is_recording; }
        bool isPaused() { return is_paused; }
    };
}
