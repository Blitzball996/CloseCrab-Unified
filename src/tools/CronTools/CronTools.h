#pragma once

#include "../Tool.h"
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <regex>
#include <atomic>
#include <sstream>
#include <ctime>

namespace closecrab {

struct CronJob {
    std::string id;
    std::string cronExpr;
    std::string prompt;
    bool recurring = true;
    bool durable = false;
    std::chrono::system_clock::time_point nextFire;
    bool fired = false;
};

// Parses standard 5-field cron: minute hour dom month dow
struct CronFields {
    std::vector<int> minutes;   // 0-59
    std::vector<int> hours;     // 0-23
    std::vector<int> doms;      // 1-31
    std::vector<int> months;    // 1-12
    std::vector<int> dows;      // 0-6 (0=Sunday)
    bool valid = false;
};

inline CronFields parseCronExpr(const std::string& expr) {
    CronFields f;
    std::istringstream iss(expr);
    std::string parts[5];
    for (int i = 0; i < 5; i++) {
        if (!(iss >> parts[i])) return f;
    }

    auto parseField = [](const std::string& s, int minVal, int maxVal) -> std::vector<int> {
        std::vector<int> result;
        // Handle */N
        if (s.size() > 2 && s[0] == '*' && s[1] == '/') {
            int step = std::stoi(s.substr(2));
            if (step <= 0) step = 1;
            for (int i = minVal; i <= maxVal; i += step) result.push_back(i);
            return result;
        }
        // Handle *
        if (s == "*") {
            for (int i = minVal; i <= maxVal; i++) result.push_back(i);
            return result;
        }
        // Handle comma-separated values and ranges
        std::istringstream ss(s);
        std::string token;
        while (std::getline(ss, token, ',')) {
            auto dash = token.find('-');
            if (dash != std::string::npos) {
                int lo = std::stoi(token.substr(0, dash));
                int hi = std::stoi(token.substr(dash + 1));
                for (int i = lo; i <= hi && i <= maxVal; i++) result.push_back(i);
            } else {
                int val = std::stoi(token);
                if (val >= minVal && val <= maxVal) result.push_back(val);
            }
        }
        return result;
    };

    try {
        f.minutes = parseField(parts[0], 0, 59);
        f.hours   = parseField(parts[1], 0, 23);
        f.doms    = parseField(parts[2], 1, 31);
        f.months  = parseField(parts[3], 1, 12);
        f.dows    = parseField(parts[4], 0, 6);
        f.valid = !f.minutes.empty() && !f.hours.empty();
    } catch (...) {
        f.valid = false;
    }
    return f;
}

inline std::chrono::system_clock::time_point computeNextFire(const std::string& cronExpr) {
    auto fields = parseCronExpr(cronExpr);
    if (!fields.valid) {
        return std::chrono::system_clock::now() + std::chrono::minutes(1);
    }

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);

    // Search forward up to 7 days
    for (int attempt = 0; attempt < 10080; attempt++) { // 7 days * 24h * 60m
        tm.tm_min++;
        std::mktime(&tm); // normalize

        bool minMatch = false, hourMatch = false, domMatch = false, monMatch = false, dowMatch = false;
        for (int v : fields.minutes) if (v == tm.tm_min) { minMatch = true; break; }
        for (int v : fields.hours)   if (v == tm.tm_hour) { hourMatch = true; break; }
        for (int v : fields.doms)    if (v == tm.tm_mday) { domMatch = true; break; }
        for (int v : fields.months)  if (v == tm.tm_mon + 1) { monMatch = true; break; }
        for (int v : fields.dows)    if (v == tm.tm_wday) { dowMatch = true; break; }

        if (minMatch && hourMatch && domMatch && monMatch && dowMatch) {
            tm.tm_sec = 0;
            return std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
    }
    // Fallback
    return now + std::chrono::hours(24);
}

class CronScheduler {
public:
    static CronScheduler& getInstance() {
        static CronScheduler instance;
        return instance;
    }

    std::string schedule(const std::string& cron, const std::string& prompt,
                         bool recurring = true, bool durable = false) {
        std::lock_guard<std::mutex> lock(mutex_);
        CronJob job;
        job.id = "cron_" + std::to_string(++nextId_);
        job.cronExpr = cron;
        job.prompt = prompt;
        job.recurring = recurring;
        job.durable = durable;
        job.nextFire = computeNextFire(cron);
        jobs_.push_back(std::move(job));
        return jobs_.back().id;
    }

    void cancel(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
            [&](const CronJob& j) { return j.id == id; }), jobs_.end());
    }

    std::vector<CronJob> list() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobs_;
    }

    // Check for jobs that should fire now, returns their prompts
    std::vector<std::string> checkAndFire() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        std::vector<std::string> fired;
        std::vector<std::string> toRemove;

        for (auto& job : jobs_) {
            if (now >= job.nextFire) {
                fired.push_back(job.prompt);
                if (job.recurring) {
                    job.nextFire = computeNextFire(job.cronExpr);
                } else {
                    toRemove.push_back(job.id);
                }
            }
        }

        for (const auto& id : toRemove) {
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                [&](const CronJob& j) { return j.id == id; }), jobs_.end());
        }
        return fired;
    }

private:
    CronScheduler() = default;
    mutable std::mutex mutex_;
    std::vector<CronJob> jobs_;
    int nextId_ = 0;
};

class CronCreateTool : public Tool {
public:
    std::string getName() const override { return "CronCreate"; }
    std::string getDescription() const override {
        return "Schedule a prompt to run on a cron schedule.";
    }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {
            {"cron", {{"type", "string"}, {"description", "5-field cron expression"}}},
            {"prompt", {{"type", "string"}, {"description", "Prompt to execute"}}},
            {"recurring", {{"type", "boolean"}, {"description", "Recurring (default true)"}}},
            {"durable", {{"type", "boolean"}, {"description", "Persist across restarts"}}}
        }}, {"required", {"cron", "prompt"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        std::string id = CronScheduler::getInstance().schedule(
            input["cron"].get<std::string>(),
            input["prompt"].get<std::string>(),
            input.value("recurring", true),
            input.value("durable", false)
        );
        return ToolResult::ok("Scheduled cron job: " + id);
    }
};

class CronDeleteTool : public Tool {
public:
    std::string getName() const override { return "CronDelete"; }
    std::string getDescription() const override { return "Cancel a scheduled cron job."; }
    std::string getCategory() const override { return "workflow"; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {
            {"id", {{"type", "string"}, {"description", "Cron job ID"}}}
        }}, {"required", {"id"}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        CronScheduler::getInstance().cancel(input["id"].get<std::string>());
        return ToolResult::ok("Cron job cancelled.");
    }
};

class CronListTool : public Tool {
public:
    std::string getName() const override { return "CronList"; }
    std::string getDescription() const override { return "List all scheduled cron jobs."; }
    std::string getCategory() const override { return "workflow"; }
    bool isReadOnly() const override { return true; }

    nlohmann::json getInputSchema() const override {
        return {{"type", "object"}, {"properties", {}}};
    }

    ToolResult call(ToolContext& ctx, const nlohmann::json&) override {
        auto jobs = CronScheduler::getInstance().list();
        if (jobs.empty()) return ToolResult::ok("No scheduled jobs.");
        std::string out;
        for (const auto& j : jobs) {
            out += j.id + " [" + j.cronExpr + "] " +
                   (j.recurring ? "recurring" : "one-shot") + ": " +
                   j.prompt.substr(0, 60) + "\n";
        }
        return ToolResult::ok(out);
    }
};

} // namespace closecrab
