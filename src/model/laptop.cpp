/// @file laptop.cpp
/// @brief Laptop class implementation.

#include "model/laptop.h"
#include "model/fan_write_strategy.h"
#include "protocol/protocol.h"

#include <chrono>
#include <thread>

/// @brief RPM value 0xFFFF is a tachometer error code.
static constexpr uint16_t kRpmInvalid = 0xFFFF;

/// @brief Sensor value 0x80 indicates inactive/invalid.
static constexpr uint8_t kTempInvalid = 0x80;

Laptop::Laptop(std::unique_ptr<Protocol> protocol,
               RegisterMap register_map,
               std::unique_ptr<FanWriteStrategy> strategy)
    : protocol_(std::move(protocol))
    , register_map_(std::move(register_map))
    , strategy_(std::move(strategy)) {}

Laptop::~Laptop() = default;

Laptop::Laptop(Laptop&&) noexcept = default;
Laptop& Laptop::operator=(Laptop&&) noexcept = default;

bool Laptop::Init() {
    return protocol_->Init();
}

void Laptop::Shutdown() {
    SetBiosMode();
    protocol_->Shutdown();
}

std::optional<int> Laptop::ReadTemperature(uint8_t offset) {
    // Noise rejection: two consecutive reads must be within 2 degrees.
    auto first = protocol_->ReadByte(offset);
    if (!first) return std::nullopt;

    // 1ms settle delay between reads
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    auto second = protocol_->ReadByte(offset);
    if (!second) return std::nullopt;

    int a = static_cast<int>(*first);
    int b = static_cast<int>(*second);

    // Validate: 0x00 and 0x80 are invalid, out of range is rejected
    if (a == 0 || a == kTempInvalid || a < register_map_.temp_min || a > register_map_.temp_max)
        return std::nullopt;
    if (b == 0 || b == kTempInvalid || b < register_map_.temp_min || b > register_map_.temp_max)
        return std::nullopt;

    int diff = (a > b) ? (a - b) : (b - a);
    if (diff > 2) return std::nullopt;

    // Return the more recent read
    return b;
}

std::vector<std::optional<int>> Laptop::ReadAllTemperatures() {
    std::vector<std::optional<int>> temps;
    temps.reserve(register_map_.temp_sensors.size());

    for (uint8_t offset : register_map_.temp_sensors) {
        temps.push_back(ReadTemperature(offset));
    }

    return temps;
}

std::optional<uint16_t> Laptop::ReadFanRpm() {
    // RPM registers can be flaky - retry up to 3 times
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto lo = protocol_->ReadByte(register_map_.fan_rpm_lo);
        auto hi = protocol_->ReadByte(register_map_.fan_rpm_hi);

        if (lo && hi) {
            uint16_t rpm = static_cast<uint16_t>((*hi << 8) | *lo);
            if (rpm == kRpmInvalid) return std::nullopt;
            return rpm;
        }
    }
    return std::nullopt;
}

bool Laptop::SetFanLevel(uint8_t level) {
    return strategy_->WriteFanLevel(*protocol_, register_map_, level);
}

bool Laptop::SetBiosMode() {
    return strategy_->SetBiosMode(*protocol_, register_map_);
}

bool Laptop::DetectDualFan() {
    if (register_map_.fan_selector == 0) return false;

    protocol_->BeginTransaction();

    // Write Fan 2 selector and read back
    if (!protocol_->WriteByte(register_map_.fan_selector, 0x01)) {
        protocol_->EndTransaction();
        return false;
    }

    auto val = protocol_->ReadByte(register_map_.fan_selector);
    bool has_dual = val && (*val == 0x01);

    // Restore Fan 1 selection
    protocol_->WriteByte(register_map_.fan_selector, 0x00);
    protocol_->EndTransaction();

    return has_dual;
}

const RegisterMap& Laptop::GetRegisterMap() const {
    return register_map_;
}

Protocol* Laptop::GetProtocol() {
    return protocol_.get();
}
