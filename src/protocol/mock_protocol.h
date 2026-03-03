/// @file mock_protocol.h
/// @brief Register-level mock protocol for testing without hardware.
///
/// Always compiled in (no build flag needed). Provides a simple map-based
/// register store that can be pre-populated with values matching any model's
/// RegisterMap.

#pragma once

#include "protocol/protocol.h"

#include <unordered_map>

/// @brief Mock protocol that stores registers in an in-memory map.
///
/// ReadByte returns the stored value for a register offset (or 0x80 if
/// not present). WriteByte stores a value. No hardware access occurs.
///
/// Construct with an initial register map to simulate a specific model,
/// or use the default constructor for an empty register set.
class MockProtocol : public Protocol {
public:
    /// @brief Construct with empty registers (reads return 0x80 by default).
    MockProtocol() = default;

    /// @brief Construct with pre-populated register values.
    /// @param initial_registers Map of offset -> value to pre-populate.
    explicit MockProtocol(std::unordered_map<uint8_t, uint8_t> initial_registers);

    ~MockProtocol() override = default;

    bool Init() override;
    void Shutdown() override;
    std::optional<uint8_t> ReadByte(uint8_t offset) override;
    bool WriteByte(uint8_t offset, uint8_t value) override;
    bool IsOpen() const override;

private:
    bool open_ = false;
    std::unordered_map<uint8_t, uint8_t> registers_;
};
