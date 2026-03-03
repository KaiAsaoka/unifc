/// @file fan_controller.h
/// @brief Fan control state machine with temperature-based speed adjustment.
///
/// Hardware-agnostic: communicates through the Laptop class, which handles
/// protocol and register map details. Implements BIOS/Smart/Manual fan
/// control modes with safety features.

#pragma once

#include "config/config.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class Laptop;

namespace fan {

/// @brief Snapshot of current sensor readings and controller state.
struct ControllerStatus {
    std::vector<std::optional<int>> temperatures;  ///< All sensor readings (Celsius).
    std::optional<uint16_t> fan_rpm;               ///< Current fan speed, or nullopt on failure.
    FanMode mode;                                  ///< Current operating mode.
    int current_fan_level;                         ///< Last written fan level (0-7), or -1 for BIOS.
    int max_temperature;                           ///< Highest valid temperature across active sensors.
    int consecutive_errors;                        ///< Count of consecutive read failures.
    std::chrono::steady_clock::time_point timestamp;
};

/// @brief Event types for controller state change notifications.
enum class ControllerEvent {
    kStatusUpdated,      ///< New temperature/RPM data available.
    kModeChanged,        ///< Operating mode changed.
    kFanLevelChanged,    ///< Fan speed level changed.
    kErrorThreshold,     ///< Consecutive errors reached max, fell back to BIOS.
    kSafetyExit          ///< Manual mode auto-exited due to high temperature.
};

/// @brief Callback type for controller events. Called from the worker thread.
using EventCallback = std::function<void(ControllerEvent event, const ControllerStatus& status)>;

// ============================================================================
// Pure Logic Functions
// ============================================================================

/// @brief Look up the fan level for a given temperature using hysteresis.
/// @param temperature Current maximum temperature in Celsius.
/// @param current_level The currently active fan level (for hysteresis).
/// @param curve The fan curve table (sorted ascending, terminated with temp=-1).
/// @return The target fan level (0-7).
int LookupFanLevel(int temperature, int current_level, const std::vector<FanLevel>& curve);

/// @brief Find the maximum valid temperature from sensor readings.
/// @param temperatures Sensor readings.
/// @param sensor_names Names corresponding to each sensor index.
/// @param ignore_list Sensor names to exclude.
/// @return The highest valid temperature, or 0 if none.
int FindMaxTemperature(
    const std::vector<std::optional<int>>& temperatures,
    const std::vector<std::string>& sensor_names,
    const std::vector<std::string>& ignore_list
);

// ============================================================================
// FanController
// ============================================================================

/// @brief Fan control state machine with background polling thread.
///
/// Manages fan speed based on temperature readings and the configured mode.
/// Runs a background thread that polls hardware every cycle_seconds.
class FanController {
public:
    /// @brief Construct a fan controller.
    /// @param config The parsed configuration.
    /// @param laptop Reference to the Laptop for hardware access.
    FanController(const Config& config, Laptop& laptop);
    ~FanController();

    FanController(const FanController&) = delete;
    FanController& operator=(const FanController&) = delete;
    FanController(FanController&&) = delete;
    FanController& operator=(FanController&&) = delete;

    /// @brief Start the background polling thread.
    /// @return true if started successfully.
    bool Start();

    /// @brief Stop the background thread and restore BIOS mode.
    void Stop();

    /// @brief Check if the controller is currently running.
    bool IsRunning() const;

    /// @brief Change the operating mode. Thread-safe.
    void SetMode(FanMode mode);

    /// @brief Get the current operating mode.
    FanMode GetMode() const;

    /// @brief Set the manual mode fan level (0-7). Thread-safe.
    void SetManualLevel(int level);

    /// @brief Get the current manual mode level setting.
    int GetManualLevel() const;

    /// @brief Get a snapshot of the current controller status. Thread-safe.
    ControllerStatus GetStatus() const;

    /// @brief Register a callback for controller events.
    void SetEventCallback(EventCallback callback);

private:
    void PollLoop();
    void PollOnce();
    bool InterruptibleSleep();

    void ApplySmartMode(int max_temp);
    void ApplyManualMode(int max_temp);
    void ApplyBiosMode();

    void HandleError();
    void ResetErrorCount();
    void Notify(ControllerEvent event);

    Config config_;
    Laptop& laptop_;

    std::atomic<FanMode> mode_;
    std::atomic<bool> running_{false};
    std::atomic<int> manual_level_{0};

    int current_fan_level_{-1};
    int consecutive_errors_{0};

    mutable std::mutex status_mutex_;
    ControllerStatus status_{};

    std::mutex callback_mutex_;
    EventCallback event_callback_;

    std::thread poll_thread_;
};

} // namespace fan
