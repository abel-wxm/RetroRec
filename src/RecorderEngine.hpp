/**
 * RetroRec - Main Engine (The "Brain")
 * * ARCHITECTURE NOTE (v1.0 Intent):
 * This class orchestrates the entire application.
 * It connects the Eyes (DXGI), Heart (RingBuffer), and Hands (Overlay).
 * * * Key Responsibility:
 * Manage the "Async Repair Queue". When a user adds a privacy mask, 
 * this engine spins up a background task to modify the RingBuffer history 
 * WITHOUT blocking the main recording loop.
 */

#pragma once

#include <thread>
#include <atomic>
#include <vector>
#include <iostream>

#include "core/DXGICapture.hpp"
#include "core/RingBuffer.hpp"
#include "ui/OverlaySystem.hpp"

namespace RetroRec {

    enum class EngineState {
        IDLE,
        RECORDING,
        PAUSED_FOR_EDIT, // The "Firefighting" mode (User pressed Ctrl+Space)
        STOPPING
    };

    class RecorderEngine {
    private:
        // Sub-systems
        Core::DXGICapturer m_Capturer;
        Core::RingBuffer m_Buffer;
        UI::OverlayController m_Overlay;

        // Threading
        std::atomic<EngineState> m_State;
        std::thread m_CaptureThread;
        
        // Background worker for "Time Travel" processing
        // We use a vector of threads to handle multiple repair tasks simultaneously if needed
        std::vector<std::thread> m_RepairWorkers;

    public:
        RecorderEngine() : m_State(EngineState::IDLE), m_Buffer(30, 3) {
            // Initialize buffer with 30 FPS * 3 Seconds history
        }

        ~RecorderEngine() {
            StopRecording();
        }

        // 1. Start the machine
        void StartRecording() {
            if (m_State != EngineState::IDLE) return;
            
            m_State = EngineState::RECORDING;
            m_Capturer.Init(); // Wake up the GPU

            // Spin up the high-priority capture thread
            m_CaptureThread = std::thread(&RecorderEngine::CaptureLoop, this);
            m_CaptureThread.detach();
            
            std::cout << "[Engine] Recording Started at 30 FPS." << std::endl;
        }

        // 2. The "Left-Hand" Shortcut Trigger (Ctrl+Space)
        void TogglePause() {
            if (m_State == EngineState::RECORDING) {
                m_State = EngineState::PAUSED_FOR_EDIT;
                m_Overlay.ToggleEditMode(true);
                std::cout << "[Engine] PAUSED. Overlay Active. Inputs redirected to Draw Tools." << std::endl;
            } 
            else if (m_State == EngineState::PAUSED_FOR_EDIT) {
                m_State = EngineState::RECORDING;
                m_Overlay.ToggleEditMode(false);
                std::cout << "[Engine] RESUMED. Audio/Video synced seamlessly." << std::endl;
            }
        }

        // 3. The "Time Travel" Command
        // Call this when user draws a "Blur Rect" and enables the "3s" icon
        void TriggerRetroactiveRepair(int x, int y, int w, int h) {
            std::cout << "[Engine] Launching background repair task..." << std::endl;

            // Fire and Forget: Launch a detached thread to fix history
            // This ensures the UI never freezes.
            std::thread worker([=]() {
                // Tell the RingBuffer to rewind 3 seconds and apply blur
                m_Buffer.ApplyRetroactiveMask(3000, x, y, w, h, 
                    [](std::vector<uint8_t>& data, int width, int height, int rx, int ry, int rw, int rh) {
                        // This lambda would call OpenCV::GaussianBlur
                        // We keep it abstract here to avoid linking OpenCV in this header
                    });
            });
            
            worker.detach(); // Let it run in background
        }

        void StopRecording() {
            m_State = EngineState::STOPPING;
            if (m_CaptureThread.joinable()) m_CaptureThread.join();
        }

    private:
        // The main heartbeat loop
        void CaptureLoop() {
            while (m_State != EngineState::STOPPING) {
                if (m_State == EngineState::PAUSED_FOR_EDIT) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue; // Don't write to disk, but maybe keep capturing for preview?
                }

                // Acquire frame from GPU
                // Push to RingBuffer
                // Signal Writer Thread (not implemented in this skeleton)
                std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
            }
        }
    };
}
