/// @file laptop_factory.h
/// @brief Factory for assembling Laptop instances from config.
///
/// Reads the protocol type and register map from Config, creates the
/// appropriate Protocol implementation and FanWriteStrategy, and assembles
/// them into a Laptop.

#pragma once

#include <optional>

struct Config;
class Laptop;

/// @brief Create a Laptop from a parsed Config.
///
/// Selects the Protocol implementation based on Config::protocol
/// ("acpi_ec", "mock", etc.) and the FanWriteStrategy based on
/// Config::register_map.num_fans.
/// @param config The parsed config file.
/// @return An assembled Laptop, or std::nullopt if the protocol is unknown.
std::optional<Laptop> CreateLaptop(const Config& config);
