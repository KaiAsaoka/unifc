/// @file register_map.h
/// @brief RegisterMap struct definition.
///
/// A RegisterMap defines what EC register offsets mean for a specific laptop
/// model. This is pure data - no behavior. Different models using the same
/// protocol (e.g., two ThinkPads both using ACPI EC) differ only in their
/// register maps.
///
/// Register maps are loaded from config files at runtime. See config.h for
/// the INI format.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// @brief Defines the register layout for a specific laptop model.
///
/// All register offsets and validation thresholds needed to read temperatures,
/// fan RPM, and control fan speed on a particular model. All fields are
/// populated by the config parser from an INI file.
struct RegisterMap {
    uint8_t fan_control;        ///< Fan speed control register (e.g., 0x2F).
    uint8_t fan_selector;       ///< Fan selector for multi-fan (e.g., 0x31). 0 if N/A.
    uint8_t bios_mode_bit;      ///< Bit position for BIOS mode flag (e.g., 7).
    uint8_t bios_mode_value;    ///< Value to write for BIOS mode (e.g., 0x80).
    uint8_t fan_rpm_lo;         ///< Fan RPM low byte register (e.g., 0x84).
    uint8_t fan_rpm_hi;         ///< Fan RPM high byte register (e.g., 0x85).

    std::vector<uint8_t> temp_sensors;     ///< Temperature sensor register offsets.
    std::vector<std::string> sensor_names; ///< Default name for each sensor.

    uint8_t temp_min;           ///< Minimum valid temperature (filter threshold).
    uint8_t temp_max;           ///< Maximum valid temperature (filter threshold).
    uint8_t num_fans;           ///< Number of independently controllable fans.
};
