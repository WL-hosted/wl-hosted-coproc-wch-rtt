# WL-hosted target profile. The SCons-generated project contains the common
# CH32V30x source set; this cache option selects the application/PHY profile.
set(WLH_TARGET "ch32v307" CACHE STRING "WL-hosted WCH target")
set_property(CACHE WLH_TARGET PROPERTY STRINGS ch32v307 ch32v317)

if(WLH_TARGET STREQUAL "ch32v307")
    target_compile_definitions(rtt_wlh PRIVATE WLH_TARGET_CH32V307=1)
elseif(WLH_TARGET STREQUAL "ch32v317")
    target_compile_definitions(rtt_wlh PRIVATE WLH_TARGET_CH32V317=1)
else()
    message(FATAL_ERROR
        "Unsupported WLH_TARGET='${WLH_TARGET}'; expected ch32v307 or ch32v317"
    )
endif()
target_compile_options(rtt_wlh PRIVATE -Wall -Werror)

set(_WLHOSTED_ARTIFACT_STEM "wlhosted-${WLH_TARGET}")
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/rtthread.elf
        ${CMAKE_CURRENT_BINARY_DIR}/${_WLHOSTED_ARTIFACT_STEM}.elf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_BINARY_DIR}/rtthread.hex
        ${CMAKE_CURRENT_BINARY_DIR}/${_WLHOSTED_ARTIFACT_STEM}.hex
    COMMENT "Create ${WLH_TARGET} firmware artifacts"
    VERBATIM
)

# 通过 WCH-Link (wlink) 下载固件到芯片 flash。
# 用法：cmake --build <build-dir> --target flash
# 下载 hex 格式固件（由 rtconfig.py 的 POST_ACTION 在构建后生成），
# 地址由 hex 记录携带（0x08000000），无需指定基地址。
add_custom_target(flash
    DEPENDS ${CMAKE_PROJECT_NAME}.elf
    COMMAND wlink flash ${CMAKE_CURRENT_BINARY_DIR}/rtthread.hex
    COMMENT "Downloading rtthread.hex via WCH-Link (wlink)..."
    VERBATIM
)

# bl_s1 / bl_s2: bare-metal bootloader projects (see part_table.md).
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

# BUILD_ALWAYS: ExternalProject 默认只在外部工程 CMakeLists 变化时重建，
# 不感知 main.c 等源文件改动；打开后每次父构建都会进入外部工程的 make，
# 由它按 mtime 增量重建。
ExternalProject_Add(bl_s1
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bl_s1
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/bl_s1-build
    CMAKE_ARGS ${_BL_CMAKE_ARGS}
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON
)

ExternalProject_Add(bl_s2
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bl_s2
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/bl_s2-build
    CMAKE_ARGS ${_BL_CMAKE_ARGS}
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON
)

# merge_hex: 合并 bl_s1 + rtthread(app) + bl_s2 三个 hex，不烧录。
# 需要 mergehex-rs（https://crates.io/crates/mergehex-rs）；PATH 中找不到时
# 可 -DMERGEHEX=/path/to/mergehex-rs 显式指定。
find_program(MERGEHEX mergehex-rs)
if(NOT MERGEHEX)
    message(FATAL_ERROR "merge_hex/flash_all requires mergehex-rs; install it or pass -DMERGEHEX=/path/to/mergehex-rs")
endif()

set(_WLHOSTED_MERGED_HEX
    ${CMAKE_CURRENT_BINARY_DIR}/${_WLHOSTED_ARTIFACT_STEM}-all.hex
)

add_custom_target(merge_hex
    DEPENDS bl_s1 bl_s2 ${CMAKE_PROJECT_NAME}.elf
    COMMAND ${MERGEHEX}
        -i ${CMAKE_CURRENT_BINARY_DIR}/bl_s1-build/bl_s1.hex
        -i ${CMAKE_CURRENT_BINARY_DIR}/rtthread.hex
        -i ${CMAKE_CURRENT_BINARY_DIR}/bl_s2-build/bl_s2.hex
        -o ${_WLHOSTED_MERGED_HEX}
    COMMENT "Merge bl_s1 + rtthread + bl_s2 into ${_WLHOSTED_MERGED_HEX}"
    VERBATIM
)

# flash_all: 先合并三个 hex，再经 WCH-Link (wlink) 一次性烧录。
add_custom_target(flash_all
    DEPENDS merge_hex
    COMMAND wlink flash ${_WLHOSTED_MERGED_HEX}
    COMMENT "Flash merged ${_WLHOSTED_MERGED_HEX} via WCH-Link (wlink)..."
    VERBATIM
)

# wl-hosted-core 经 core/SConscript（RT-Thread group）接入，由 scons 统一收集
# 进生成的 CMakeLists；这里不放任何 core 接线。
