/**
 * RetroRec - Ring Buffer Implementation (The "Time Machine")
 * * ARCHITECTURE NOTE (v1.0 Intent):
 * This class manages the circular memory buffer. 
 * It is the ONLY place where "Past" data exists and can be modified before being written to disk.
 * * * Thread Safety:
 * CRITICAL. The Producer (Capture), Consumer (Writer), and Editor (Retro-Repair) 
 * all access this simultaneously. std::mutex lock is mandatory.
 */

#pragma once

#include <vector>
#include <mutex>
#include <memory>
#include <deque>
#include <algorithm>
#include <iostream>

namespace RetroRec::Core {

    // A single video frame with metadata
    struct Frame {
        int64_t Timestamp;      // Microseconds (for Audio Sync)
        int Width, Height;
        std::vector<uint8_t> Data; // Raw Pixel Data (BGRA)
        bool IsKeyFrame;        // For video encoding optimization
    };

    class RingBuffer {
    private:
        std::deque<std::shared_ptr<Frame>> m_Buffer; // using shared_ptr to avoid heavy copying
        const size_t m_MaxFrames;                    // Capacity (e.g., 30fps * 3s = 90 frames)
        mutable std::mutex m_Mutex;                  // Guards the buffer

    public:
        // Constructor: Define how many seconds of history we keep
        RingBuffer(int fps, int secondsToKeep) 
            : m_MaxFrames(fps * secondsToKeep) {}

        // Producer calls this: Push a new frame
        void Push(std::shared_ptr<Frame> frame) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            
            m_Buffer.push_back(frame);
            
            // If full, drop the oldest frame (The "Ring" behavior)
            if (m_Buffer.size() > m_MaxFrames) {
                m_Buffer.pop_front();
            }
        }

        // Consumer calls this: Get a snapshot of current buffer to write to disk
        // Note: For the "Chunked Recording" architecture, we might verify contiguous timestamps here.
        std::vector<std::shared_ptr<Frame>> GetSnapshot() {
            std::lock_guard<std::mutex> lock(m_Mutex);
            // Return a copy of the list (pointers are cheap to copy)
            return std::vector<std::shared_ptr<Frame>>(m_Buffer.begin(), m_Buffer.end());
        }

        /**
         * The "Retroactive" Magic Function
         * @param durationMs: How far back to go (e.g., 3000ms)
         * @param x, y, w, h: The region to blur
         * @param processor: A callback function (lambda) to apply the blur effect
         */
        template <typename Func>
        void ApplyRetroactiveMask(int durationMs, int x, int y, int w, int h, Func pixelProcessor) {
            std::lock_guard<std::mutex> lock(m_Mutex);

            if (m_Buffer.empty()) return;

            // 1. Calculate the time threshold
            int64_t currentTime = m_Buffer.back()->Timestamp;
            int64_t targetTime = currentTime - (durationMs * 1000); // Convert to microseconds

            std::cout << "[RingBuffer] Rewinding time... Processing frames since timestamp " << targetTime << std::endl;

            // 2. Iterate BACKWARDS from the newest frame
            for (auto it = m_Buffer.rbegin(); it != m_Buffer.rend(); ++it) {
                auto& frame = *it;

                if (frame->Timestamp < targetTime) {
                    break; // We have gone back far enough
                }

                // 3. Apply the processing (Blurring) directly to memory
                // The 'pixelProcessor' is a dependency-injected function (e.g., OpenCV logic)
                // This keeps RingBuffer clean of OpenCV headers.
                pixelProcessor(frame->Data, frame->Width, frame->Height, x, y, w, h);
            }
        }
    };
}
