# 通过 WCH-Link (wlink) 下载固件到芯片 flash。
# 用法：cmake --build build-debug --target flash
# 下载 hex 格式固件（由 rtconfig.py 的 POST_ACTION 在构建后生成），
# 地址由 hex 记录携带（0x08000000），无需指定基地址。
add_custom_target(flash
    DEPENDS ${CMAKE_PROJECT_NAME}.elf
    COMMAND wlink flash ${CMAKE_CURRENT_BINARY_DIR}/rtthread.hex
    COMMENT "Downloading rtthread.hex via WCH-Link (wlink)..."
    VERBATIM
)

# bl_s1 / bl_s2: bare-metal bootloader projects (see flash_table.md).
# They are standalone CMake projects built in isolated sub-build dirs so the
# RT-Thread compile flags do not leak in. Toolchain paths come from the
# parent project (scons-generated CMakeLists already points to the WCH tools).
include(ExternalProject)

set(_BL_CMAKE_ARGS
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_ASM_COMPILER=${CMAKE_ASM_COMPILER}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
)
if(CMAKE_OBJCOPY)
    list(APPEND _BL_CMAKE_ARGS -DCMAKE_OBJCOPY=${CMAKE_OBJCOPY})
endif()
if(CMAKE_SIZE)
    list(APPEND _BL_CMAKE_ARGS -DCMAKE_SIZE=${CMAKE_SIZE})
endif()
if(CMAKE_AR)
    list(APPEND _BL_CMAKE_ARGS -DCMAKE_AR=${CMAKE_AR})
endif()
if(CMAKE_RANLIB)
    list(APPEND _BL_CMAKE_ARGS -DCMAKE_RANLIB=${CMAKE_RANLIB})
endif()

ExternalProject_Add(bl_s1
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bl_s1
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/bl_s1-build
    CMAKE_ARGS ${_BL_CMAKE_ARGS}
    INSTALL_COMMAND ""
)

ExternalProject_Add(bl_s2
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bl_s2
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/bl_s2-build
    CMAKE_ARGS ${_BL_CMAKE_ARGS}
    INSTALL_COMMAND ""
)

# flash_all: 合并 bl_s1 + rtthread(app) + bl_s2 三个 hex 后一次性烧录。
# 需要 mergehex-rs（https://crates.io/crates/mergehex-rs）；PATH 中找不到时
# 可 -DMERGEHEX=/path/to/mergehex-rs 显式指定。
find_program(MERGEHEX mergehex-rs)
if(NOT MERGEHEX)
    message(FATAL_ERROR "flash_all requires mergehex-rs; install it or pass -DMERGEHEX=/path/to/mergehex-rs")
endif()

set(_WLHOSTED_MERGED_HEX ${CMAKE_CURRENT_BINARY_DIR}/wlhosted_all.hex)

add_custom_target(flash_all
    DEPENDS bl_s1 bl_s2 ${CMAKE_PROJECT_NAME}.elf
    COMMAND ${MERGEHEX}
        -i ${CMAKE_CURRENT_BINARY_DIR}/bl_s1-build/bl_s1.hex
        -i ${CMAKE_CURRENT_BINARY_DIR}/rtthread.hex
        -i ${CMAKE_CURRENT_BINARY_DIR}/bl_s2-build/bl_s2.hex
        -o ${_WLHOSTED_MERGED_HEX}
    COMMAND wlink flash ${_WLHOSTED_MERGED_HEX}
    COMMENT "Merge bl_s1 + rtthread + bl_s2 into ${_WLHOSTED_MERGED_HEX} and flash via WCH-Link (wlink)..."
    VERBATIM
)
