/// @file fan_controller.cpp
/// @brief Fan control state machine implementation.

#include "fan/fan_controller.h"
#include "model/laptop.h"

#include <algorithm>
#include <iostream>

namespace fan {

// ============================================================================
// Pure Logic Functions
// ============================================================================

int LookupFanLevel(int temperature, int current_level, const std::vector<FanLevel>& curve) {
    if (curve.empty()) return 0;

    int target_level = curve[0].fan_speed;

    for (size_t i = 0; i < curve.size(); ++i) {
        const auto& entry = curve[i];
        if (entry.temperature < 0) break;

        int threshold = entry.temperature;

        if (current_level < entry.fan_speed) {
            threshold += entry.hysteresis_up;
        } else if (current_level > entry.fan_speed) {
            threshold -= entry.hysteresis_down;
        }

        if (temperature >= threshold) {
            target_level = entry.fan_speed;
        }
    }

    return target_level;
}

int FindMaxTemperature(
    const std::vector<std::optional<int>>& temperatures,
    const std::vector<std::string>& sensor_names,
    const std::vector<std::string>& ignore_list
) {
    int max_temp = 0;

    for (size_t i = 0; i < temperatures.size(); ++i) {
        if (!temperatures[i]) continue;

        // Get sensor name
        std::string name;
        if (i < sensor_names.size() && !sensor_names[i].empty()) {
            name = sensor_names[i];
        }

        // Check ignore list
        bool ignored = false;
        for (const auto& ignore_name : ignore_list) {
            if (name == ignore_name) {
                ignored = true;
                break;
            }
        }

        if (!ignored) {
            max_temp = std::max(max_temp, *temperatures[i]);
        }
    }

    return max_temp;
}

// ============================================================================
// FanController
// ============================================================================

FanController::FanController(const Config& config, Laptop& laptop)
    : config_(config)
    , laptop_(laptop)
    , mode_(config.active_mode)
    , manual_level_(config.manual_fan_speed) {}

FanController::~FanController() {
    Stop();
}

bool FanController::Start() {
    if (running_.load(std::memory_order_acquire)) return false;

    running_.store(true, std::memory_order_release);
    poll_thread_ = std::thread(&FanController::PollLoop, this);
    return true;
}

void FanController::Stop() {
    bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    if (was_running && poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

bool FanController::IsRunning() const {
    return running_.load(std::memory_order_acquire);
}

void FanController::SetMode(FanMode mode) {
    FanMode old_mode = mode_.exchange(mode, std::memory_order_acq_rel);
    if (old_mode != mode) {
        if (mode == FanMode::kDisabled || mode == FanMode::kReadOnly) {
            ApplyBiosMode();
        }
        Notify(ControllerEvent::kModeChanged);
    }
}

FanMode FanController::GetMode() const {
    return mode_.load(std::memory_order_acquire);
}

void FanController::SetManualLevel(int level) {
    level = std::clamp(level, 0, 7);
    manual_level_.store(level, std::memory_order_release);
}

int FanController::GetManualLevel() const {
    return manual_level_.load(std::memory_order_acquire);
}

ControllerStatus FanController::GetStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

void FanController::SetEventCallback(EventCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    event_callback_ = std::move(callback);
}

// ============================================================================
// Worker Thread
// ============================================================================

void FanController::PollLoop() {
    PollOnce();

    while (running_.load(std::memory_order_acquire)) {
        if (!InterruptibleSleep()) break;
        PollOnce();
    }

    ApplyBiosMode();
}

void FanController::PollOnce() {
    auto temps = laptop_.ReadAllTemperatures();
    auto rpm = laptop_.ReadFanRpm();

    const auto& regs = laptop_.GetRegisterMap();
    int max_temp = FindMaxTemperature(temps, regs.sensor_names, config_.ignore_sensors);

    if (max_temp == 0) {
        HandleError();

        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            status_.temperatures = std::move(temps);
            status_.fan_rpm = rpm;
            status_.mode = mode_.load(std::memory_order_acquire);
            status_.current_fan_level = current_fan_level_;
            status_.max_temperature = 0;
            status_.consecutive_errors = consecutive_errors_;
            status_.timestamp = std::chrono::steady_clock::now();
        }
        Notify(ControllerEvent::kStatusUpdated);
        return;
    }

    ResetErrorCount();

    FanMode mode = mode_.load(std::memory_order_acquire);

    switch (mode) {
        case FanMode::kSmart:
            ApplySmartMode(max_temp);
            break;
        case FanMode::kManual:
            ApplyManualMode(max_temp);
            break;
        case FanMode::kDisabled:
        case FanMode::kReadOnly:
            break;
    }

    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.temperatures = std::move(temps);
        status_.fan_rpm = rpm;
        status_.mode = mode;
        status_.current_fan_level = current_fan_level_;
        status_.max_temperature = max_temp;
        status_.consecutive_errors = consecutive_errors_;
        status_.timestamp = std::chrono::steady_clock::now();
    }

    Notify(ControllerEvent::kStatusUpdated);
}

bool FanController::InterruptibleSleep() {
    constexpr int chunk_ms = 100;
    const int total_chunks = (config_.cycle_seconds * 1000) / chunk_ms;

    for (int i = 0; i < total_chunks; ++i) {
        if (!running_.load(std::memory_order_acquire)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(chunk_ms));
    }

    return true;
}

// ============================================================================
// Mode Handlers
// ============================================================================

void FanController::ApplySmartMode(int max_temp) {
    int target_level = LookupFanLevel(max_temp, current_fan_level_, config_.fan_curve);

    if (target_level != current_fan_level_) {
        if (laptop_.SetFanLevel(static_cast<uint8_t>(target_level))) {
            current_fan_level_ = target_level;
            Notify(ControllerEvent::kFanLevelChanged);
        } else {
            HandleError();
        }
    }
}

void FanController::ApplyManualMode(int max_temp) {
    if (max_temp > config_.manual_mode_exit_temp) {
        mode_.store(FanMode::kSmart, std::memory_order_release);
        Notify(ControllerEvent::kSafetyExit);
        Notify(ControllerEvent::kModeChanged);
        ApplySmartMode(max_temp);
        return;
    }

    int level = manual_level_.load(std::memory_order_acquire);

    if (level != current_fan_level_) {
        if (laptop_.SetFanLevel(static_cast<uint8_t>(level))) {
            current_fan_level_ = level;
            Notify(ControllerEvent::kFanLevelChanged);
        } else {
            HandleError();
        }
    }
}

void FanController::ApplyBiosMode() {
    if (laptop_.SetBiosMode()) {
        current_fan_level_ = -1;
    }
}

// ============================================================================
// Safety
// ============================================================================

void FanController::HandleError() {
    ++consecutive_errors_;

    if (consecutive_errors_ >= config_.max_read_errors) {
        ApplyBiosMode();
        mode_.store(FanMode::kDisabled, std::memory_order_release);
        Notify(ControllerEvent::kErrorThreshold);
        Notify(ControllerEvent::kModeChanged);
    }
}

void FanController::ResetErrorCount() {
    consecutive_errors_ = 0;
}

// ============================================================================
// Notification
// ============================================================================

void FanController::Notify(ControllerEvent event) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (event_callback_) {
        ControllerStatus status;
        {
            std::lock_guard<std::mutex> status_lock(status_mutex_);
            status = status_;
        }

        try {
            event_callback_(event, status);
        } catch (...) {
            // Don't let callback exceptions crash the worker thread
        }
    }
}

} // namespace fan
