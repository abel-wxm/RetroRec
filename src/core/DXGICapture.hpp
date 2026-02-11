/**
 * RetroRec - DXGI Screen Capture Module (The "Eyes")
 * * ARCHITECTURE NOTE (v1.0 Intent):
 * This module MUST use Windows Desktop Duplication API (DXGI) for 0-copy capture.
 * DO NOT downgrade to GDI (BitBlt) as it will kill performance during 60fps recording.
 * * Key Responsibilities:
 * 1. Initialize D3D11 Device.
 * 2. Acquire "Dirty Rects" (Only update changed pixels to save bandwidth).
 * 3. Capture Mouse Pointer shape and position separately (vital for presentation highlights).
 */

#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <stdexcept>

// Using ComPtr for automatic resource management (No memory leaks allowed!)
using Microsoft::WRL::ComPtr;

namespace RetroRec::Core {

    // Structure to hold mouse cursor data separately
    // We need this because we might want to highlight/hide cursor in post-processing
    struct CursorInfo {
        bool Visible;
        int X, Y;
        std::vector<uint8_t> ShapeBuffer; // Cursor bitmap data
        DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
    };

    class DXGICapturer {
    private:
        ComPtr<ID3D11Device> m_Device;
        ComPtr<ID3D11DeviceContext> m_DeviceContext;
        ComPtr<IDXGIOutputDuplication> m_DeskDupl;
        DXGI_OUTPUT_DESC m_OutputDesc;
        
        // Texture that holds the current screen image
        ComPtr<ID3D11Texture2D> m_AcquiredDesktopImage;
        
    public:
        DXGICapturer() = default;
        ~DXGICapturer() { CleanUp(); }

        // Step 1: Initialize the GPU hook
        void Init(int adapterIndex = 0, int outputIndex = 0) {
            HRESULT hr = S_OK;
            
            // Driver types to attempt (Hardware first, then WARP/Software)
            D3D_DRIVER_TYPE driverTypes[] = {
                D3D_DRIVER_TYPE_HARDWARE,
                D3D_DRIVER_TYPE_WARP,
            };

            // Create D3D11 Device
            // ARCHITECTURE REQUIREMENT: Must support BGRA for Video Encoding compatibility
            UINT createDeviceFlags = 0;
            #ifdef _DEBUG
            createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
            #endif

            // ... (Implementation detail: Device creation logic would go here)
            // Ideally, we obtain the IDXGIOutputDuplication interface here.
            
            // Placeholder for collaborators:
            // TODO: Implement D3D11CreateDevice and CreateOutputDuplication here.
            // Ensure we are attached to the discrete GPU if available.
        }

        // Step 2: The Heartbeat - Get the latest frame
        // Returns false if timeout (screen didn't change), true if new frame arrived
        bool AcquireNextFrame(int timeoutMs, CursorInfo* outCursor) {
            if (!m_DeskDupl) return false;

            DXGI_OUTDUPL_FRAME_INFO frameInfo;
            ComPtr<IDXGIResource> desktopResource;

            // The core API call: Get the frame from GPU memory
            HRESULT hr = m_DeskDupl->AcquireNextFrame(timeoutMs, &frameInfo, desktopResource.GetAddressOf());

            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                return false; // No screen update, keep previous frame
            }
            if (FAILED(hr)) {
                // If device lost (e.g., UAC popup), we need to reset
                return false;
            }

            // ARCHITECTURE CRITICAL POINT:
            // We now have the texture in GPU memory. 
            // DO NOT copy to CPU RAM yet if we are using Hardware Encoding (NVENC).
            // Only copy if we need to apply OpenCV effects (Blurring).
            
            // Get the texture interface
            desktopResource.As(&m_AcquiredDesktopImage);

            // Capture Cursor (If requested)
            if (outCursor) {
                // Logic to extract cursor position and shape metadata
                // This allows the "Presentation Mode" to draw a yellow halo around the mouse later.
                outCursor->Visible = frameInfo.PointerPosition.Visible;
                outCursor->X = frameInfo.PointerPosition.Position.x;
                outCursor->Y = frameInfo.PointerPosition.Position.y;
            }

            return true;
        }

        // Release the frame so Windows can update the screen again
        void ReleaseFrame() {
            if (m_DeskDupl) m_DeskDupl->ReleaseFrame();
        }

    private:
        void CleanUp() {
            m_DeskDupl.Reset();
            m_DeviceContext.Reset();
            m_Device.Reset();
        }
    };
}
