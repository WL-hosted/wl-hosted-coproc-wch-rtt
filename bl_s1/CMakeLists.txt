# bl_s1: stage-1 bootloader (pure assembly, flash offset 0, 2K).
#
# Standalone build:
#   cmake -S bl_s1 -B bl_s1/build-debug && cmake --build bl_s1/build-debug
# Integrated build: added via custom.cmake's ExternalProject_Add, which
# passes CMAKE_ASM_COMPILER/CMAKE_OBJCOPY/CMAKE_SIZE explicitly.

cmake_minimum_required(VERSION 3.16)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Toolchain defaults (overridable via -DCMAKE_ASM_COMPILER=...)
if(NOT CMAKE_ASM_COMPILER)
    set(CMAKE_ASM_COMPILER "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-gcc")
endif()
if(NOT CMAKE_OBJCOPY)
    set(CMAKE_OBJCOPY "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-objcopy")
endif()
if(NOT CMAKE_SIZE)
    set(CMAKE_SIZE "/Users/kai/DevTools/riscv32-wch-elf-gcc-15/bin/riscv32-wch-elf-size")
endif()

project(bl_s1 ASM)

set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -Os -Wall -Werror")

add_executable(bl_s1 bl_s1.S)

# track the linker script so ld changes trigger a relink
set_target_properties(bl_s1 PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bl_s1.ld")

target_link_options(bl_s1 PRIVATE
    -T ${CMAKE_CURRENT_SOURCE_DIR}/bl_s1.ld
    -nostartfiles
    -Wl,--gc-sections,-Map=${CMAKE_CURRENT_BINARY_DIR}/bl_s1.map
)

add_custom_command(TARGET bl_s1 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:bl_s1> ${CMAKE_CURRENT_BINARY_DIR}/bl_s1.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:bl_s1> ${CMAKE_CURRENT_BINARY_DIR}/bl_s1.hex
    COMMAND ${CMAKE_SIZE}    $<TARGET_FILE:bl_s1>
    COMMENT "Generate bl_s1.bin / bl_s1.hex"
    VERBATIM
)
