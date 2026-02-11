RetroRec (时光倒流录屏) ⚡
Record & Publish. The Zero-Post-Production screen recorder for professionals.

即录即发。 专为专业人士打造的“零后期”高性能录屏工具。

⚡ The Philosophy: Efficiency First (核心理念：效率至上)
Problem: In technical demos or live coding, a 1-second accidental exposure of a password or API key usually forces you to scrap the recording or spend hours in post-production (blurring frame by frame).

Solution: RetroRec eliminates the need for post-editing.
It introduces a "Silent Processing" workflow. When you encounter sensitive data (or realize you just showed it), you hit a shortcut. The engine silently processes the Ring Buffer in memory, applying a Gaussian Blur to the specific region in the past 3 seconds.

The Result: When you hit "Stop", your video is clean, safe, and ready to upload. No rendering. No secondary editing.

痛点： 在技术演示或代码讲解中，仅仅 1 秒钟的密码或 API Key 泄露，往往意味着整个录制报废，或者需要花费数小时进行后期打码和重新渲染。

解决方案： RetroRec 彻底消除了后期剪辑的需求。
它引入了**“静默处理”工作流。当你遇到敏感信息（或意识到刚才不小心展示了）时，只需按下快捷键。引擎会在后台静默处理内存中的环形缓冲区**，自动对过去3秒的指定区域进行高斯模糊。

结果： 当你点击“停止”时，你得到的是一个干净、安全、即刻可发布的视频。无需渲染，拒绝二次剪辑。

🏗️ Architecture (技术架构)
We are designing this as a lightweight, native C++ application for Windows, optimized for 0% frame drop.

Core: C++ 17 / 20

Capture: Windows DXGI (Desktop Duplication API) for low-latency capture.

Memory: Zero-copy Ring Buffer (The "Time Machine" holding raw frames).

Rendering: Direct2D Overlay for non-intrusive UI.

Logic: Asynchronous Repair Queue (Background processing without blocking the recording loop).

🤝 Call for Contributors (寻找队友！)
Current Status:
I am the Product Owner and Architect. I have designed the complete interaction logic and technical roadmap.
However, I do not have a local C++ development environment.

I am looking for C++ developers who want to build the ultimate efficiency tool:

Graphics: High-performance capture (DirectX/DXGI).

Video: Encoding pipeline (FFmpeg/NVENC).

System: Low-level Windows hooks.

If you hate video editing as much as I do, check the ARCHITECTURE.md and submit a Pull Request!

我作为发起人已经完成了所有的架构设计和交互逻辑规划。但我目前没有本地编译环境。 我正在寻找痛恨繁琐剪辑、追求极致效率的 C++ 开发者加入项目，共同打造这款生产力工具！

📄 License
MIT License. Copyright (c) 2026.
