# RetroRec Roadmap (开发路线图) 🗺️

This document tracks planned features and ideas for future versions.
本文档记录了 RetroRec 未来的开发计划和灵感。

## 🚀 v1.1 - The "Efficiency" Update (效率升级)
* **[New] Stamp Mode (印章打码模式):**
    * **Concept:** A preset rectangular block (e.g., 200x50px) that applies a mask with a single click.
    * **Use Case:** Instantly hiding passwords, credit card numbers, or avatars without dragging a box.
    * **Config:** User can adjust the preset size with mouse wheel or hotkeys.
    * **想法来源:** "做个预设像素大小的矩形块，只要在对应位置一点就行。"

* **[Optimization] Smart UI Detection (智能 UI 识别):**
    * Use OpenCV to automatically snap the mask to UI elements (buttons, input fields).

## 🐛 v1.0 - Core Stability (当前目标)
* [ ] Implement DXGI Capture (src/core/DXGICapture.hpp)
* [ ] Finish RingBuffer Logic (src/core/RingBuffer.hpp)
* [ ] Connect FFmpeg Encoder (src/core/VideoEncoder.hpp)
* [ ] Audio Sync with WASAPI

---
*Got a new idea? Add it here via Pull Request!*
