RetroRec (时光倒流录屏)
No Regrets. A high-performance screen recorder that lets you fix privacy leaks after they happen.
不留遗憾。 一款允许你在录制后“时光倒流”，对意外泄露的隐私进行回溯打码的高性能录屏工具。

💡 The Concept (核心理念)
In screen recording (demos, tutorials, coding), privacy leaks—like accidentally showing a password or API key—usually mean scrapping the footage and starting over.

RetroRec changes this. It maintains a 3-second Ring Buffer in memory. When a leak occurs, you don't need to stop. Just hit a shortcut (Ctrl + Space), and the software proactively masks the past 3 seconds of the specific region, saving you hours of re-recording time.

在录屏演示（如网课、代码讲解）中，意外展示密码或敏感信息通常意味着录制报废。RetroRec 改变了这一点。它在内存中维护一个环形缓冲区。当你意识到泄露时，按下快捷键，软件会自动对过去3秒的指定区域进行高斯模糊。

🏗️ Architecture (技术架构)
We are designing this as a lightweight, native C++ application for Windows.

Core: C++ 17 / 20

Capture: Windows DXGI (Desktop Duplication API) for low-latency capture.

Memory: Zero-copy Ring Buffer (Storing Raw YUV or H.264 chunks).

Rendering: Direct2D Overlay for low-overhead UI.

Logic: Asynchronous Repair Queue (Retroactive processing without blocking the main thread).

🤝 Call for Contributors (寻找队友！)
Current Status:
I am the Product Owner and Architect of this project. I have designed the complete interaction logic and technical roadmap.
However, I do not have a local C++ development environment.

I am looking for C++ developers who are interested in:

High-performance graphics programming (DirectX/OpenGL).

Video encoding/decoding (FFmpeg).

Windows API hooks and system interaction.

If you are interested in turning this innovative idea into reality, please check the ARCHITECTURE.md and submit a Pull Request!

我作为发起人已经完成了所有的架构设计和交互逻辑规划。但我目前没有本地编译环境。 我正在寻找对高性能图形编程感兴趣的 C++ 开发者加入项目，共同将这个创意落地！

📄 License
MIT License. Copyright (c) 2026.
