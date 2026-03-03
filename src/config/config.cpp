/// @file config.cpp
/// @brief INI config parser implementation.
///
/// Reads a unifc INI file line by line. Known keys go into typed Config
/// fields. Register map keys populate Config::register_map. Everything
/// else goes into Config::extras.

#include "config/config.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// ============================================================================
// Helper functions
// ============================================================================

/// @brief Trim whitespace from both ends of a string.
static std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// @brief Check if a line is a comment or empty.
static bool IsComment(const std::string& line) {
    if (line.empty()) return true;
    char first = line[0];
    return first == '/' || first == '#' || first == ';';
}

/// @brief Parse a fan curve line into a FanLevel.
static std::optional<FanLevel> ParseFanLevel(const std::string& value) {
    std::istringstream iss(value);
    FanLevel level{};

    if (!(iss >> level.temperature >> level.fan_speed)) {
        return std::nullopt;
    }

    if (!(iss >> level.hysteresis_up)) level.hysteresis_up = 0;
    if (!(iss >> level.hysteresis_down)) level.hysteresis_down = 0;

    return level;
}

/// @brief Parse a hotkey definition.
static std::optional<HotkeyDef> ParseHotkey(const std::string& value) {
    std::istringstream iss(value);
    int modifiers = 0;
    char key = 0;

    if (!(iss >> modifiers >> key)) {
        return std::nullopt;
    }

    return HotkeyDef{modifiers, key};
}

/// @brief Parse a comma-separated list of strings.
static std::vector<std::string> ParseCommaList(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream iss(value);
    std::string token;

    while (std::getline(iss, token, ',')) {
        std::string trimmed = Trim(token);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }

    return result;
}

/// @brief Parse a comma-separated list of hex byte values.
///
/// Accepts formats like "0x78,0x79,0x7A" or "0x78, 0x79, 0x7A".
static std::vector<uint8_t> ParseHexByteList(const std::string& value) {
    std::vector<uint8_t> result;
    std::istringstream iss(value);
    std::string token;

    while (std::getline(iss, token, ',')) {
        std::string trimmed = Trim(token);
        if (!trimmed.empty()) {
            unsigned long val = std::stoul(trimmed, nullptr, 0);
            result.push_back(static_cast<uint8_t>(val));
        }
    }

    return result;
}

/// @brief Parse a single hex or decimal byte value.
static uint8_t ParseByte(const std::string& value) {
    return static_cast<uint8_t>(std::stoul(value, nullptr, 0));
}

/// @brief Validate that a fan curve is properly formed.
static bool ValidateFanCurve(const std::vector<FanLevel>& curve) {
    if (curve.empty()) return false;
    if (curve.back().temperature != -1) return false;

    for (size_t i = 1; i + 1 < curve.size(); ++i) {
        if (curve[i].temperature <= curve[i - 1].temperature) return false;
    }

    return true;
}

// ============================================================================
// Public API
// ============================================================================

std::optional<Config> LoadConfig(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return std::nullopt;
    }

    Config cfg;
    std::string line;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (IsComment(line)) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));

        // ---- Model identity ----
        if (key == "Model") {
            cfg.model = value;
        } else if (key == "Protocol") {
            cfg.protocol = value;
        }
        // ---- Register map ----
        else if (key == "FanControl") {
            cfg.register_map.fan_control = ParseByte(value);
        } else if (key == "FanSelector") {
            cfg.register_map.fan_selector = ParseByte(value);
        } else if (key == "BiosModeBit") {
            cfg.register_map.bios_mode_bit = ParseByte(value);
        } else if (key == "BiosModeValue") {
            cfg.register_map.bios_mode_value = ParseByte(value);
        } else if (key == "FanRpmLo") {
            cfg.register_map.fan_rpm_lo = ParseByte(value);
        } else if (key == "FanRpmHi") {
            cfg.register_map.fan_rpm_hi = ParseByte(value);
        } else if (key == "TempSensors") {
            cfg.register_map.temp_sensors = ParseHexByteList(value);
        } else if (key == "SensorNames") {
            cfg.register_map.sensor_names = ParseCommaList(value);
        } else if (key == "TempMin") {
            cfg.register_map.temp_min = ParseByte(value);
        } else if (key == "TempMax") {
            cfg.register_map.temp_max = ParseByte(value);
        } else if (key == "NumFans") {
            cfg.register_map.num_fans = ParseByte(value);
        }
        // ---- Fan control settings ----
        else if (key == "Active") {
            cfg.active_mode = static_cast<FanMode>(std::stoi(value));
        } else if (key == "Cycle") {
            cfg.cycle_seconds = std::stoi(value);
        } else if (key == "ManFanSpeed") {
            cfg.manual_fan_speed = std::stoi(value);
        } else if (key == "ManModeExit") {
            cfg.manual_mode_exit_temp = std::stoi(value);
        } else if (key == "MaxReadErrors") {
            cfg.max_read_errors = std::stoi(value);
        }
        // ---- Fan curves ----
        else if (key == "Level") {
            auto level = ParseFanLevel(value);
            if (level) cfg.fan_curve.push_back(*level);
        } else if (key == "Level2") {
            auto level = ParseFanLevel(value);
            if (level) cfg.fan_curve2.push_back(*level);
        }
        // ---- Sensor overrides ----
        else if (key == "IgnoreSensors") {
            cfg.ignore_sensors = ParseCommaList(value);
        }
        // ---- Hotkeys ----
        else if (key == "HK_BIOS") {
            cfg.hk_bios = ParseHotkey(value);
        } else if (key == "HK_Smart") {
            cfg.hk_smart = ParseHotkey(value);
        } else if (key == "HK_Manual") {
            cfg.hk_manual = ParseHotkey(value);
        }
        // ---- Unknown keys go to extras ----
        else {
            cfg.extras[key] = value;
        }
    }

    // Validate fan curves
    if (!cfg.fan_curve.empty() && !ValidateFanCurve(cfg.fan_curve)) {
        std::cerr << "Warning: primary fan curve is malformed\n";
    }
    if (!cfg.fan_curve2.empty() && !ValidateFanCurve(cfg.fan_curve2)) {
        std::cerr << "Warning: secondary fan curve is malformed\n";
    }

    return cfg;
}

/// @brief Convert a FanMode enum to a display string.
static const char* FanModeStr(FanMode mode) {
    switch (mode) {
        case FanMode::kReadOnly: return "ReadOnly";
        case FanMode::kDisabled: return "Disabled";
        case FanMode::kSmart:    return "Smart";
        case FanMode::kManual:   return "Manual";
        default:                 return "Unknown";
    }
}

void PrintConfig(const Config& cfg) {
    std::cout << "=== unifc Configuration ===\n\n";

    std::cout << "Model:          " << cfg.model << "\n";
    std::cout << "Protocol:       " << cfg.protocol << "\n";
    std::cout << "Mode:           " << FanModeStr(cfg.active_mode) << "\n";
    std::cout << "Cycle:          " << cfg.cycle_seconds << "s\n";
    std::cout << "ManFanSpeed:    " << cfg.manual_fan_speed << "\n";
    std::cout << "ManModeExit:    " << cfg.manual_mode_exit_temp << "C\n";
    std::cout << "MaxReadErrors:  " << cfg.max_read_errors << "\n";

    // Register map
    std::cout << "\n--- Register Map ---\n";
    std::cout << "FanControl:     0x" << std::hex << static_cast<int>(cfg.register_map.fan_control) << "\n";
    std::cout << "FanSelector:    0x" << static_cast<int>(cfg.register_map.fan_selector) << "\n";
    std::cout << "BiosModeBit:    " << std::dec << static_cast<int>(cfg.register_map.bios_mode_bit) << "\n";
    std::cout << "BiosModeValue:  0x" << std::hex << static_cast<int>(cfg.register_map.bios_mode_value) << "\n";
    std::cout << "FanRpmLo:       0x" << static_cast<int>(cfg.register_map.fan_rpm_lo) << "\n";
    std::cout << "FanRpmHi:       0x" << static_cast<int>(cfg.register_map.fan_rpm_hi) << "\n";
    std::cout << "NumFans:        " << std::dec << static_cast<int>(cfg.register_map.num_fans) << "\n";
    std::cout << "TempMin:        " << static_cast<int>(cfg.register_map.temp_min) << "\n";
    std::cout << "TempMax:        " << static_cast<int>(cfg.register_map.temp_max) << "\n";

    std::cout << "TempSensors:    ";
    for (size_t i = 0; i < cfg.register_map.temp_sensors.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "0x" << std::hex << static_cast<int>(cfg.register_map.temp_sensors[i]);
    }
    std::cout << std::dec << "\n";

    std::cout << "SensorNames:    ";
    for (size_t i = 0; i < cfg.register_map.sensor_names.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << cfg.register_map.sensor_names[i];
    }
    std::cout << "\n";

    // Fan curve
    std::cout << "\n--- Fan Curve ---\n";
    if (cfg.fan_curve.empty()) {
        std::cout << "  (none)\n";
    } else {
        std::cout << "  Temp  Fan  HystUp  HystDown\n";
        for (const auto& level : cfg.fan_curve) {
            if (level.temperature == -1) {
                std::cout << "  [terminator]\n";
            } else {
                std::cout << "  " << level.temperature << "C"
                          << "    " << level.fan_speed
                          << "    " << level.hysteresis_up
                          << "       " << level.hysteresis_down << "\n";
            }
        }
    }

    // Ignored sensors
    if (!cfg.ignore_sensors.empty()) {
        std::cout << "\n--- Ignored Sensors ---\n  ";
        for (size_t i = 0; i < cfg.ignore_sensors.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << cfg.ignore_sensors[i];
        }
        std::cout << "\n";
    }

    // Extras
    if (!cfg.extras.empty()) {
        std::cout << "\n--- Extra Settings ---\n";
        for (const auto& [k, v] : cfg.extras) {
            std::cout << "  " << k << " = " << v << "\n";
        }
    }
}
