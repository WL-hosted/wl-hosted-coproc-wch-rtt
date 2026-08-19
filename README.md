# wl-hosted-coproc-wch-rtt

CH32V307VC / CH32V317WCU6 + RT-Thread 5.0.3 的 WL-hosted 协处理器固件：CherryUSB device（USBHS，PB6/PB7，USB 2.0 HS）承载标准 Wire/RPC 帧，数据面为有线 Ethernet（channel 0x0a）。V307 使用片内 10M PHY，V317W EVT 使用集成 10/100M PHY。宿主侧使用 `wl-hosted-host-macos-sim --usb 1a86:8210`。

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

# 默认目标：CH32V307VC
cmake -S . -B build-v307 -DCMAKE_BUILD_TYPE=Debug \
  -DWLH_TARGET=ch32v307
cmake --build build-v307 --parallel

# CH32V317WCU6 EVT
cmake -S . -B build-v317 -DCMAKE_BUILD_TYPE=Debug \
  -DWLH_TARGET=ch32v317
cmake --build build-v317 --parallel

cmake --build build-v307 --target flash      # 仅 app（wlink）
cmake --build build-v307 --target flash_all  # bl_s1+app+bl_s2 合并烧录
```

`WLH_TARGET` 只接受 `ch32v307` 和 `ch32v317`，未指定时兼容默认到
`ch32v307`。构建会生成带目标名的应用 ELF/HEX 和合并镜像，例如
`wlhosted-ch32v317.elf`、`wlhosted-ch32v317.hex`、
`wlhosted-ch32v317-all.hex`。

V317 PHY 初始化参考 WCH EVT 的
`EXAM/ETH/MAC_RAW/ETH_Driver/eth_driver_CH32V317.c`：ETH PLL3 为 100 MHz，
使用 PD8/PE8–PE15 RMII 配置及官方 PHY 分页寄存器初始化。两目标共享当前
SDK、USBHS、启动文件、分区和 bootloader。

## 布局

- `applications/` — RT-Thread 应用目录（main/ 等价物，FinSH 控制台在 USART1 PA9/PA10 @115200），内部分层：
  - `app/` — 入口 `main.c`、coproc-core 装配（`wlh_app`）、`firmware_config.h`、msh 诊断命令；
  - `backends/` — ETH 后端与 PHY 初始化；
  - `transports/` — 链路公共头 `transport.h`；`usb/` 为 USB bulk 传输与 CherryUSB `usb_config.h`。
- `bl_s1/`、`bl_s2/` — 两级 bootloader；分区见 `part_table.md`。
- `core/` — wl-hosted-core submodule，不要在此目录内改代码。
- 更多约定见 `AGENTS.md`。
