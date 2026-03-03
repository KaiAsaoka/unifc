/// @file laptop.h
/// @brief Laptop class - composes Protocol + RegisterMap + FanWriteStrategy.
///
/// A Laptop is a concrete class that assembles the right components for a
/// specific model. It provides a hardware-agnostic API for the fan controller:
/// read temperatures, read RPM, set fan level, set BIOS mode.

#pragma once

#include "model/register_map.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class Protocol;
class FanWriteStrategy;

/// @brief A configured laptop: protocol + register map + write strategy.
///
/// Owns the Protocol and FanWriteStrategy. Provides validated temperature
/// reading (noise rejection, range filtering) and fan control that delegates
/// to the strategy.
class Laptop {
public:
    /// @brief Construct a Laptop from its components.
    /// @param protocol The hardware access protocol (takes ownership).
    /// @param register_map The register layout for this model.
    /// @param strategy The fan write strategy (takes ownership).
    Laptop(std::unique_ptr<Protocol> protocol,
           RegisterMap register_map,
           std::unique_ptr<FanWriteStrategy> strategy);

    ~Laptop();

    // Move-only (owns unique_ptrs)
    Laptop(Laptop&&) noexcept;
    Laptop& operator=(Laptop&&) noexcept;
    Laptop(const Laptop&) = delete;
    Laptop& operator=(const Laptop&) = delete;

    /// @brief Initialize the hardware protocol.
    /// @return true on success.
    bool Init();

    /// @brief Set BIOS mode and shut down the protocol.
    void Shutdown();

    /// @brief Read a temperature sensor with validation and noise rejection.
    ///
    /// Performs two consecutive reads and requires values within 2 degrees
    /// (noise rejection). Rejects values of 0x00, 0x80, and anything outside
    /// the temp_min/temp_max range from the register map.
    /// @param offset The sensor register offset.
    /// @return Temperature in Celsius, or std::nullopt if invalid.
    std::optional<int> ReadTemperature(uint8_t offset);

    /// @brief Read all temperature sensors defined in the register map.
    /// @return Vector of optional temperature values (Celsius), one per sensor.
    std::vector<std::optional<int>> ReadAllTemperatures();

    /// @brief Read the current fan speed in RPM.
    /// @return Fan RPM, or std::nullopt on failure or error code.
    std::optional<uint16_t> ReadFanRpm();

    /// @brief Set the fan to a specific speed level.
    /// @param level Fan speed level (0-7).
    /// @return true on success.
    bool SetFanLevel(uint8_t level);

    /// @brief Set fan to BIOS/automatic mode.
    /// @return true on success.
    bool SetBiosMode();

    /// @brief Detect whether a second fan is present.
    ///
    /// Writes to the fan selector register and reads back to verify.
    /// Uses BeginTransaction for atomicity.
    /// @return true if a second fan responded.
    bool DetectDualFan();

    /// @brief Get the register map for this model.
    /// @return Const reference to the register map.
    const RegisterMap& GetRegisterMap() const;

    /// @brief Get the underlying protocol (for signal handler cleanup).
    /// @return Pointer to the protocol, or nullptr.
    Protocol* GetProtocol();

private:
    std::unique_ptr<Protocol> protocol_;
    RegisterMap register_map_;
    std::unique_ptr<FanWriteStrategy> strategy_;
};
