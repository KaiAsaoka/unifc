/// @file fan_write_strategy.h
/// @brief Fan write strategy interface and implementations.
///
/// Different laptop models need different sequences to set fan speed.
/// Single-fan systems just write a byte to a register. Dual-fan systems
/// need a select-write-delay-verify handshake. The strategy pattern lets
/// the Laptop class delegate this without knowing the details.

#pragma once

#include <cstdint>

class Protocol;
struct RegisterMap;

/// @brief Abstract interface for writing fan speed to hardware.
class FanWriteStrategy {
public:
    virtual ~FanWriteStrategy() = default;

    /// @brief Write a fan speed level to the hardware.
    /// @param proto The protocol to use for register access.
    /// @param regs The register map for this model.
    /// @param level Fan speed level (0-7). Implementations should clamp if needed.
    /// @return true on success.
    virtual bool WriteFanLevel(Protocol& proto, const RegisterMap& regs,
                               uint8_t level) = 0;

    /// @brief Set BIOS/automatic mode (EC firmware controls the fan).
    /// @param proto The protocol to use for register access.
    /// @param regs The register map for this model.
    /// @return true on success.
    virtual bool SetBiosMode(Protocol& proto, const RegisterMap& regs) = 0;
};

/// @brief Single-fan write: just write the level to the fan control register.
class SingleFanWrite : public FanWriteStrategy {
public:
    bool WriteFanLevel(Protocol& proto, const RegisterMap& regs,
                       uint8_t level) override;
    bool SetBiosMode(Protocol& proto, const RegisterMap& regs) override;
};

/// @brief Dual-fan handshake for systems with two independently controlled fans.
///
/// Sequence: select fan 1 -> write level -> sleep 100ms -> select fan 2 ->
/// write level -> sleep 100ms -> verify readback -> retry up to 5 times.
class DualFanHandshake : public FanWriteStrategy {
public:
    bool WriteFanLevel(Protocol& proto, const RegisterMap& regs,
                       uint8_t level) override;
    bool SetBiosMode(Protocol& proto, const RegisterMap& regs) override;
};
