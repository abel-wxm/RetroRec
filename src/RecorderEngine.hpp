#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>
#include <vector>
#include <wrl/client.h> // 微软智能指针

// 使用 ComPtr 自动管理内存，防止内存泄漏
using Microsoft::WRL::ComPtr;

namespace retrorec {

    class RecorderEngine {
    private:
        // DirectX 核心变量
        ComPtr<ID3D11Device> d3d_device;
        ComPtr<ID3D11DeviceContext> d3d_context;
        ComPtr<IDXGIOutputDuplication> dxgi_duplication;
        DXGI_OUTPUT_DESC output_desc;

        bool is_initialized = false;

    public:
        RecorderEngine() = default;
        ~RecorderEngine() { cleanup(); }

        // 1. 初始化 DirectX (连接显卡)
        bool initialize() {
            if (is_initialized) return true;

            HRESULT hr;

            // 创建 D3D 设备
            D3D_FEATURE_LEVEL featureLevel;
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &d3d_device, &featureLevel, &d3d_context);

            if (FAILED(hr)) return false;

            // 获取 DXGI 设备
            ComPtr<IDXGIDevice> dxgi_device;
            hr = d3d_device.As(&dxgi_device);
            if (FAILED(hr)) return false;

            // 获取显卡适配器
            ComPtr<IDXGIAdapter> dxgi_adapter;
            hr = dxgi_device->GetAdapter(&dxgi_adapter);
            if (FAILED(hr)) return false;

            // 获取显示器 0
            ComPtr<IDXGIOutput> dxgi_output;
            hr = dxgi_adapter->EnumOutputs(0, &dxgi_output);
            if (FAILED(hr)) return false;

            // --- 核心步骤：创建桌面副本 (抓屏源头) ---
            ComPtr<IDXGIOutput1> dxgi_output1;
            hr = dxgi_output.As(&dxgi_output1);
            hr = dxgi_output1->DuplicateOutput(d3d_device.Get(), &dxgi_duplication);

            if (FAILED(hr)) {
                // 如果这里失败，通常是因为没有显示器连接，或者全屏游戏独占
                return false;
            }

            dxgi_output->GetDesc(&output_desc);
            is_initialized = true;
            return true;
        }

        // 清理资源
        void cleanup() {
            is_initialized = false;
            // ComPtr 会自动释放 d3d_device 等资源，无需手动 Release
        }
        
        // 检查引擎是否就绪
        bool isReady() const { return is_initialized; }
    };
}
