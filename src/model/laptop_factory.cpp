/// @file laptop_factory.cpp
/// @brief Factory for assembling Laptop instances from config.

#include "model/laptop_factory.h"
#include "model/laptop.h"
#include "model/fan_write_strategy.h"
#include "config/config.h"
#include "protocol/mock_protocol.h"

#ifndef UNIFC_MOCK
#include "protocol/acpi_ec_protocol.h"
#endif

#include <iostream>
#include <memory>

/// @brief Build mock register values from a RegisterMap for MockProtocol.
///
/// Populates the mock's register store with plausible values for each
/// sensor offset, RPM registers, and fan control register.
static std::unordered_map<uint8_t, uint8_t> BuildMockRegisters(const RegisterMap& regs) {
    std::unordered_map<uint8_t, uint8_t> mock_regs;

    // Temperature sensors: assign plausible values
    uint8_t base_temp = 45;
    for (size_t i = 0; i < regs.temp_sensors.size(); ++i) {
        // Alternate between valid temps and inactive (0x80) sensors
        if (i % 3 == 2) {
            mock_regs[regs.temp_sensors[i]] = 0x80; // inactive
        } else {
            mock_regs[regs.temp_sensors[i]] = base_temp + static_cast<uint8_t>(i * 3);
        }
    }

    // Fan RPM (little-endian: 0x09A0 = 2464 RPM)
    mock_regs[regs.fan_rpm_lo] = 0xA0;
    mock_regs[regs.fan_rpm_hi] = 0x09;

    // Fan control: BIOS mode
    mock_regs[regs.fan_control] = regs.bios_mode_value;

    // Fan selector: Fan 1
    if (regs.fan_selector != 0) {
        mock_regs[regs.fan_selector] = 0x00;
    }

    return mock_regs;
}

std::optional<Laptop> CreateLaptop(const Config& config) {
    // Select protocol
    std::unique_ptr<Protocol> protocol;

    if (config.protocol == "mock") {
        auto mock_regs = BuildMockRegisters(config.register_map);
        protocol = std::make_unique<MockProtocol>(std::move(mock_regs));
    }
#ifndef UNIFC_MOCK
    else if (config.protocol == "acpi_ec") {
        std::string module_path = "LpcACPIEC.bin";
        auto it = config.extras.find("ModulePath");
        if (it != config.extras.end()) {
            module_path = it->second;
        }
        protocol = std::make_unique<AcpiEcProtocol>(module_path);
    }
#else
    else if (config.protocol == "acpi_ec") {
        // In mock build, acpi_ec falls back to mock protocol
        std::cerr << "Mock build: using MockProtocol for acpi_ec\n";
        auto mock_regs = BuildMockRegisters(config.register_map);
        protocol = std::make_unique<MockProtocol>(std::move(mock_regs));
    }
#endif
    else {
        std::cerr << "Unknown protocol: " << config.protocol << "\n";
        return std::nullopt;
    }

    // Select write strategy based on number of fans
    std::unique_ptr<FanWriteStrategy> strategy;
    if (config.register_map.num_fans >= 2) {
        strategy = std::make_unique<DualFanHandshake>();
    } else {
        strategy = std::make_unique<SingleFanWrite>();
    }

    return Laptop(std::move(protocol), config.register_map, std::move(strategy));
}
