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
