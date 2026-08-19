# wl-hosted-coproc-wch-rtt Agent Guide

CH32V307VC / CH32V317WCU6 + RT-Thread 5.0.3 的 WL-hosted 协处理器适配仓库：CherryUSB device 跑在 USBHS（PB6/PB7，USB 2.0 HS），承载标准 Wire/RPC 帧；数据面为有线 ETH（channel 0x0a）。宿主侧为 `wl-hosted-host-macos-sim --usb 1a86:8210`。

工作区根规则见 `../AGENTS.md`；本文件优先。

## 配置工作流（Kconfig → rtconfig.h）

所有 RT-Thread 配置改动**只写入 `.config`**，然后执行：

```sh
scons --pyconfig-silent
```

由它重新生成 `rtconfig.h`。**不要手改 `rtconfig.h`**。`.config` 不入库（gitignored/untracked），`rtconfig.h` 入库。

## 构建与下载

- 根 `CMakeLists.txt` 由 `scons --target=cmake` 生成，**gitignored，不手改**；tracked 的 `custom.cmake` 被其自动 include（flash targets、bl_s1/bl_s2 在这里；**wl-hosted-core 不在这里接入**——core 仓库根部的 `SConscript` 由 scons 的源码 walk 自动收集为 `wlh_core` group，无需 custom.cmake 接线）。
- 新增源码：放进 `applications/` 下对应分层目录（`app/`、`backends/`、`transports/<link>/`，均带 SConscript），改动 SConscript 后重新 `scons --target=cmake` 再配置 CMake。
- 构建目标由 `-DWLH_TARGET=ch32v307|ch32v317` 选择，未指定时默认 `ch32v307`；两目标必须使用独立 build 目录。
- 构建：`cmake -S . -B build-v307 -DCMAKE_BUILD_TYPE=Debug -DWLH_TARGET=ch32v307 && cmake --build build-v307 --parallel`。
- 下载：`cmake --build build-v307 --target flash`（app 经 wlink）；`flash_all`（合并 bl_s1+app+bl_s2，需要 mergehex-rs）。
- 工具链：`riscv32-wch-elf-gcc`（rtconfig.py 指向本机安装路径）。
- CI（`.github/workflows/build.yml`）走同一路径，改动构建接线时保持 CI 可用。

## 启动链与分区

`part_table.md` 为准：bl_s1（0x0，2K，跳 bl_s2）→ app_s1（0x800，222K，本工程）→ app_s2（224K，预留）→ easyflash（446K）→ bl_s2（448K，32K，上电打印后 2s 恢复窗口，经 USART1 PA9/PA10 @115200）。

## 硬件约定

- 控制台/FinSH：USART1 PA9/PA10 @115200（与 bl_s2 同一串口）。
- USB device：USBHS 控制器，PB6(DM)/PB7(DP)，内置 HS PHY，480Mbps；CherryUSB `port/ch32/usb_dc_usbhs.c`（自上游 CherryUSB 移植，vendored 在 rt-thread 树内）。
- 以太网：CH32V307 使用内置 10M PHY（`EXTEN_ETH_10M_EN`）；CH32V317W EVT 使用集成 10/100M PHY、100 MHz ETH 时钟和 PD8/PE8–PE15 RMII 配置。两者都不走 lwIP/netdev，L2 帧直接桥接进 coproc-core。
- USB 描述符：VID:PID `1a86:8210`，interface 0 vendor class，bulk OUT 0x01 / IN 0x81（必须与 host-macos-sim 的硬编码一致）。

## 依赖边界

```text
wl-hosted-coproc-wch-rtt -> wl-hosted-core（git submodule，core/）
```

只用 `coproc-core` + `protocol` + `common/osal`（RTT 后端）+ `common/log`（RTT_ULOG 后端）。Submodule 变更遵守工作区 AGENTS.md 的 SUBMODULE.lock 纪律。

## 质量要求

- `-Wall -Werror`；所有队列/ring/pool 有界静态分配（64KB RAM 预算）。
- CherryUSB 端点 DMA 要求 4 字节对齐缓冲。
- 不在 ISR 里调用 coproc-core 的 `wlh_coproc_on_frame` 或 TX completion。
- USB 总线复位/断开必须丢弃旧 session 并触发 core 重启（重 Hello）。
- EMAC TX 描述符只能按环序分配（DMA 挂起后从当前指针顺序取描述符；乱序占用会把已武装帧困在挂起点之后）。TX 环深必须 ≥ `WLH_INITIAL_CREDIT`：环满时 backend 返回 REJECTED，core 丢帧只回 credit——对 UDP 无感，对 TCP 突发是致命的。
