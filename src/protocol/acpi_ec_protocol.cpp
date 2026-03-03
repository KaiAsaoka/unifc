/// @file acpi_ec_protocol.cpp
/// @brief ACPI EC protocol implementation.
///
/// Contains two implementations selected at compile time:
/// - Real mode: Uses PawnIO driver for hardware port access.
/// - Mock mode (UNIFC_MOCK): Simulates EC at the port level.

#include "protocol/acpi_ec_protocol.h"

#include <chrono>
#include <iostream>
#include <thread>

// ============================================================================
// EC Protocol Constants
// ============================================================================

static constexpr uint16_t kDataPort = 0x62;
static constexpr uint16_t kCtrlPort = 0x66;
static constexpr uint8_t kStatusObf = 0x01;
static constexpr uint8_t kStatusIbf = 0x02;
static constexpr uint8_t kCmdRead = 0x80;
static constexpr uint8_t kCmdWrite = 0x81;
static constexpr int kTimeoutMs = 1000;
static constexpr int kPollIntervalMs = 10;

#ifdef UNIFC_MOCK

// ============================================================================
// Mock EC port-level implementation
// ============================================================================

#include <unordered_map>

/// @brief States of the mock EC command state machine.
enum class MockState {
    kIdle,
    kReadOffset,
    kReadData,
    kWriteOffset,
    kWriteData
};

static MockState g_state = MockState::kIdle;
static uint8_t g_current_offset = 0;
static std::unordered_map<uint8_t, uint8_t> g_mock_regs = {
    {0x78, 55}, {0x79, 0x80}, {0x7A, 48}, {0x7B, 60},
    {0x7C, 42}, {0x7D, 0x80}, {0x7E, 40}, {0x7F, 0x80},
    {0xC0, 50}, {0xC1, 45}, {0xC2, 38}, {0xC3, 0x80},
    {0x84, 0xA0}, {0x85, 0x09},
    {0x2F, 0x80}, {0x31, 0x00},
};

AcpiEcProtocol::AcpiEcProtocol(const std::string& module_path)
    : module_path_(module_path) {}

AcpiEcProtocol::~AcpiEcProtocol() {
    Shutdown();
}

bool AcpiEcProtocol::Init() {
    std::cerr << "[AcpiEcProtocol] Mock EC initialized\n";
    open_ = true;
    return true;
}

void AcpiEcProtocol::Shutdown() {
    if (open_) {
        std::cerr << "[AcpiEcProtocol] Mock EC shutdown\n";
    }
    open_ = false;
}

std::optional<uint8_t> AcpiEcProtocol::Inb(uint16_t port) {
    if (!open_) return std::nullopt;

    if (port == kCtrlPort) {
        if (g_state == MockState::kReadData) return kStatusObf;
        return 0x00;
    }

    if (port == kDataPort) {
        if (g_state == MockState::kReadData) {
            uint8_t val = 0x80;
            auto it = g_mock_regs.find(g_current_offset);
            if (it != g_mock_regs.end()) val = it->second;
            g_state = MockState::kIdle;
            return val;
        }
    }

    return 0x00;
}

bool AcpiEcProtocol::Outb(uint16_t port, uint8_t value) {
    if (!open_) return false;

    if (port == kCtrlPort) {
        if (value == kCmdRead) g_state = MockState::kReadOffset;
        else if (value == kCmdWrite) g_state = MockState::kWriteOffset;
    } else if (port == kDataPort) {
        switch (g_state) {
            case MockState::kReadOffset:
                g_current_offset = value;
                g_state = MockState::kReadData;
                break;
            case MockState::kWriteOffset:
                g_current_offset = value;
                g_state = MockState::kWriteData;
                break;
            case MockState::kWriteData:
                g_mock_regs[g_current_offset] = value;
                g_state = MockState::kIdle;
                break;
            default:
                break;
        }
    }

    return true;
}

bool AcpiEcProtocol::AcquireLock() { return true; }
void AcpiEcProtocol::ReleaseLock() {}

#else // !UNIFC_MOCK

// ============================================================================
// Real PawnIO implementation
// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winternl.h>
#include <PawnIOLib.h>

#include <fstream>
#include <vector>

AcpiEcProtocol::AcpiEcProtocol(const std::string& module_path)
    : module_path_(module_path) {}

AcpiEcProtocol::~AcpiEcProtocol() {
    Shutdown();
}

bool AcpiEcProtocol::Init() {
    // Open PawnIO driver
    HANDLE h = nullptr;
    HRESULT hr = pawnio_open(&h);
    if (FAILED(hr)) {
        std::cerr << "Failed to open PawnIO driver (HRESULT=0x"
                  << std::hex << hr << std::dec << ")\n"
                  << "Is the PawnIO driver installed and running as admin?\n";
        return false;
    }
    handle_ = h;

    // Read the module file
    std::ifstream file(module_path_, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open module file: " << module_path_ << "\n";
        Shutdown();
        return false;
    }

    auto size = file.tellg();
    file.seekg(0);
    std::vector<unsigned char> blob(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(blob.data()), size);

    if (!file) {
        std::cerr << "Failed to read module file: " << module_path_ << "\n";
        Shutdown();
        return false;
    }

    // Load the module into PawnIO
    hr = pawnio_load(static_cast<HANDLE>(handle_), blob.data(), blob.size());
    if (FAILED(hr)) {
        std::cerr << "Failed to load PawnIO module (HRESULT=0x"
                  << std::hex << hr << std::dec << ")\n";
        Shutdown();
        return false;
    }

    // Create named mutex for cross-process synchronization
    ec_mutex_ = CreateMutexW(nullptr, FALSE, L"Access_EC");

    open_ = true;
    return true;
}

void AcpiEcProtocol::Shutdown() {
    if (handle_) {
        pawnio_close(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    if (ec_mutex_) {
        CloseHandle(static_cast<HANDLE>(ec_mutex_));
        ec_mutex_ = nullptr;
    }
    open_ = false;
    in_transaction_ = false;
    lock_held_ = false;
}

std::optional<uint8_t> AcpiEcProtocol::Inb(uint16_t port) {
    if (!open_) return std::nullopt;

    ULONG64 in_params[] = {port};
    ULONG64 out_params[1] = {};
    SIZE_T return_size = 0;

    HRESULT hr = pawnio_execute(
        static_cast<HANDLE>(handle_),
        "ioctl_pio_read",
        in_params, 1,
        out_params, 1,
        &return_size
    );

    if (FAILED(hr)) return std::nullopt;
    return static_cast<uint8_t>(out_params[0]);
}

bool AcpiEcProtocol::Outb(uint16_t port, uint8_t value) {
    if (!open_) return false;

    ULONG64 in_params[] = {port, value};
    SIZE_T return_size = 0;

    HRESULT hr = pawnio_execute(
        static_cast<HANDLE>(handle_),
        "ioctl_pio_write",
        in_params, 2,
        nullptr, 0,
        &return_size
    );

    return SUCCEEDED(hr);
}

bool AcpiEcProtocol::AcquireLock() {
    if (in_transaction_ && lock_held_) return true;

    if (!ec_mutex_) {
        ec_mutex_ = CreateMutexW(nullptr, FALSE, L"Access_EC");
    }
    if (!ec_mutex_) return false;

    DWORD result = WaitForSingleObject(static_cast<HANDLE>(ec_mutex_), kTimeoutMs);
    lock_held_ = (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED);
    return lock_held_;
}

void AcpiEcProtocol::ReleaseLock() {
    if (in_transaction_) return; // Transaction keeps the lock
    if (lock_held_ && ec_mutex_) {
        ReleaseMutex(static_cast<HANDLE>(ec_mutex_));
        lock_held_ = false;
    }
}

#endif // UNIFC_MOCK

// ============================================================================
// EC Handshake (shared by both real and mock)
// ============================================================================

bool AcpiEcProtocol::WaitStatus(uint8_t mask, uint8_t expected) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(kTimeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        auto status = Inb(kCtrlPort);
        if (!status) return false;
        if ((*status & mask) == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

    return false;
}

std::optional<uint8_t> AcpiEcProtocol::ReadByteImpl(uint8_t offset) {
    if (!WaitStatus(kStatusIbf, 0x00)) return std::nullopt;
    if (!Outb(kCtrlPort, kCmdRead))    return std::nullopt;
    if (!WaitStatus(kStatusIbf, 0x00)) return std::nullopt;
    if (!Outb(kDataPort, offset))      return std::nullopt;
    if (!WaitStatus(kStatusObf, kStatusObf)) return std::nullopt;
    auto result = Inb(kDataPort);

    // 1ms settle delay - prevents bus contention with ACPI driver
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return result;
}

bool AcpiEcProtocol::WriteByteImpl(uint8_t offset, uint8_t value) {
    if (!WaitStatus(kStatusIbf, 0x00)) return false;
    if (!Outb(kCtrlPort, kCmdWrite))   return false;
    if (!WaitStatus(kStatusIbf, 0x00)) return false;
    if (!Outb(kDataPort, offset))      return false;
    if (!WaitStatus(kStatusIbf, 0x00)) return false;
    if (!Outb(kDataPort, value))       return false;
    return true;
}

// ============================================================================
// Public Protocol interface
// ============================================================================

std::optional<uint8_t> AcpiEcProtocol::ReadByte(uint8_t offset) {
    if (!AcquireLock()) return std::nullopt;
    auto result = ReadByteImpl(offset);
    ReleaseLock();
    return result;
}

bool AcpiEcProtocol::WriteByte(uint8_t offset, uint8_t value) {
    if (!AcquireLock()) return false;
    bool result = WriteByteImpl(offset, value);
    ReleaseLock();
    return result;
}

void AcpiEcProtocol::BeginTransaction() {
    AcquireLock();
    in_transaction_ = true;
}

void AcpiEcProtocol::EndTransaction() {
    in_transaction_ = false;
    if (lock_held_ && ec_mutex_) {
#ifndef UNIFC_MOCK
        ReleaseMutex(static_cast<HANDLE>(ec_mutex_));
#endif
        lock_held_ = false;
    }
}

bool AcpiEcProtocol::IsOpen() const {
    return open_;
}
