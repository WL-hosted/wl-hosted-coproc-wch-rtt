# bl_s2: stage-2 bootloader (bare-metal, flash offset 448K, 32K).
#
# Uses the WCH SDK at ../packages/ch32v307_sdk-v2.7 (referenced in place,
# not copied). system_ch32v30x.c / ch32v30x_conf.h are copied verbatim from
# the official CH32V307 EVT (FSMC/NORFLASH example, 144MHz HSE) — the
# rtt-patch variants are RT-Thread-specific and are NOT used.
#
# Standalone build:
#   cmake -S bl_s2 -B bl_s2/build-debug && cmake --build bl_s2/build-debug
# Integrated build: added via custom.cmake's ExternalProject_Add, which
# passes CMAKE_C_COMPILER/CMAKE_ASM_COMPILER/CMAKE_OBJCOPY/CMAKE_SIZE/... .

cmake_minimum_required(VERSION 3.16)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Toolchain defaults (overridable via -DCMAKE_*)
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-gcc")
endif()
if(NOT CMAKE_ASM_COMPILER)
    set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
endif()
if(NOT CMAKE_AR)
    set(CMAKE_AR "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-ar")
endif()
if(NOT CMAKE_RANLIB)
    set(CMAKE_RANLIB "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-ranlib")
endif()
if(NOT CMAKE_OBJCOPY)
    set(CMAKE_OBJCOPY "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-objcopy")
endif()
if(NOT CMAKE_SIZE)
    set(CMAKE_SIZE "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-size")
endif()

project(bl_s2 C ASM)

set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../packages/ch32v307_sdk-v2.7")
set(SRC_DIR "${SDK_ROOT}/EVT/EXAM/SRC")

add_executable(bl_s2
    main.c
    system_ch32v30x.c
    startup_ch32v30x_D8C.S
    ${SRC_DIR}/Core/core_riscv.c
    ${SRC_DIR}/Debug/debug.c
    ${SRC_DIR}/Peripheral/src/ch32v30x_gpio.c
    ${SRC_DIR}/Peripheral/src/ch32v30x_usart.c
    ${SRC_DIR}/Peripheral/src/ch32v30x_rcc.c
)

target_include_directories(bl_s2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SRC_DIR}/Core
    ${SRC_DIR}/Debug
    ${SRC_DIR}/Peripheral/inc
)

target_compile_options(bl_s2 PRIVATE
    -march=rv32imac -mabi=ilp32 -DUSE_PLIC -DUSE_M_TIME
    -mcmodel=medany -msmall-data-limit=8 -Os -g3 -Wall -Werror
)

target_link_options(bl_s2 PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/bl_s2.ld
    -nostartfiles -lc
    -Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/bl_s2.map
)

# track the linker script so ld changes trigger a relink
set_target_properties(bl_s2 PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bl_s2.ld")

add_custom_command(TARGET bl_s2 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:bl_s2> ${CMAKE_CURRENT_BINARY_DIR}/bl_s2.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:bl_s2> ${CMAKE_CURRENT_BINARY_DIR}/bl_s2.hex
    COMMAND ${CMAKE_SIZE}    $<TARGET_FILE:bl_s2>
    COMMENT "Generate bl_s2.bin / bl_s2.hex"
    VERBATIM
)
