cmake_minimum_required(VERSION 3.20)
project(unifc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build with mock protocol (no hardware access, no PawnIO dependency)
option(UNIFC_MOCK "Use mock EC for development without hardware" OFF)

add_executable(unifc
    src/main.cpp
    src/config/config.cpp
    src/protocol/mock_protocol.cpp
    src/protocol/acpi_ec_protocol.cpp
    src/model/fan_write_strategy.cpp
    src/model/laptop.cpp
    src/model/laptop_factory.cpp
    src/fan/fan_controller.cpp
)
target_include_directories(unifc PRIVATE src)

if(UNIFC_MOCK)
    target_compile_definitions(unifc PRIVATE UNIFC_MOCK)
    message(STATUS "Building with MOCK protocol (no real hardware access)")
else()
    # PawnIO usermode library - compiled directly into the executable
    target_sources(unifc PRIVATE
        thirdparty/PawnIOLib/PawnIOLib/PawnIOLib.cpp
    )
    target_compile_definitions(unifc PRIVATE PawnIOLib_EXPORTS)
    target_include_directories(unifc PRIVATE
        thirdparty/PawnIOLib/PawnIOLib
        thirdparty/PawnIOLib/PawnIOLib/include
        thirdparty/PawnIOLib/PawnIO/include
    )
    target_link_libraries(unifc PRIVATE ntdll.lib)
    message(STATUS "Building with PawnIO EC access")
endif()
