// AraExcev.h
#pragma once
#include <string>
#include <functional>
#include <memory>
#include <iostream>
#include <thread>
#include <atomic>

namespace ara {
namespace excev {

// Application states
enum class AppState {
    kRegistered,
    kRunning,
    kStopped,
    kTerminated,
    kCrashed
};

// Simple execution client
class ApplicationClient {
    std::string app_id_;
    std::function<void()> stop_handler_;
    std::atomic<AppState> state_;
    std::thread monitor_thread_;
    bool auto_restart_;

public:
    ApplicationClient(const std::string& appId, bool autoRestart = true)
        : app_id_(appId), state_(AppState::kStopped), auto_restart_(autoRestart) {}

    // Register with Exec Manager
    bool RegisterApplication() {
        std::cout << "[ExecM] Register app: " << app_id_ << std::endl;
        state_ = AppState::kRegistered;
        return true;
    }

    // Set callback when ExecM sends stop signal
    void SetStopHandler(std::function<void()> cb) {
        stop_handler_ = cb;
    }

    // Start application
    void Start() {
        std::cout << "[ExecM] Start app: " << app_id_ << std::endl;
        state_ = AppState::kRunning;
#ifdef EXCEV_SIMULATE_CRASH   // <-- chỉ bật khi muốn demo tự crash
        // Monitor thread simulates ExecM watchdog
        monitor_thread_ = std::thread([this]() {
            while (state_ == AppState::kRunning) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                // Giả lập crash
                std::cerr << "[ExecM] Crash detected in app " << app_id_ << std::endl;
                state_ = AppState::kCrashed;
                if (auto_restart_) {
                    Restart();
                }
                break;
            }
        });
        monitor_thread_.detach();
    #endif
    }

    // Stop application
    void Stop() {
        std::cout << "[ExecM] Stop app: " << app_id_ << std::endl;
        state_ = AppState::kStopped;
        if (stop_handler_) stop_handler_();
    }

    // Restart application
    void Restart() {
        std::cout << "[ExecM] Restarting app: " << app_id_ << std::endl;
        Stop();
        Start();
    }

    void Crash() {
        std::cerr << "[ExecM] Crash detected in " << app_id_ << std::endl;
        state_ = AppState::kCrashed;
        if (auto_restart_) Restart();
    }

    AppState GetState() const { return state_; }
};

} // namespace excev
} // namespace ara
