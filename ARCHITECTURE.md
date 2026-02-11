# RetroRec System Architecture

## 1. Core Logic: The Ring Buffer & Time Travel
The system operates on a dual-thread model to ensure 0% frame drop during recording.

### Data Flow
1. **Producer (DXGI):** Captures screen at 30/60 FPS.
2. **Ring Buffer:** Stores the last N seconds (Configurable, default 3s) in RAM.
3. **Consumer (Disk Writer):** Writes frames to disk *delayed by N seconds*.
4. **Retro-Intervention:** When the user marks a region as "Private", the `RepairEngine` modifies the frames inside the Ring Buffer *before* they are consumed by the Disk Writer.

## 2. Privacy Mode Interaction
* **Hotkeys:** Left-hand focused (`Ctrl+Space`).
* **Behavior:**
    * **Pause Mode:** Recording halts, Overlay activates. User draws a mask. System applies mask to buffer history.
    * **Live Mode:** User draws while recording. System asynchronously blurs history in the background.

## 3. Tech Stack Requirements
* **Language:** C++ 17
* **GUI:** Direct2D / ImGui (For lightweight overlay)
* **Video I/O:** FFmpeg (libavcodec)
