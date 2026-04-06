#pragma once

#include "../Tool.h"
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <regex>
#include <atomic>

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

private:
    CronScheduler() = default;

    static std::chrono::system_clock::time_point computeNextFire(const std::string&) {
        // Simplified: fire in 1 minute from now
        return std::chrono::system_clock::now() + std::chrono::minutes(1);
    }

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
