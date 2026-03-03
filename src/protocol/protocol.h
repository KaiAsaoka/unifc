/// @file protocol.h
/// @brief Abstract interface for hardware register access.
///
/// Protocol is the primary point of polymorphism in unifc. Concrete
/// implementations handle specific hardware access methods (ACPI EC, WMI,
/// vendor drivers, mock). Everything else (register maps, fan write
/// strategies) is data or strategy objects composed into the Laptop class.

#pragma once

#include <cstdint>
#include <optional>

/// @brief Abstract interface for reading and writing hardware registers.
///
/// Each concrete Protocol handles a specific access method (ACPI EC ports,
/// WMI calls, vendor driver IOCTLs, etc). The Laptop class owns a Protocol
/// and delegates all hardware I/O through it.
///
/// Thread safety: Implementations must be safe for concurrent access from
/// multiple threads. The ACPI EC implementation uses a cross-process mutex.
class Protocol {
public:
    virtual ~Protocol() = default;

    /// @brief Initialize the protocol (load drivers, open handles).
    /// @return true on success, false if the driver or hardware is unavailable.
    virtual bool Init() = 0;

    /// @brief Clean up resources (close handles, release drivers).
    virtual void Shutdown() = 0;

    /// @brief Read a byte from the given register offset.
    /// @param offset The register offset to read.
    /// @return The byte read, or std::nullopt on timeout or failure.
    virtual std::optional<uint8_t> ReadByte(uint8_t offset) = 0;

    /// @brief Write a byte to the given register offset.
    /// @param offset The register offset to write.
    /// @param value The byte to write.
    /// @return true on success, false on timeout or failure.
    virtual bool WriteByte(uint8_t offset, uint8_t value) = 0;

    /// @brief Begin an atomic transaction (hold mutex across multiple calls).
    ///
    /// When a transaction is active, ReadByte/WriteByte skip per-call mutex
    /// acquisition. This is needed for multi-step operations like the dual-fan
    /// handshake where the mutex must be held across the entire sequence.
    ///
    /// Default implementation is a no-op (for protocols without a mutex).
    virtual void BeginTransaction() {}

    /// @brief End an atomic transaction (release the mutex).
    virtual void EndTransaction() {}

    /// @brief Check if the protocol is initialized and ready.
    /// @return true if Init() succeeded and Shutdown() hasn't been called.
    virtual bool IsOpen() const = 0;
};
