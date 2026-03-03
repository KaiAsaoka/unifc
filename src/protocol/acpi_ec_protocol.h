/// @file acpi_ec_protocol.h
/// @brief ACPI Embedded Controller protocol via PawnIO driver.
///
/// Implements the standard ACPI EC handshake at I/O ports 0x62/0x66.
/// Used by ThinkPads, some HP, some Acer laptops.

#pragma once

#include "protocol/protocol.h"

#include <cstdint>
#include <mutex>
#include <string>

/// @brief ACPI EC protocol using PawnIO for privileged I/O port access.
///
/// Handles the full EC read/write handshake (IBF/OBF flag polling, command
/// dispatch, data transfer). Uses a cross-process named mutex ("Access_EC")
/// to prevent collisions with other fan control tools.
///
/// The UNIFC_MOCK compile flag replaces PawnIO with an in-memory port-level
/// mock that simulates the EC state machine (IBF/OBF flags, command dispatch).
class AcpiEcProtocol : public Protocol {
public:
    /// @brief Construct with the path to the PawnIO EC module.
    /// @param module_path Path to LpcACPIEC.bin (or .amx).
    explicit AcpiEcProtocol(const std::string& module_path);
    ~AcpiEcProtocol() override;

    bool Init() override;
    void Shutdown() override;
    std::optional<uint8_t> ReadByte(uint8_t offset) override;
    bool WriteByte(uint8_t offset, uint8_t value) override;
    void BeginTransaction() override;
    void EndTransaction() override;
    bool IsOpen() const override;

private:
    // ---- Port-level I/O ----

    /// @brief Read a byte from an I/O port.
    std::optional<uint8_t> Inb(uint16_t port);

    /// @brief Write a byte to an I/O port.
    bool Outb(uint16_t port, uint8_t value);

    // ---- EC handshake (caller must hold mutex) ----

    /// @brief Wait for an EC status flag condition.
    bool WaitStatus(uint8_t mask, uint8_t expected);

    /// @brief Read a byte from an EC register (no mutex).
    std::optional<uint8_t> ReadByteImpl(uint8_t offset);

    /// @brief Write a byte to an EC register (no mutex).
    bool WriteByteImpl(uint8_t offset, uint8_t value);

    // ---- Cross-process mutex ----

    /// @brief Acquire the EC mutex if not in a transaction.
    bool AcquireLock();

    /// @brief Release the EC mutex if not in a transaction.
    void ReleaseLock();

    std::string module_path_;
    void* handle_ = nullptr;       ///< PawnIO driver handle.
    bool open_ = false;

    void* ec_mutex_ = nullptr;     ///< Named mutex handle (HANDLE as void*).
    bool in_transaction_ = false;  ///< True between BeginTransaction/EndTransaction.
    bool lock_held_ = false;       ///< True when the named mutex is held.
};
