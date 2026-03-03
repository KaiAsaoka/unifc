# unifc

Universal fan control for Windows. One tool for laptops and desktops, any manufacturer.

## Why

Existing Windows fan control tools tend to target specific hardware families: [FanControl](https://github.com/Rem0o/FanControl.Releases) focuses on desktops, TPFanCtrl on ThinkPads, DellFanManagement on Dells, and [NBFC](https://github.com/hirschmann/nbfc) on direct EC access across laptops. There is no single tool that covers laptops and desktops from all manufacturers.

unifc aims to be that tool. The hardware access method (ACPI EC, WMI, SuperIO, vendor drivers) is a swappable protocol object, and the per-model register layout is just data. So a ThinkPad T480, a Dell XPS, and an ASUS desktop motherboard can all be supported within the same tool.

## Architecture

```
Laptop / Desktop
  |-- Protocol          (how to talk to hardware: ACPI EC, WMI, SuperIO, ...)
  |-- RegisterMap       (what registers to hit: offsets, bit layouts, sensor locations)
  +-- FanWriteStrategy  (single-fan write vs dual-fan handshake)
```

New system = pick the right protocol + register map + write strategy. New hardware interface = one new Protocol class.

## Protocols

| Protocol | Access Method | Systems |
|----------|---------------|---------|
| ACPI EC  | I/O ports 0x62/0x66 via [PawnIO](https://github.com/namazso/PawnIO) | ThinkPad, some HP, some Acer |
| WMI      | Windows Management Instrumentation | Dell (newer), ASUS, MSI |
| Mock     | In-memory simulation | Dev/testing |

Dell SMM, desktop SuperIO, and vendor kernel drivers are planned.

## Building

C++17. Tested with MSYS2 UCRT64 (GCC 13+) and MSVC 2022.

```bash
# MinGW
cmake -B build -G "MinGW Makefiles"
cmake --build build

# Visual Studio
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# Mock mode (no hardware needed)
cmake -B build -G "MinGW Makefiles" -DUNIFC_MOCK=ON
cmake --build build
```

## Requirements

- Windows x86/x64
- Admin privileges
- ACPI EC protocol requires [PawnIO](https://pawnio.eu) (`winget install namazso.PawnIO`)

## Status

Early development. ACPI EC (ThinkPad) is the first protocol, verified on a T480.

## License

TBD
