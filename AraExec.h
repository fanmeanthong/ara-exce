// AraExec.h
#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <iostream>

/**
 * Chuẩn hoá: ara::exec thay vì ara::excev.
 * - API giữ nguyên như ApplicationClient trước đây.
 * - Có macro EXCEV_SIMULATE_CRASH để demo auto-crash 5s (mặc định tắt).
 * - Có alias ara::excev để không phá vỡ code cũ.
 */
namespace ara {
namespace exec {

enum class AppState { kRegistered, kRunning, kStopped, kTerminated, kCrashed };

class ApplicationClient {
    std::string app_id_;
    std::function<void()> stop_handler_;
    std::atomic<AppState> state_{AppState::kStopped};
    std::thread monitor_thread_;
    bool auto_restart_{true};

public:
    explicit ApplicationClient(const std::string& appId, bool autoRestart = true)
        : app_id_(appId), auto_restart_(autoRestart) {}

    bool RegisterApplication() {
        std::cout << "[ExecM] Register app: " << app_id_ << std::endl;
        state_ = AppState::kRegistered;
        return true;
    }

    void SetStopHandler(std::function<void()> cb) { stop_handler_ = std::move(cb); }

    void Start() {
        std::cout << "[ExecM] Start app: " << app_id_ << std::endl;
        state_ = AppState::kRunning;

#if defined(EXCEV_SIMULATE_CRASH)
        // Demo: tự crash sau 5s để trình diễn RestartPolicy
        monitor_thread_ = std::thread([this]() {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(5s);
            if (state_ == AppState::kRunning) {
                std::cerr << "[ExecM] Crash detected in app " << app_id_ << std::endl;
                state_ = AppState::kCrashed;
                if (auto_restart_) Restart();
            }
        });
        monitor_thread_.detach();
#endif
    }

    void Stop() {
        std::cout << "[ExecM] Stop app: " << app_id_ << std::endl;
        state_ = AppState::kStopped;
        if (stop_handler_) stop_handler_();
    }

    void Restart() {
        std::cout << "[ExecM] Restarting app: " << app_id_ << std::endl;
        Stop();
        Start();
    }

    // Cho phép crash “có chủ đích” từ luồng nghiệp vụ
    void Crash() {
        std::cerr << "[ExecM] Crash detected in " << app_id_ << std::endl;
        state_ = AppState::kCrashed;
        if (auto_restart_) Restart();
    }

    AppState GetState() const { return state_; }
};

} // namespace exec

// ====== Giữ tương thích ngược: ara::excev là alias của ara::exec ======
namespace excev {
using AppState = ::ara::exec::AppState;
using ApplicationClient = ::ara::exec::ApplicationClient;
} // namespace excev

} // namespace ara
