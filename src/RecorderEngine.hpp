#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace retrorec {
    class RecorderEngine {
    private:
        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        ComPtr<IDXGIOutputDuplication> dxgi_duplication;
        bool is_initialized = false;
    public:
        RecorderEngine() = default;
        ~RecorderEngine() { cleanup(); }

        bool initialize() {
            if (is_initialized) return true;
            HRESULT hr;
            
            // 1. 创建 D3D 设备
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d3d_device, nullptr, &d3d_context);
            if (FAILED(hr)) return false;

            // 2. 获取 DXGI 接口
            ComPtr<IDXGIDevice> dxgi_device;
            hr = d3d_device.As(&dxgi_device);
            if (FAILED(hr)) return false;

            ComPtr<IDXGIAdapter> dxgi_adapter;
            hr = dxgi_device->GetAdapter(&dxgi_adapter);
            if (FAILED(hr)) return false;

            ComPtr<IDXGIOutput> dxgi_output;
            hr = dxgi_adapter->EnumOutputs(0, &dxgi_output);
            if (FAILED(hr)) return false;

            // 3. 创建抓屏副本
            ComPtr<IDXGIOutput1> dxgi_output1;
            hr = dxgi_output.As(&dxgi_output1);
            hr = dxgi_output1->DuplicateOutput(d3d_device.Get(), &dxgi_duplication);
            if (FAILED(hr)) return false;

            is_initialized = true;
            return true;
        }

        void cleanup() { is_initialized = false; }
        bool isReady() const { return is_initialized; }
    };
}
