/// @file main.cpp
/// @brief Entry point for unifc.

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#endif

#include "config/config.h"
#include "fan/fan_controller.h"
#include "model/laptop.h"
#include "model/laptop_factory.h"

/// @brief Global Laptop pointer for cleanup in signal handlers.
static Laptop* g_laptop = nullptr;

/// @brief Global FanController pointer for cleanup in signal handlers.
static fan::FanController* g_controller = nullptr;

/// @brief Restore BIOS fan control on normal exit.
static void CleanupAtexit() {
    if (g_controller) {
        g_controller->Stop();
    }
    if (g_laptop) {
        g_laptop->Shutdown();
    }
}

/// @brief Restore BIOS fan control on signal (Ctrl+C, termination).
static void CleanupSignal(int sig) {
    if (g_laptop) {
        g_laptop->SetBiosMode();
    }
    std::_Exit(128 + sig);
}

/// @brief Convert FanMode enum to display string.
static const char* ModeToString(FanMode mode) {
    switch (mode) {
        case FanMode::kReadOnly: return "ReadOnly";
        case FanMode::kDisabled: return "Disabled";
        case FanMode::kSmart:    return "Smart";
        case FanMode::kManual:   return "Manual";
        default:                 return "Unknown";
    }
}

/// @brief Print a single-line status update.
static void PrintStatusLine(const fan::ControllerStatus& status) {
    std::cout << "\r"
              << "Temp: " << std::setw(3) << status.max_temperature << "C"
              << " | Fan: ";

    if (status.current_fan_level < 0) {
        std::cout << "BIOS";
    } else {
        std::cout << "L" << status.current_fan_level;
    }

    std::cout << " | RPM: ";
    if (status.fan_rpm) {
        std::cout << std::setw(4) << *status.fan_rpm;
    } else {
        std::cout << "----";
    }

    std::cout << " | Mode: " << std::setw(8) << ModeToString(status.mode);

    if (status.consecutive_errors > 0) {
        std::cout << " | Errors: " << status.consecutive_errors;
    }

    std::cout << "     " << std::flush;
}

/// @brief Print the full sensor table.
static void PrintSensorTable(const fan::ControllerStatus& status, const Config& cfg) {
    const auto& regs = cfg.register_map;

    std::cout << "\n=== Sensor Readings ===\n\n";
    std::cout << std::left
              << std::setw(10) << "Sensor"
              << std::setw(8)  << "Name"
              << std::setw(8)  << "Temp"
              << "\n";
    std::cout << std::string(26, '-') << "\n";

    int sensor_count = static_cast<int>(regs.temp_sensors.size());
    for (int i = 0; i < sensor_count; ++i) {
        std::cout << std::left << std::setw(10) << (i + 1);

        // Sensor name from register map
        if (i < static_cast<int>(regs.sensor_names.size())) {
            std::cout << std::setw(8) << regs.sensor_names[i];
        } else {
            std::cout << std::setw(8) << "---";
        }

        // Temperature
        if (i < static_cast<int>(status.temperatures.size()) && status.temperatures[i]) {
            std::cout << *status.temperatures[i] << "C";
        } else {
            std::cout << "--";
        }
        std::cout << "\n";
    }

    std::cout << "\nFan RPM: ";
    if (status.fan_rpm) {
        std::cout << *status.fan_rpm << "\n";
    } else {
        std::cout << "-- (read failed)\n";
    }
}

/// @brief Check if a key has been pressed (non-blocking).
static bool KeyAvailable() {
#ifdef _WIN32
    return _kbhit() != 0;
#else
    return false;
#endif
}

/// @brief Consume a pressed key.
static int GetKey() {
#ifdef _WIN32
    return _getch();
#else
    return getchar();
#endif
}

int main(int argc, char* argv[]) {
    std::string config_path = "unifc.ini";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::cout << "Loading config from: " << config_path << "\n\n";

    auto cfg = LoadConfig(config_path);
    if (!cfg) {
        std::cerr << "Failed to load config file: " << config_path << "\n";
        return 1;
    }

    PrintConfig(*cfg);

    // Assemble the laptop from config
    auto laptop = CreateLaptop(*cfg);
    if (!laptop) {
        std::cerr << "Failed to create laptop (unknown protocol: "
                  << cfg->protocol << ")\n";
        return 1;
    }

    g_laptop = &*laptop;

    // Register cleanup handlers
    std::atexit(CleanupAtexit);
    std::signal(SIGINT, CleanupSignal);
    std::signal(SIGTERM, CleanupSignal);

    // Initialize hardware
    if (!laptop->Init()) {
        std::cerr << "Failed to initialize hardware protocol.\n";
        return 1;
    }

    // Detect dual fan
    bool has_dual_fan = laptop->DetectDualFan();
    std::cout << "\nHardware initialized.";
    if (has_dual_fan) {
        std::cout << " Dual-fan system detected.";
    }
    std::cout << "\n";

    // Create and start fan controller
    fan::FanController controller(*cfg, *laptop);
    g_controller = &controller;

    if (!controller.Start()) {
        std::cerr << "Failed to start fan controller.\n";
        return 1;
    }

    std::cout << "\nFan controller started in " << ModeToString(cfg->active_mode)
              << " mode.\n";
    std::cout << "Press 'q' to quit, 'b' for BIOS mode, 's' for Smart mode, "
              << "'m' for Manual mode.\n";
    std::cout << "Press 'v' for verbose sensor table.\n\n";

    // Main display loop
    bool running = true;
    while (running) {
        auto status = controller.GetStatus();
        PrintStatusLine(status);

        if (KeyAvailable()) {
            int key = GetKey();
            switch (key) {
                case 'q': case 'Q':
                    running = false;
                    break;
                case 'b': case 'B':
                    controller.SetMode(FanMode::kDisabled);
                    std::cout << "\n\n>>> Switched to BIOS/Disabled mode <<<\n\n";
                    break;
                case 's': case 'S':
                    controller.SetMode(FanMode::kSmart);
                    std::cout << "\n\n>>> Switched to Smart mode <<<\n\n";
                    break;
                case 'm': case 'M':
                    controller.SetMode(FanMode::kManual);
                    std::cout << "\n\n>>> Switched to Manual mode (level "
                              << controller.GetManualLevel() << ") <<<\n\n";
                    break;
                case 'v': case 'V':
                    std::cout << "\n";
                    PrintSensorTable(status, *cfg);
                    std::cout << "\n";
                    break;
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7': {
                    int level = key - '0';
                    controller.SetManualLevel(level);
                    if (controller.GetMode() == FanMode::kManual) {
                        std::cout << "\n\n>>> Manual level set to "
                                  << level << " <<<\n\n";
                    } else {
                        std::cout << "\n\n>>> Manual level set to " << level
                                  << " (switch to Manual mode with 'm' to apply) <<<\n\n";
                    }
                    break;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n\nStopping fan controller...\n";
    controller.Stop();
    g_controller = nullptr;

    std::cout << "BIOS fan mode restored. Exiting.\n";

    g_laptop = nullptr;
    laptop->Shutdown();
    return 0;
}
