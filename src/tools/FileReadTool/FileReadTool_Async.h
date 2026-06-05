#pragma once

// 异步文件读取工具（可选，需充分测试）
// 使用 C++20 jthread + 线程池实现并发读取

#include "../Tool.h"
#include "FileReadTool_Enhanced.h"
#include <thread>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace closecrab {
// Thread pool for async file operations
class FileReadThreadPool {
public:
    static FileReadThreadPool& getInstance() {
        static FileReadThreadPool instance(4); // 4 worker threads
        return instance;
    }

    template<typename F>
    auto enqueue(F&& task) -> std::future<typename std::invoke_result<F>::type> {
        using return_type = typename std::invoke_result<F>::type;

        auto packagedTask = std::make_shared<std::packaged_task<return_type()>>(
            std::forward<F>(task)
        );

        std::future<return_type> result = packagedTask->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex_);

            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            tasks_.emplace([packagedTask]() { (*packagedTask)(); });
        }

      condition_.notify_one();
        return result;
    }

    void shutdown() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    size_t queueSize() const {
      std::unique_lock<std::mutex> lock(queueMutex_);
      return tasks_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};

    explicit FileReadThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] {
           while (true) {
           std::function<void()> task;

        {
                        std::unique_lock<std::mutex> lock(queueMutex_);
                    condition_.wait(lock, [this] {
                      return stop_ || !tasks_.empty();
                 });

                  if (stop_ && tasks_.empty()) {
                         return;
                 }

                    task = std::move(tasks_.front());
              tasks_.pop();
                    }

                    try {
               task();
                    } catch (const std::exception& e) {
                   // Log error but don't crash thread
                 // TODO: Add proper logging
                 } catch (...) {
               // Catch all exceptions to prevent thread termination
                }
                }
            });
        }
    }

    ~FileReadThreadPool() {
        shutdown();
    }
};

// Async version of FileReadTool (inherits from Enhanced)
// Uses thread pool for parallel reads
class FileReadToolAsync : public FileReadToolEnhanced {
public:
    std::string getName() const override { return "Read"; }

    // Override call() to add async support
    ToolResult call(ToolContext& ctx, const nlohmann::json& input) override {
        // Check if this is a batch read request (multiple files)
        // For single file, fall back to sync version (no overhead)

        // For now, always use sync version for safety
        // Future: detect batch operations and use thread pool
        return FileReadToolEnhanced::call(ctx, input);
    }

    // New API: batch read multiple files concurrently
    std::vector<ToolResult> callBatch(ToolContext& ctx,
                    const std::vector<nlohmann::json>& inputs) {
        if (inputs.size() == 1) {
            // Single file: no async benefit
            return {FileReadToolEnhanced::call(ctx, inputs[0])};
     }

     auto& pool = FileReadThreadPool::getInstance();
        std::vector<std::future<ToolResult>> futures;

        // Enqueue all read tasks
        for (const auto& input : inputs) {
          // CRITICAL: Copy all data to avoid dangling references
            // The lambda captures by value to ensure lifetime safety
          std::string pathCopy = input["file_path"].get<std::string>();
            nlohmann::json inputCopy = input;

            futures.push_back(pool.enqueue([this, inputCopy]() -> ToolResult {
                try {
                    // Create a new context for this thread (thread-local state)
                    ToolContext threadCtx;
           // SAFETY: Don't share readFileState across threads
               // Each thread gets its own context

                    return this->FileReadToolEnhanced::call(threadCtx, inputCopy);
             } catch (const std::exception& e) {
              return ToolResult::fail("Async read error: " + std::string(e.what()));
                } catch (...) {
             return ToolResult::fail("Unknown async read error");
                }
            }));
        }

        // Collect results
        std::vector<ToolResult> results;
        results.reserve(futures.size());

        for (auto& future : futures) {
            try {
           results.push_back(future.get());
            } catch (const std::exception& e) {
             results.push_back(ToolResult::fail("Future error: " + std::string(e.what())));
          }
        }

        return results;
    }

    // Get async stats
    size_t getQueueSize() const {
        return FileReadThreadPool::getInstance().queueSize();
    }
};

} // namespace closecrab
