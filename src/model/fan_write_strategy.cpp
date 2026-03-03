/// @file fan_write_strategy.cpp
/// @brief Fan write strategy implementations.

#include "model/fan_write_strategy.h"
#include "model/register_map.h"
#include "protocol/protocol.h"

#include <chrono>
#include <thread>

// ============================================================================
// Constants
// ============================================================================

/// @brief Delay between dual-fan write sequences (milliseconds).
static constexpr int kDualFanDelayMs = 100;

/// @brief Max retries for dual-fan verification.
static constexpr int kDualFanRetries = 5;

/// @brief Fan selector value for Fan 1.
static constexpr uint8_t kFan1 = 0x00;

/// @brief Fan selector value for Fan 2.
static constexpr uint8_t kFan2 = 0x01;

// ============================================================================
// SingleFanWrite
// ============================================================================

bool SingleFanWrite::WriteFanLevel(Protocol& proto, const RegisterMap& regs,
                                   uint8_t level) {
    if (level > 7) level = 7;
    return proto.WriteByte(regs.fan_control, level);
}

bool SingleFanWrite::SetBiosMode(Protocol& proto, const RegisterMap& regs) {
    return proto.WriteByte(regs.fan_control, regs.bios_mode_value);
}

// ============================================================================
// DualFanHandshake
// ============================================================================

bool DualFanHandshake::WriteFanLevel(Protocol& proto, const RegisterMap& regs,
                                     uint8_t level) {
    if (level > 7) level = 7;

    proto.BeginTransaction();

    for (int attempt = 0; attempt < kDualFanRetries; ++attempt) {
        // Select Fan 1, set level
        if (!proto.WriteByte(regs.fan_selector, kFan1)) {
            proto.EndTransaction();
            return false;
        }
        if (!proto.WriteByte(regs.fan_control, level)) {
            proto.EndTransaction();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kDualFanDelayMs));

        // Select Fan 2, set level
        if (!proto.WriteByte(regs.fan_selector, kFan2)) {
            proto.EndTransaction();
            return false;
        }
        if (!proto.WriteByte(regs.fan_control, level)) {
            proto.EndTransaction();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kDualFanDelayMs));

        // Read back Fan 1
        if (!proto.WriteByte(regs.fan_selector, kFan1)) {
            proto.EndTransaction();
            return false;
        }
        auto fan1 = proto.ReadByte(regs.fan_control);

        // Read back Fan 2
        if (!proto.WriteByte(regs.fan_selector, kFan2)) {
            proto.EndTransaction();
            return false;
        }
        auto fan2 = proto.ReadByte(regs.fan_control);

        // Verify: mask off BIOS mode bit and check level
        uint8_t mask = static_cast<uint8_t>(~(1u << regs.bios_mode_bit));
        bool fan1_ok = fan1 && ((*fan1 & mask) == level);
        bool fan2_ok = fan2 && ((*fan2 & mask) == level);

        if (fan1_ok && fan2_ok) {
            // Restore Fan 1 as active selection
            proto.WriteByte(regs.fan_selector, kFan1);
            proto.EndTransaction();
            return true;
        }
    }

    proto.EndTransaction();
    return false;
}

bool DualFanHandshake::SetBiosMode(Protocol& proto, const RegisterMap& regs) {
    proto.BeginTransaction();

    // Set BIOS mode on both fans
    if (!proto.WriteByte(regs.fan_selector, kFan1)) {
        proto.EndTransaction();
        return false;
    }
    if (!proto.WriteByte(regs.fan_control, regs.bios_mode_value)) {
        proto.EndTransaction();
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kDualFanDelayMs));

    if (!proto.WriteByte(regs.fan_selector, kFan2)) {
        proto.EndTransaction();
        return false;
    }
    if (!proto.WriteByte(regs.fan_control, regs.bios_mode_value)) {
        proto.EndTransaction();
        return false;
    }

    // Restore Fan 1 selection
    proto.WriteByte(regs.fan_selector, kFan1);
    proto.EndTransaction();
    return true;
}
