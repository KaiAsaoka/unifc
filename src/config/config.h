/// @file config.h
/// @brief INI config parsing for unifc.
///
/// Parses a single INI file that defines everything about a model: protocol
/// type, register map, fan curve, and user preferences. Config files live
/// in the configs/ directory (e.g., configs/thinkpad_t480.ini).
///
/// Known keys are parsed into typed struct fields. Unknown keys are stored
/// in an extras map so protocol-specific or model-specific settings can be
/// passed through without modifying this struct.

#pragma once

#include "model/register_map.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief One row of the fan curve table.
///
/// Maps a temperature threshold to a fan speed level, with optional
/// hysteresis to prevent rapid on/off cycling near boundaries.
struct FanLevel {
    int temperature;      ///< Activation temperature in Celsius. -1 = terminator.
    int fan_speed;        ///< EC fan level (0-7). 0 = off, 7 = max.
    int hysteresis_up;    ///< Extra degrees above temperature needed to activate.
    int hysteresis_down;  ///< Extra degrees below temperature needed to deactivate.
};

/// @brief Operating mode for the fan controller.
enum class FanMode {
    kReadOnly = 0,   ///< Monitor temperatures only, never write to EC.
    kDisabled = 1,   ///< Fan control off, application idle.
    kSmart    = 2,   ///< Automatic control via fan curve lookup.
    kManual   = 3    ///< User-specified fixed fan level.
};

/// @brief Global keyboard shortcut definition.
struct HotkeyDef {
    int modifiers;    ///< Modifier combo: 1=Ctrl, 2=Shift, 3=Ctrl+Shift, 4=Alt, 5=Alt+Shift.
    char key;         ///< The key character (uppercase).
};

/// @brief All parsed configuration values from a unifc INI file.
///
/// Common fields that all models use are typed struct members. Everything
/// else (protocol-specific settings, model quirks, future extensions) goes
/// into the extras map. Components query extras for their own keys.
struct Config {
    // ---- Model identity ----
    std::string model;                             ///< Model name (e.g., "ThinkPad T480").
    std::string protocol;                          ///< Protocol type (e.g., "acpi_ec", "mock").

    // ---- Register map (loaded from config) ----
    RegisterMap register_map{};                    ///< Hardware register layout for this model.

    // ---- Fan control settings ----
    FanMode active_mode = FanMode::kSmart;         ///< Fan controller operating mode.
    int cycle_seconds = 5;                         ///< EC temperature poll interval in seconds.
    int manual_fan_speed = 0;                      ///< Default fan level for manual mode (0-7).
    int manual_mode_exit_temp = 78;                ///< Auto-exit manual mode above this temp (C).
    int max_read_errors = 10;                      ///< Consecutive EC read failures before BIOS fallback.

    // ---- Fan curves ----
    std::vector<FanLevel> fan_curve;               ///< Primary fan curve ("Level=" entries).
    std::vector<FanLevel> fan_curve2;              ///< Secondary fan curve ("Level2=" entries).

    // ---- Sensor overrides ----
    std::vector<std::string> ignore_sensors;       ///< Sensor names excluded from max-temp calculation.

    // ---- Hotkeys ----
    std::optional<HotkeyDef> hk_bios;
    std::optional<HotkeyDef> hk_smart;
    std::optional<HotkeyDef> hk_manual;

    // ---- Extensible key-value store ----
    /// Unknown or protocol-specific keys. Components can query this for
    /// their own settings (e.g., "ModulePath", "WmiNamespace").
    std::unordered_map<std::string, std::string> extras;
};

/// @brief Parse a unifc INI file into a Config struct.
///
/// Known keys are parsed into typed fields. Register map fields are parsed
/// into config.register_map. Unknown keys go into Config::extras.
/// @param filepath Path to the INI file.
/// @return The parsed configuration, or std::nullopt if the file cannot be opened.
std::optional<Config> LoadConfig(const std::string& filepath);

/// @brief Print all parsed config values to stdout.
/// @param cfg The configuration to print.
void PrintConfig(const Config& cfg);
