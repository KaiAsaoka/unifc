# unifc - Universal Fan Control

Universal fan control for Windows. One tool for laptops and desktops, any manufacturer.

## Mission

Existing Windows fan control tools tend to target specific hardware families: FanControl
focuses on desktops, TPFanCtrl on ThinkPads, DellFanManagement on Dells, and NBFC on
direct EC access across laptops. There is no single tool that covers laptops and desktops
from all manufacturers.

unifc aims to be that tool. The hardware access method (ACPI EC, WMI, SuperIO, vendor
drivers) is a swappable protocol object, and the per-model register layout is just data.
So a ThinkPad T480, a Dell XPS, and an ASUS desktop motherboard can all be supported
within the same tool.

## Build

- **Language**: C++17
- **Build system**: CMake
- **Toolchains tested**: MSYS2 UCRT64 (GCC 13+), MSVC / Visual Studio 2022
- **Target platform**: Windows x86/x64
- **Requires**: Administrator privileges (for EC port access); driver depends on protocol

```bash
# MinGW (MSYS2 UCRT64)
cmake -B build -G "MinGW Makefiles"
cmake --build build

# Visual Studio 2022
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# Mock protocol (no hardware required - simulates sensor data)
cmake -B build -G "MinGW Makefiles" -DUNIFC_MOCK=ON
cmake --build build
```

## Architecture

### Core Design: Composition Over Inheritance

A `Laptop` is a concrete class assembled from composed objects. The `Protocol` base class
is the primary point of polymorphism - it defines how to communicate with the hardware.

```
Laptop (concrete - assembles components)
  HAS-A Protocol            (abstract base - how to read/write hardware)
  HAS-A RegisterMap         (data struct - what register offsets mean)
  HAS-A FanWriteStrategy    (strategy object - single-fan vs dual-fan)
```

Adding a new laptop model = config/factory entry picking the right components.
Adding a new access method = one new Protocol subclass.

### Layers (top to bottom)

1. **UI** - Win32 system tray icon + dialog window. Timer-driven display updates.
2. **Fan Controller** - State machine (BIOS/Smart/Manual modes). Reads temperatures,
   applies fan curve, writes fan level. Runs on a background thread. Hardware-agnostic -
   communicates through Protocol + RegisterMap + FanWriteStrategy.
3. **Protocol** - Abstract base class. Concrete implementations handle specific hardware
   access methods (ACPI EC, WMI, vendor drivers, mock).

### Protocol Interface

```cpp
/// @brief Abstract interface for hardware register access.
class Protocol {
public:
    virtual ~Protocol() = default;
    virtual std::optional<uint8_t> ReadByte(uint8_t offset) = 0;
    virtual bool WriteByte(uint8_t offset, uint8_t value) = 0;
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
};
```

### Protocol Implementations

| Protocol Class           | Access Method                  | Used By                       |
|--------------------------|--------------------------------|-------------------------------|
| `AcpiEcProtocol`         | Direct I/O ports 0x62/0x66     | ThinkPad, some HP, some Acer  |
| `AcpiEcAltProtocol`      | Direct I/O ports 0x68/0x6C     | Some Acer models              |
| `WmiProtocol`            | Windows WMI calls              | Dell (newer), ASUS, MSI       |
| `SmmProtocol`            | Dell SMM BIOS interface        | Dell (older)                  |
| `VendorDriverProtocol`   | Vendor kernel driver IOCTL     | Lenovo Legion, others         |
| `MockProtocol`           | In-memory simulation           | Testing (no hardware)         |

### RegisterMap

Per-model register offsets are data, not behavior. Expressed as a struct, not a class:

```cpp
/// @brief Defines the EC register layout for a specific laptop model.
struct RegisterMap {
    uint8_t fan_control;                   ///< Fan speed control register (e.g., 0x2F)
    uint8_t fan_selector;                  ///< Fan selector for multi-fan (e.g., 0x31), 0 if N/A
    uint8_t bios_mode_bit;                 ///< Bit position for BIOS mode flag (e.g., 7)
    uint8_t fan_rpm_lo;                    ///< Fan RPM low byte register (e.g., 0x84)
    uint8_t fan_rpm_hi;                    ///< Fan RPM high byte register (e.g., 0x85)
    std::vector<uint8_t> temp_sensors;     ///< Temperature sensor register offsets
    uint8_t temp_min;                      ///< Minimum valid temperature (filter threshold)
    uint8_t temp_max;                      ///< Maximum valid temperature (filter threshold)
    uint8_t num_fans;                      ///< Number of independently controllable fans
};
```

### FanWriteStrategy

Some models need special write sequences beyond "write a byte to a register":

```cpp
class FanWriteStrategy {
public:
    virtual bool WriteFanLevel(Protocol& proto, const RegisterMap& regs,
                               uint8_t level) = 0;
};

class SingleFanWrite : public FanWriteStrategy { /* ... */ };
class DualFanHandshake : public FanWriteStrategy { /* ... */ };
```

### Assembling a Laptop

A factory or config loader picks the right components for each model:

```cpp
std::optional<Laptop> CreateLaptop(const std::string& model_id) {
    if (model_id == "ThinkPad T480") {
        return Laptop(
            std::make_unique<AcpiEcProtocol>(),
            kThinkPadT480Registers,
            std::make_unique<DualFanHandshake>()
        );
    }
    if (model_id == "ASUS Zenbook 14") {
        return Laptop(
            std::make_unique<WmiProtocol>("ASUS_ATK"),
            kAsusZenbook14Registers,
            std::make_unique<SingleFanWrite>()
        );
    }
    return std::nullopt;
}
```

Register maps can be constexpr structs for known models or loaded from config files
for community-contributed models.

### Three Axes of Variation

| Axis                  | What Varies                           | Expressed As               |
|-----------------------|---------------------------------------|----------------------------|
| **Protocol**          | How to read/write (EC, WMI, SMBus)    | Abstract base class        |
| **Register map**      | What offsets mean (0x2F = fan, etc.)   | Data struct or config file |
| **Behavioral quirks** | Dual-fan handshake, BIOS override     | Strategy objects           |

### Threading Model

- Main thread: Win32 message loop (UI).
- Worker thread: Persistent thread that polls hardware every `Cycle` seconds. Posts
  results back to the UI thread via custom window messages (`WM_USER+N`).
- Hardware access is serialized with a mutex (`std::mutex`). Named mutex `Access_EC`
  for cross-process sync when using ACPI EC protocol.

## Project Structure

```
src/
  main.cpp                      Entry point, setup, safety handlers
  config/
    config.h / .cpp             INI config parsing, fan curve definitions
  protocol/
    protocol.h                  Abstract Protocol base class
    acpi_ec_protocol.h / .cpp   ACPI EC via PawnIO (ports 0x62/0x66)
    mock_protocol.h / .cpp      Mock implementation for testing
  model/
    register_map.h              RegisterMap struct, predefined model configs
    fan_write_strategy.h / .cpp FanWriteStrategy base + SingleFan, DualFan
    laptop.h / .cpp             Laptop class (composes Protocol + RegisterMap + Strategy)
    laptop_factory.h / .cpp     Factory: model ID -> assembled Laptop
  fan/
    fan_controller.h / .cpp     Fan control state machine, safety checks
  ui/                           (future)
    tray_icon.h / .cpp
    settings_dialog.h / .cpp
thirdparty/
  PawnIOLib/                    Git submodule - PawnIO usermode library
```

## Hardware Reference: ACPI EC Protocol

This section documents the standard ACPI Embedded Controller protocol used by ThinkPads
and some other laptops. Other protocols (WMI, SMM) have their own conventions.

### EC I/O Ports

| Control Port | Data Port | Description |
|-------------|-----------|-------------|
| 0x66        | 0x62      | Standard ACPI EC ports |

### Status Flags (read from control port)

- `0x01` (OBF): Output buffer full - data ready to read
- `0x02` (IBF): Input buffer full - EC busy, wait before writing

### Commands (written to control port)

- `0x80`: Read byte
- `0x81`: Write byte

### Read Sequence

Wait IBF clear -> write `0x80` to ctrl -> wait IBF clear -> write offset to data ->
wait OBF set -> read result from data.

### Write Sequence

Wait IBF clear -> write `0x81` to ctrl -> wait IBF clear -> write offset to data ->
wait IBF clear -> write value to data.

### Timing

- Timeout: 1000ms max per flag wait, polled every 10ms
- 1ms settle delay after each read (prevents bus contention with ACPI driver)
- 100ms delay between dual-fan write sequences

## PawnIO Integration (for ACPI EC Protocol)

PawnIO is a signed kernel driver that runs sandboxed bytecode modules. unifc uses PawnIO
instead of WinRing0 for the reasons outlined below.

### PawnIO vs WinRing0

| Aspect           | WinRing0                         | PawnIO                              |
|------------------|----------------------------------|-------------------------------------|
| Security model   | Grants access to all I/O ports   | Restricted to ports 0x62/0x66       |
| AV compatibility | Blocked by Defender (CVE-2020-14979) | Signed, not blocked             |
| Cross-process    | `Access_Thinkpad_EC` mutex       | `Access_EC` mutex                   |

### PawnIO Architecture

1. **PawnIO.sys** (kernel) - Signed driver, device `\\.\PawnIO`, three IOCTLs
2. **LpcACPIEC.bin** (module) - Sandboxed Pawn bytecode allowing only ports 0x62/0x66
3. **PawnIOLib** (usermode) - C/C++ API: `pawnio_open()`, `pawnio_load()`, `pawnio_execute()`

### PawnIO API Usage

```cpp
// Read a byte from EC port
ULONG64 in_params[] = {port};
ULONG64 out_params[1] = {};
SIZE_T return_size = 0;
HRESULT hr = pawnio_execute(handle, "ioctl_pio_read",
    in_params, 1, out_params, 1, &return_size);

// Write a byte to EC port
ULONG64 in_params[] = {port, value};
HRESULT hr = pawnio_execute(handle, "ioctl_pio_write",
    in_params, 2, nullptr, 0, nullptr);
```

### Build Requirements for PawnIO

- PawnIOLib source added as git submodule at `thirdparty/PawnIOLib/`
- Compiled directly into executable (define `PawnIOLib_EXPORTS`)
- Link against `ntdll.lib` (PawnIOLib uses NT native APIs)
- `LpcACPIEC.bin` must be in working directory alongside executable

### User Requirements for PawnIO

- PawnIO driver installed: `winget install namazso.PawnIO` or https://pawnio.eu
- Administrator privileges

## ThinkPad EC Register Map (Reference Model)

The ThinkPad is the first and most thoroughly tested model family.

### Registers

| Offset     | Size    | Purpose |
|------------|---------|---------|
| 0x2F       | 1 byte  | Fan control. Bit 7 = BIOS mode (0x80). Bits 0-6 = manual level (0-7). |
| 0x31       | 1 byte  | Fan selector. 0x00 = Fan 1, 0x01 = Fan 2. |
| 0x78-0x7F  | 8 bytes | Temperature sensors (group 1) |
| 0x84-0x85  | 2 bytes | Fan RPM (little-endian 16-bit). RPM = (0x85 << 8) | 0x84. |
| 0xC0-0xC3  | 4 bytes | Temperature sensors (group 2, extended) - NOT reliable on all models |

### Temperature Sensors (up to 12)

| Index | Offset | Default Name | Location |
|-------|--------|-------------|----------|
| 0     | 0x78   | cpu         | Main processor |
| 1     | 0x79   | aps         | Accelerometer / HDD protection |
| 2     | 0x7A   | crd         | PCMCIA slot area |
| 3     | 0x7B   | gpu         | Graphics processor |
| 4     | 0x7C   | bat         | Battery |
| 5     | 0x7D   | x7d         | Usually N/A |
| 6     | 0x7E   | bat         | Battery 2 |
| 7     | 0x7F   | x7f         | Usually N/A |
| 8     | 0xC0   | bus         | Unknown |
| 9     | 0xC1   | pci         | Mini-PCI / WLAN / Southbridge |
| 10    | 0xC2   | pwr         | Power supply |
| 11    | 0xC3   | xc3         | Usually N/A |

### Fan Control Modes

- **BIOS mode**: Write `0x80` to 0x2F. EC firmware controls the fan.
- **Smart mode**: Poll temps, lookup fan level from curve, write level (0x00-0x7F) to 0x2F.
- **Manual mode**: Write user-specified level (0-7) to 0x2F.

### Dual-Fan Protocol

1. Write `0x00` to 0x31 (select Fan 1)
2. Write fan level to 0x2F
3. Sleep 100ms
4. Write `0x01` to 0x31 (select Fan 2)
5. Write fan level to 0x2F
6. Sleep 100ms
7. Read back both fans to verify (retry up to 5 times on mismatch)

## EC Protocol Pitfalls

- **Noise rejection**: Two consecutive EC reads must be within 2 degrees to accept.
  Exact match is too strict - active sensors update between reads.
- **1ms settle delay** after each EC read prevents bus contention with the ACPI driver.
  Without this, subsequent reads can return stale data.
- **RPM register returns 0xFFFF** as a tachometer error code at certain fan speeds.
  Must be rejected, not displayed as 65535 RPM.
- **Register 0x2F initialization bug**: EC does not correctly initialize this register
  on boot. First read may not reflect actual fan state. Write 0x80 (BIOS mode) as
  initialization step.
- **Cross-process mutex is critical**: Named mutex `Access_EC` prevents collisions with
  other fan control tools or BIOS EC access. Without it, reads/writes corrupt.

## ThinkPad T480 Hardware Quirks (Verified)

- **Extended sensors (0xC0-0xC3)**: NOT thermal registers on T480. These offsets are
  repurposed in newer EC firmware. Register 0xC2 consistently fails to read (IBF timeout).
- **GPU sensor (0x7B)**: Returns 0x01 when dGPU is powered off (NVIDIA Optimus).
  Filtered by kTempMin=10.
- **Only 2-3 sensors reliably active** on any given model (CPU, APS, GPU when dGPU on).
- **Dual-fan detection**: Write 0x01 to fan selector 0x31, readback confirms Fan 2 present.

## BIOS Override Behavior (Newer ThinkPads, ~2020+)

EC firmware aggressively reasserts its own fan control after userspace writes to 0x2F.
Creates a tug-of-war. Fan speed changes take 5+ seconds. Fans may desync. This is a
firmware limitation - no driver can fix it.

## Safety Requirements (MUST implement for any protocol)

- On program exit or suspension: **always** set fan to BIOS/automatic mode.
- On lid close (`GUID_LIDSWITCH_STATE_CHANGE`): switch to BIOS mode, restore on open.
- After `MaxReadErrors` consecutive read failures (default 10): revert to BIOS mode.
- Manual mode must auto-exit to Smart mode when any sensor exceeds `ManModeExit` threshold.
- Sensor values of 0x00 or 0x80 are invalid. Values < 10 or > 127 are rejected.
- Fan RPM value 0xFFFF is rejected as a tachometer error code.

## Known Incompatible Models (ThinkPad)

| Model              | Problem                                |
|--------------------|----------------------------------------|
| ThinkBook 13s Gen2 | Cannot read EC registers (not a ThinkPad EC) |
| ThinkPad P50       | Causes blackscreen on EC access        |
| ThinkPad L560      | Fan control does not work (firmware limitation) |

## Design Constraints

- **Mock should simulate at the port level**, not at the register level. The mock
  tracks IBF/OBF status flags and responds to the read/write command handshake. This
  exercises the real protocol code (handshake timing, flag polling) rather than bypassing it.
- **Fan controller must be hardware-agnostic**: The fan control state machine (curves,
  hysteresis, modes, safety) should have zero hardware dependencies. It just needs "read temp"
  and "write fan level".
- **RAII for driver handles**: Driver handle management via RAII (open in constructor,
  close in destructor) prevents resource leaks on error paths.
- **Signal handlers must be minimal**: Can't safely call complex C++ code from signal
  handlers. Just write BIOS mode directly and `_Exit()`.

## Config File Format (INI)

File: `unifc.ini` in the executable directory. Comment lines start with `/`, `#`, or `;`.

### Key Settings

```ini
Model=ThinkPad T480   # Laptop model (selects protocol + register map)
Active=2              # 0=read-only, 1=disabled, 2=smart, 3=manual
Cycle=5               # Temperature poll interval (seconds)
ManFanSpeed=0         # Manual mode default level
ManModeExit=78        # Auto-exit manual mode temperature (C)
MaxReadErrors=10      # Consecutive failures before BIOS fallback
```

### Fan Curve Format

```ini
Level=TEMP FAN HYST_UP HYST_DOWN
Level=50 0 0 0        # Fan off below 50C
Level=60 3 0 0        # Level 3 at 60C
Level=70 5 2 0        # Level 5 at 72C (hysteresis: +2 to activate)
Level=80 7 0 0        # Level 7 at 80C
Level=-1 0            # Terminator (required)
```

## Conventions

- Follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
  Functions are PascalCase, constants are kCamelCase, enum values are kPascalCase,
  private members have trailing underscore.
- Modern C++17: `std::string`, `std::vector`, `std::optional`, `std::mutex`, `std::thread`,
  structured bindings. No C-style strings or manual memory management except at driver
  boundaries (PawnIO).
- No exceptions across module boundaries. Use return codes or `std::optional` for errors.
- Prefer `uint8_t`/`uint16_t` for hardware register values.
- Named constants over magic numbers. Every register offset and command has a named constant.
- All public functions, structs, enums, and struct members must have Doxygen docstrings
  using `@`-style tags (`@brief`, `@param`, `@return`). Static helper functions in `.cpp`
  files should also be documented.

## Dependencies

- **PawnIO driver** (for ACPI EC protocol): `winget install namazso.PawnIO` or https://pawnio.eu
- **PawnIOLib**: Git submodule at `thirdparty/PawnIOLib/`
- **LpcACPIEC.bin**: Signed PawnIO module for ACPI EC port access

## References

- [NBFC - NoteBook FanControl](https://github.com/hirschmann/nbfc) - 200+ model configs
- [TPFanCtrl2](https://github.com/Shuzhengz/TPFanCtrl2) - ThinkPad-specific reference
- [thinkfan](https://github.com/vmatare/thinkfan) - Linux fan control
- [ACPI EC Specification](https://uefi.org/specs/ACPI/)
- [ThinkWiki - Fan Control](https://www.thinkwiki.org/wiki/How_to_control_fan_speed)
- [PawnIO](https://github.com/namazso/PawnIO) - Signed kernel I/O driver
