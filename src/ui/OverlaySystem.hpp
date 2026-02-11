/**
 * RetroRec - Overlay & Interaction System (The "Hands")
 * * ARCHITECTURE NOTE (v1.0 Intent):
 * This module manages the transparent drawing layer that appears when the user pauses recording.
 * It strictly separates "Annotation Tools" (Pen, Rect) from "Privacy Tools" (Blur, Mosaic).
 * * * Key Interaction Logic:
 * 1. "Privacy Tools" automatically carry a "Retroactive" property (default = true).
 * 2. When a Privacy Tool is used, a "3s" icon appears near the object.
 * 3. Clicking the icon toggles the retroactive behavior (saving processing power).
 */

#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <functional>

namespace RetroRec::UI {

    // Defined tool types based on user requirements
    enum class ToolType {
        PEN,            // Standard Red/Yellow pen for teaching (No Retroactive effect)
        RECTANGLE,      // Box highlight (No Retroactive effect)
        ARROW,          // Directional arrow (No Retroactive effect)
        
        // Privacy Tools
        GAUSSIAN_BLUR,  // Blurs the region. DEFAULT: Retroactive = True
        MOSAIC          // Pixelates the region. DEFAULT: Retroactive = True
    };

    struct Point { int x, y; };
    struct Rect { int x, y, w, h; };

    // Every stroke or box drawn by the user is an "Object"
    struct OverlayObject {
        int ID;
        ToolType Type;
        Rect Bounds;
        std::vector<Point> StrokePath; // For Pen tools
        
        // The "Time Travel" Flag
        // If true, the backend will apply this effect to the past 3 seconds in the RingBuffer.
        bool IsRetroactive; 
        
        // Timestamp when this object was drawn (used to hide the "3s" icon after a few seconds if needed)
        std::chrono::system_clock::time_point CreationTime;

        OverlayObject(int id, ToolType type, Rect bounds) 
            : ID(id), Type(type), Bounds(bounds), CreationTime(std::chrono::system_clock::now()) {
            
            // Auto-enable retroactive masking ONLY for privacy tools
            if (type == ToolType::GAUSSIAN_BLUR || type == ToolType::MOSAIC) {
                IsRetroactive = true;
            } else {
                IsRetroactive = false;
            }
        }
    };

    class OverlayController {
    private:
        std::vector<OverlayObject> m_Objects;
        int m_NextID = 1;
        bool m_IsEditingMode = false; // Triggered by Left-Hand Shortcut (Ctrl+Space)

    public:
        // Called when user presses Ctrl+Space
        void ToggleEditMode(bool active) {
            m_IsEditingMode = active;
            // When entering edit mode, we might want to clear temporary hover states
        }

        // Called when user finishes drawing a shape
        void AddObject(ToolType type, Rect bounds) {
            m_Objects.emplace_back(m_NextID++, type, bounds);
        }

        // The UI Renderer calls this 60 times a second
        // We pass a "Drawer" interface to keep this logic decoupled from Direct2D/GDI
        template <typename Renderer>
        void Render(Renderer& drawer) {
            if (m_Objects.empty()) return;

            for (auto& obj : m_Objects) {
                // 1. Draw the actual shape (Blur rect or Pen stroke)
                drawer.DrawShape(obj);

                // 2. LOGIC: Draw the "3s Retroactive" Icon
                // Requirements: 
                // - Must be a privacy tool
                // - Must be in Edit Mode (Paused)
                // - User hasn't disabled it yet
                if (IsPrivacyTool(obj.Type) && m_IsEditingMode) {
                    
                    // Visual state: Active (Red/Blue) or Disabled (Gray)
                    std::string iconState = obj.IsRetroactive ? "ICON_3S_ACTIVE" : "ICON_3S_DISABLED";
                    
                    // Draw icon at top-right corner of the object
                    drawer.DrawIcon(obj.Bounds.x + obj.Bounds.w, obj.Bounds.y, iconState);
                }
            }
        }

        // Interaction: User clicks on the overlay
        void OnClick(int x, int y) {
            if (!m_IsEditingMode) return;

            // Check if user clicked the "3s" icon of any object
            for (auto& obj : m_Objects) {
                if (IsPrivacyTool(obj.Type)) {
                    // Hit-test logic for the icon (Conceptual)
                    if (IsOverIcon(x, y, obj.Bounds)) {
                        // TOGGLE the state! This saves CPU power if user didn't mean to blur the past.
                        obj.IsRetroactive = !obj.IsRetroactive;
                        return;
                    }
                }
            }
        }

    private:
        bool IsPrivacyTool(ToolType type) {
            return type == ToolType::GAUSSIAN_BLUR || type == ToolType::MOSAIC;
        }

        bool IsOverIcon(int x, int y, Rect objBounds) {
            // Implementation detail: check if x,y is within the icon area
            return false; 
        }
    };
}
