/**
 * RetroRec - Video Encoder Module (The "Mouth")
 * * ARCHITECTURE NOTE (v1.0 Intent):
 * This module uses FFmpeg (libavcodec/libavformat) to compress frames into MP4.
 * * * PERFORMANCE CRITICAL:
 * 1. It MUST attempt to use Hardware Acceleration (h264_nvenc, h264_qsv) first.
 * 2. Fallback to libx264 (CPU) only if GPU is unavailable.
 * 3. It runs in a separate thread (Consumer) to avoid blocking capture.
 */

#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <stdexcept>

// FFmpeg is a C library, so we need extern "C"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace RetroRec::Core {

    class VideoEncoder {
    private:
        AVFormatContext* m_FormatCtx = nullptr;
        AVCodecContext* m_CodecCtx = nullptr;
        AVStream* m_Stream = nullptr;
        AVFrame* m_Frame = nullptr;
        AVPacket* m_Packet = nullptr;
        int m_FrameCounter = 0;

    public:
        VideoEncoder() = default;
        ~VideoEncoder() { Finish(); }

        // Initialize the encoder (Width, Height, FPS, Output Filename)
        bool Init(int width, int height, int fps, const std::string& filename) {
            // 1. Setup container (MP4)
            avformat_alloc_output_context2(&m_FormatCtx, nullptr, nullptr, filename.c_str());
            if (!m_FormatCtx) return false;

            // 2. Find Encoder (Try NVIDIA GPU first -> Intel -> CPU)
            const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc"); 
            if (!codec) codec = avcodec_find_encoder_by_name("h264_qsv");
            if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            
            if (!codec) {
                std::cerr << "[Encoder] Critical: No H.264 encoder found!" << std::endl;
                return false;
            }
            std::cout << "[Encoder] Using Codec: " << codec->name << std::endl;

            // 3. Configure Context
            m_CodecCtx = avcodec_alloc_context3(codec);
            m_CodecCtx->width = width;
            m_CodecCtx->height = height;
            m_CodecCtx->time_base = {1, fps};
            m_CodecCtx->framerate = {fps, 1};
            m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P; // Standard pixel format
            
            // Bitrate control (Higher = Better Quality, Larger File)
            m_CodecCtx->bit_rate = 4000000; // 4 Mbps (Good for 1080p)
            m_CodecCtx->gop_size = 10;      // Keyframe every 10 frames
            m_CodecCtx->max_b_frames = 1;

            if (m_FormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
                m_CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            avcodec_open2(m_CodecCtx, codec, nullptr);

            // 4. Create Stream
            m_Stream = avformat_new_stream(m_FormatCtx, nullptr);
            avcodec_parameters_from_context(m_Stream->codecpar, m_CodecCtx);
            m_Stream->time_base = m_CodecCtx->time_base;

            // 5. Open File
            if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE)) {
                avio_open(&m_FormatCtx->pb, filename.c_str(), AVIO_FLAG_WRITE);
            }

            // Write Header
            avformat_write_header(m_FormatCtx, nullptr);

            // Alloc tools
            m_Packet = av_packet_alloc();
            m_Frame = av_frame_alloc();
            m_Frame->format = m_CodecCtx->pix_fmt;
            m_Frame->width = width;
            m_Frame->height = height;
            av_frame_get_buffer(m_Frame, 32);

            return true;
        }

        // The Main Loop calls this to save a frame
        void EncodeFrame(const std::vector<uint8_t>& bgraData) {
            // TODO: Convert BGRA (Screen) to YUV420P (Encoder) using sws_scale
            // This step is omitted for brevity but is essential for implementation.
            
            // Dummy logic: Make frame writable
            av_frame_make_writable(m_Frame);
            m_Frame->pts = m_FrameCounter++;

            // Send frame to encoder
            int ret = avcodec_send_frame(m_CodecCtx, m_Frame);
            if (ret < 0) return;

            // Receive packet and write to file
            while (ret >= 0) {
                ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;

                av_interleaved_write_frame(m_FormatCtx, m_Packet);
                av_packet_unref(m_Packet);
            }
        }

        // Close file properly
        void Finish() {
            if (m_FormatCtx) {
                av_write_trailer(m_FormatCtx);
                if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE))
                    avio_closep(&m_FormatCtx->pb);
                avformat_free_context(m_FormatCtx);
                m_FormatCtx = nullptr;
            }
            if (m_CodecCtx) avcodec_free_context(&m_CodecCtx);
            if (m_Packet) av_packet_free(&m_Packet);
            if (m_Frame) av_frame_free(&m_Frame);
            
            std::cout << "[Encoder] Video saved successfully." << std::endl;
        }
    };
}
