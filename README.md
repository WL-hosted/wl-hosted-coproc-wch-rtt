# wl-hosted-coproc-wch-rtt

CH32V307VC + RT-Thread 5.0.3 的 WL-hosted 协处理器固件：CherryUSB device（USBHS，PB6/PB7，USB 2.0 HS）承载标准 Wire/RPC 帧，数据面为有线 Ethernet（channel 0x0a，片内 10M PHY）。宿主侧使用 `wl-hosted-host-macos-sim --usb 1a86:8210`。

## 依赖

- `core/` — [wl-hosted-core](https://github.com/WL-hosted/wl-hosted-core.git) submodule（coproc-core + protocol + common/osal RTT 后端 + common/log RTT_ULOG 后端），pin 见 `SUBMODULE.lock`。
- 工具链：`riscv32-wch-elf-gcc`（见 `rtconfig.py` 的 `EXEC_PATH`）。
- 下载：`wlink`（WCH-Link）；合并镜像需要 `mergehex-rs`。

克隆后：

```sh
git submodule update --init --recursive
git submodule status --recursive   # 与 SUBMODULE.lock 一致
```

## 构建 / 下载

```sh
# 配置变更：只改 .config，然后
scons --pyconfig-silent            # 重新生成 rtconfig.h

# 源码/SConscript 变更后重新生成构建系统（CMakeLists.txt 为生成物，不入库）
scons --target=cmake
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel

cmake --build build-debug --target flash      # 仅 app（wlink）
cmake --build build-debug --target flash_all  # bl_s1+app+bl_s2 合并烧录
```

## 布局

- `applications/` — RT-Thread 应用入口（FinSH 控制台在 USART1 PA9/PA10 @115200）。
- `applications/wlh/` — WL-hosted 适配层：USB bulk 传输、coproc-core 装配、ETH 后端、msh 诊断命令、CherryUSB `usb_config.h`。
- `bl_s1/`、`bl_s2/` — 两级 bootloader；分区见 `part_table.md`。
- `core/` — wl-hosted-core submodule，不要在此目录内改代码。
- 更多约定见 `AGENTS.md`。
