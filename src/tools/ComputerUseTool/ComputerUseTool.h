#pragma once
#include "../Tool.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace closecrab {

class ComputerUseTool : public Tool {
public:
    std::string getName() const override { return "ComputerUse"; }
    std::string getDescription() const override {
        return "Control the computer: take screenshots, move mouse, click, type text. "
               "Use for GUI automation and visual verification.";
    }
    std::string getCategory() const override { return "computer"; }
    bool isDestructive() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"action", {{"type", "string"}, {"enum", {"screenshot", "click", "type", "move", "scroll", "key"}},
                    {"description", "Action to perform"}}},
                {"x", {{"type", "integer"}, {"description", "X coordinate (for click/move)"}}},
                {"y", {{"type", "integer"}, {"description", "Y coordinate (for click/move)"}}},
                {"text", {{"type", "string"}, {"description", "Text to type (for type action)"}}},
                {"key", {{"type", "string"}, {"description", "Key to press (for key action, e.g. 'enter', 'tab')"}}},
                {"button", {{"type", "string"}, {"description", "Mouse button: left, right, middle"}}},
                {"amount", {{"type", "integer"}, {"description", "Scroll amount (for scroll action)"}}}
            }},
            {"required", {"action"}}
        };
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string action = input["action"].get<std::string>();

#ifdef _WIN32
        if (action == "screenshot") {
            return takeScreenshot();
        } else if (action == "click") {
            int x = input.value("x", 0);
            int y = input.value("y", 0);
            std::string button = input.value("button", "left");
            return doClick(x, y, button);
        } else if (action == "type") {
            std::string text = input.value("text", "");
            return doType(text);
        } else if (action == "move") {
            int x = input.value("x", 0);
            int y = input.value("y", 0);
            return doMove(x, y);
        } else if (action == "key") {
            std::string key = input.value("key", "");
            return doKey(key);
        } else if (action == "scroll") {
            int amount = input.value("amount", 3);
            return doScroll(amount);
        }
#endif
        return ToolResult::fail("Unsupported action: " + action);
    }

private:
#ifdef _WIN32
    ToolResult takeScreenshot() {
        // Get screen dimensions
        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);

        // Capture screen to bitmap
        HDC hScreen = GetDC(nullptr);
        HDC hDC = CreateCompatibleDC(hScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
        SelectObject(hDC, hBitmap);
        BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);

        // Save to file (BMP for simplicity)
        std::string path = "data/screenshot.bmp";
        saveBitmap(hBitmap, width, height, path);

        DeleteObject(hBitmap);
        DeleteDC(hDC);
        ReleaseDC(nullptr, hScreen);

        return ToolResult::ok("Screenshot saved to " + path + " (" +
            std::to_string(width) + "x" + std::to_string(height) + ")");
    }

    ToolResult doClick(int x, int y, const std::string& button) {
        SetCursorPos(x, y);
        DWORD flags = (button == "right") ? (MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP)
                                           : (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP);
        mouse_event(flags, 0, 0, 0, 0);
        return ToolResult::ok("Clicked at (" + std::to_string(x) + "," + std::to_string(y) + ")");
    }

    ToolResult doType(const std::string& text) {
        for (char c : text) {
            SHORT vk = VkKeyScanA(c);
            BYTE key = LOBYTE(vk);
            bool shift = (HIBYTE(vk) & 1) != 0;
            if (shift) keybd_event(VK_SHIFT, 0, 0, 0);
            keybd_event(key, 0, 0, 0);
            keybd_event(key, 0, KEYEVENTF_KEYUP, 0);
            if (shift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
        }
        return ToolResult::ok("Typed " + std::to_string(text.size()) + " characters");
    }

    ToolResult doMove(int x, int y) {
        SetCursorPos(x, y);
        return ToolResult::ok("Moved to (" + std::to_string(x) + "," + std::to_string(y) + ")");
    }

    ToolResult doKey(const std::string& key) {
        BYTE vk = 0;
        if (key == "enter") vk = VK_RETURN;
        else if (key == "tab") vk = VK_TAB;
        else if (key == "escape") vk = VK_ESCAPE;
        else if (key == "backspace") vk = VK_BACK;
        else if (key == "delete") vk = VK_DELETE;
        else if (key == "up") vk = VK_UP;
        else if (key == "down") vk = VK_DOWN;
        else if (key == "left") vk = VK_LEFT;
        else if (key == "right") vk = VK_RIGHT;
        else return ToolResult::fail("Unknown key: " + key);

        keybd_event(vk, 0, 0, 0);
        keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);
        return ToolResult::ok("Pressed key: " + key);
    }

    ToolResult doScroll(int amount) {
        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(amount * WHEEL_DELTA), 0);
        return ToolResult::ok("Scrolled " + std::to_string(amount) + " units");
    }

    void saveBitmap(HBITMAP hBitmap, int width, int height, const std::string& path) {
        BITMAP bmp;
        GetObject(hBitmap, sizeof(BITMAP), &bmp);

        BITMAPFILEHEADER bmfHeader = {};
        BITMAPINFOHEADER bi = {};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = width;
        bi.biHeight = -height; // top-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        DWORD dwBmpSize = width * height * 4;
        std::vector<char> buffer(dwBmpSize);

        HDC hDC = CreateCompatibleDC(nullptr);
        GetDIBits(hDC, hBitmap, 0, height, buffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        DeleteDC(hDC);

        bmfHeader.bfType = 0x4D42;
        bmfHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwBmpSize;
        bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

        std::filesystem::create_directories("data");
        std::ofstream file(path, std::ios::binary);
        file.write((char*)&bmfHeader, sizeof(bmfHeader));
        file.write((char*)&bi, sizeof(bi));
        file.write(buffer.data(), dwBmpSize);
    }
#endif
};

} // namespace closecrab
