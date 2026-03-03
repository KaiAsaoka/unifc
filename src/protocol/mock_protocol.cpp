/// @file mock_protocol.cpp
/// @brief Register-level mock protocol implementation.

#include "protocol/mock_protocol.h"

#include <iostream>

MockProtocol::MockProtocol(std::unordered_map<uint8_t, uint8_t> initial_registers)
    : registers_(std::move(initial_registers)) {}

bool MockProtocol::Init() {
    std::cerr << "[MockProtocol] Initialized (no hardware access)\n";
    open_ = true;
    return true;
}

void MockProtocol::Shutdown() {
    if (open_) {
        std::cerr << "[MockProtocol] Shutdown\n";
    }
    open_ = false;
}

std::optional<uint8_t> MockProtocol::ReadByte(uint8_t offset) {
    if (!open_) return std::nullopt;

    auto it = registers_.find(offset);
    if (it != registers_.end()) {
        return it->second;
    }
    return 0x80; // Default: invalid/inactive sensor
}

bool MockProtocol::WriteByte(uint8_t offset, uint8_t value) {
    if (!open_) return false;

    registers_[offset] = value;
    return true;
}

bool MockProtocol::IsOpen() const {
    return open_;
}
