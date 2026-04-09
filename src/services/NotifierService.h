#pragma once

#include <string>
#include <mutex>
#include <cstdlib>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <csignal>
#include <unistd.h>
#endif

namespace closecrab {

class NotifierService {
public:
    static NotifierService& getInstance() {
        static NotifierService instance;
        return instance;
    }

    void notify(const std::string& title, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!enabled_) {
            spdlog::debug("Notifier: notifications disabled, skipping");
            return;
        }

        spdlog::info("Notifier: sending notification '{}': {}", title, message);

#ifdef _WIN32
        notifyWindows(title, message);
#elif defined(__APPLE__)
        notifyMacOS(title, message);
#else
        notifyLinux(title, message);
#endif
    }

    void setEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
        spdlog::info("Notifier: notifications {}", enabled ? "enabled" : "disabled");
    }

    bool isEnabled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return enabled_;
    }

private:
    NotifierService() = default;
    mutable std::mutex mutex_;
    bool enabled_ = true;

    static std::string escapeForShell(const std::string& s) {
        std::string result;
        for (char c : s) {
            if (c == '"' || c == '\\' || c == '`' || c == '$') {
                result += '\\';
            }
            result += c;
        }
        return result;
    }

#ifdef _WIN32
    void notifyWindows(const std::string& title, const std::string& message) {
        // Use PowerShell to show a toast notification via BurntToast or fallback
        std::string escapedTitle = escapeForShell(title);
        std::string escapedMsg = escapeForShell(message);

        std::string cmd =
            "powershell -NoProfile -Command \""
            "[Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, "
            "ContentType = WindowsRuntime] | Out-Null; "
            "$template = [Windows.UI.Notifications.ToastNotificationManager]::"
            "GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02); "
            "$textNodes = $template.GetElementsByTagName('text'); "
            "$textNodes.Item(0).AppendChild($template.CreateTextNode('" + escapedTitle + "')); "
            "$textNodes.Item(1).AppendChild($template.CreateTextNode('" + escapedMsg + "')); "
            "$toast = [Windows.UI.Notifications.ToastNotification]::new($template); "
            "[Windows.UI.Notifications.ToastNotificationManager]::"
            "CreateToastNotifier('CloseCrab').Show($toast)\"";

        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            spdlog::debug("Notifier: toast notification failed (ret={}), trying fallback", ret);
            // Fallback: simple message box via PowerShell
            std::string fallback =
                "powershell -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; "
                "[System.Windows.Forms.MessageBox]::Show('" + escapedMsg + "', '" + escapedTitle + "')\"";
            std::system(fallback.c_str());
        }
    }
#elif defined(__APPLE__)
    void notifyMacOS(const std::string& title, const std::string& message) {
        std::string escapedTitle = escapeForShell(title);
        std::string escapedMsg = escapeForShell(message);

        std::string cmd =
            "osascript -e 'display notification \"" + escapedMsg +
            "\" with title \"" + escapedTitle + "\"'";

        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            spdlog::warn("Notifier: osascript notification failed (ret={})", ret);
        }
    }
#else
    void notifyLinux(const std::string& title, const std::string& message) {
        std::string escapedTitle = escapeForShell(title);
        std::string escapedMsg = escapeForShell(message);

        std::string cmd = "notify-send \"" + escapedTitle + "\" \"" + escapedMsg + "\"";

        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            spdlog::debug("Notifier: notify-send failed (ret={}), trying kdialog", ret);
            cmd = "kdialog --passivepopup \"" + escapedMsg + "\" 5 --title \"" + escapedTitle + "\"";
            std::system(cmd.c_str());
        }
    }
#endif
};

} // namespace closecrab
