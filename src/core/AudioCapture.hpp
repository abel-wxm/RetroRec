/**
 * RetroRec - Audio Capture Module (The "Ears")
 * * ARCHITECTURE NOTE (v1.0 Intent):
 * This module uses Windows WASAPI (Loopback Mode) to capture system audio.
 * * * SYNC CRITICAL:
 * 1. Audio and Video run on different clocks. We MUST use the Audio Clock as the Master Clock.
 * 2. If silence is detected (no audio playing), we must generate "Silence Packets" 
 * to keep the MP4 file timebase synchronized.
 */

#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
#include <iostream>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace RetroRec::Core {

    class AudioCapturer {
    private:
        ComPtr<IMMDeviceEnumerator> m_Enumerator;
        ComPtr<IMMDevice> m_Device;
        ComPtr<IAudioClient> m_AudioClient;
        ComPtr<IAudioCaptureClient> m_CaptureClient;
        WAVEFORMATEX* m_MixFormat = nullptr;
        
        bool m_IsRecording = false;

    public:
        AudioCapturer() {
            CoInitialize(nullptr); // Initialize COM library
        }

        ~AudioCapturer() {
            Stop();
            if (m_MixFormat) CoTaskMemFree(m_MixFormat);
            CoUninitialize();
        }

        // Step 1: Initialize WASAPI Loopback
        bool Init() {
            HRESULT hr;

            // Get default audio endpoint (Speakers)
            hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, 
                                  __uuidof(IMMDeviceEnumerator), (void**)&m_Enumerator.GetAddressOf());
            if (FAILED(hr)) return false;

            hr = m_Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_Device.GetAddressOf());
            if (FAILED(hr)) {
                std::cerr << "[Audio] No Speaker Found!" << std::endl;
                return false;
            }

            // Activate Audio Client
            hr = m_Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_AudioClient.GetAddressOf());
            if (FAILED(hr)) return false;

            // Get the format (usually 44.1kHz or 48kHz Stereo)
            m_AudioClient->GetMixFormat(&m_MixFormat);
            
            // Initialize in LOOPBACK mode (Capture what is being played)
            // AUDCLNT_STREAMFLAGS_LOOPBACK is the magic flag here.
            hr = m_AudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 
                                           AUDCLNT_STREAMFLAGS_LOOPBACK, 
                                           10000000, 0, m_MixFormat, nullptr);
            if (FAILED(hr)) return false;

            // Get the capture service
            hr = m_AudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_CaptureClient.GetAddressOf());
            
            return SUCCEEDED(hr);
        }

        // Step 2: Start Capture
        void Start() {
            if (m_AudioClient) {
                m_AudioClient->Start();
                m_IsRecording = true;
                std::cout << "[Audio] WASAPI Loopback Started." << std::endl;
            }
        }

        void Stop() {
            if (m_AudioClient) {
                m_AudioClient->Stop();
                m_IsRecording = false;
            }
        }

        // Step 3: Fetch Audio Data (Called by Recorder Loop)
        // Returns a vector of raw PCM bytes
        std::vector<BYTE> CapturePacket() {
            if (!m_IsRecording || !m_CaptureClient) return {};

            UINT32 packetLength = 0;
            HRESULT hr = m_CaptureClient->GetNextPacketSize(&packetLength);
            
            if (packetLength == 0) return {}; // No sound playing

            BYTE* pData;
            UINT32 numFramesAvailable;
            DWORD flags;

            hr = m_CaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr)) return {};

            // Deep copy the audio buffer
            int bytesPerFrame = m_MixFormat->nBlockAlign;
            int totalBytes = numFramesAvailable * bytesPerFrame;
            
            std::vector<BYTE> buffer(pData, pData + totalBytes);

            // Important: Release buffer back to Windows
            m_CaptureClient->ReleaseBuffer(numFramesAvailable);

            return buffer;
        }
    };
}
