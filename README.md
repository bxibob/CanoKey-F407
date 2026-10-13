# CanoKey 移植到 STM32F407（VET6，512KB Flash）

把 [canokeys/canokey-stm32](https://github.com/canokeys/canokey-stm32) 从 STM32L432 移植到
STM32F407VET6 开发板，做成一个 USB 安全令牌：OpenPGP / PIV / FIDO2(U2F) / OATH / Admin 全功能，
通过 CCID + CTAPHID + WebUSB + Keyboard-HID 与主机通信。

> ⚠️ **安全警告**：CanoKey 的 STM32 版本没有安全存储保护，SWD 接口一直接着就意味着
> 私钥可以被读出。这个移植只能当**学习/实验/备用**令牌用，**不要**拿它保护真实资产。
> 真要用于生产请买 CanoKey Pigeon 或关闭 SWD（JTAG 熔丝）+ 开 RDP Level 2。

## 硬件要求

| 项目 | 要求 |
| --- | --- |
| MCU | STM32F407VET6（512KB Flash / 192KB RAM） |
| 晶振 | 8MHz（默认；25MHz 见下文） |
| USB | 板载 USB Device 接口接在 **PA11(DM) / PA12(DP)**，即 OTG FS |
| LED | 一个 GPIO（默认 PC13），用于"需要触摸确认"的闪烁提示 |
| 按键 | 一个 GPIO（默认 PA0），替代原厂的电容触摸做用户确认 |
| 烧录 | ST-Link V2（SWD） |
| 调试串口（可选） | USART1：PA9(TX) / PA10(RX)，115200 |

如果你的板子 USB 口不是接在 PA11/PA12（例如只引出了 OTG HS + 外部 PHY），
那这个口不能直接当 USB 设备用，需要自己飞线到 PA11/PA12。

## Flash 分区（这是本移植的关键决策）

F407VET6 的扇区划分是：sector 0~3 各 16KB、sector 4 是 64KB、sector 5~7 各 128KB。
littlefs 的块大小必须等于物理擦除粒度，所以这个文件系统的布局完全被扇区结构绑架：

- 用 16KB 小扇区 → 最多只有 3 个块（sector 0 必须留给上电向量表）
- 用末尾 128KB 扇区 → 只有 2 个块

**实测 3 个 16KB 块根本跑不起来**：littlefs 中大于 `LFS_ATTR_MAX`(1022 字节) 的文件
无法内联、必须独占一个块，而 `sizeof(rsa_key_t)` 按 4096 位定义有 1288 字节，
光 OpenPGP 初始化就要建 3 个密钥文件，3 个块瞬间写满（`FLASH_USAGE` 报 48KB/48KB），
OpenPGP applet 装不上、一直停在 terminated 状态。

**最终方案：用末尾两个 128KB 扇区，并让所有文件都小于 1022 字节从而全部内联。**
这样就不存在"独立数据块"的需求，2 个块（元数据对）也够用。

```
0x08000000  sector 0~5  256KB  应用程序（向量表就在 0x08000000，无 boot stub）
0x08040000  sector 6~7  256KB  littlefs（2 个 128KB 块）
```

配套的三处尺寸约束（**改大任何一处都会让文件系统再次写满**）：

| 位置 | 原值 | 现值 | 原因 |
| --- | --- | --- | --- |
| `canokey-crypto/include/rsa.h` `RSA_N_BIT_MAX` | 4096 | **2048** | `sizeof(rsa_key_t)` 1288 → 648 字节，密钥文件可内联 |
| `applets/openpgp/openpgp.c` `MAX_CERT_LENGTH` | 0x480 | **0x3C0** | 1152 → 960 字节，证书文件可内联 |
| `include/fs.h` `LFS_CACHE_SIZE` | 512 | **1024** | `inline_max` 取 `min(cache_size, 1022, block_size/8)` |

另外 `lfs_init.c` 里 `block_cycles` 设为 `-1`：只有 2 个块，块级磨损均衡到期时
要搬迁元数据却没有空闲块可用，必须关掉（元数据仍会在两个块间交替写入）。

代价：不支持 RSA 3072/4096 密钥，证书上限 960 字节，擦除块变大（128KB，约 1s）
所以首次上电格式化会慢一点。好处是烧录时镜像只覆盖 sector 0~5，
**重复烧固件不会碰到文件系统区**，密钥和凭证能保留。

## 目录结构

```
canokey-f407/
├── CMakeLists.txt            # 构建配置（MCU、TinyUSB、烧录目标）
├── toolchain.cmake
├── STM32F407VETx_FLASH.ld    # 链接脚本（应用 256KB + LFS 256KB 两段）
├── mkimage.py                # 镜像校验（拒绝越过文件系统区的固件）
├── build.bat / build.sh      # 一键构建
├── Inc/
│   ├── main.h                # ★ LED / 按键 / 串口引脚，按你的板子改这里
│   ├── device-config.h
│   ├── stm32f4xx_hal_conf.h  # 只开了用到的 HAL 模块
│   └── mbedtls-config.h      # 从上游拷来
├── Src/
│   ├── main.c                # ★ 时钟(168MHz/USB 48MHz)、GPIO、RNG、USB
│   ├── device.c              # ★ LED、按键状态机、超时定时器、自旋锁
│   ├── lfs_init.c            # ★ 内部 flash littlefs 后端（sector 6~7，各 128KB）
│   ├── stm32f4xx_it.c        # SysTick / TIM6 / OTG_FS 中断
│   ├── usb.c, retarget.c, admin_vendor.c
│   ├── system_stm32f4xx.c
│   └── startup_stm32f407xx.s
├── Drivers/                  # STM32F4xx HAL + CMSIS
└── canokey-core/             # 上游核心（子模块）
```

## 构建

### 1. 准备工具链

- CMake ≥ 3.13
- GNU Arm Embedded Toolchain（10.3-2021.10 或更新）
- Ninja 或 make
- Python 3（mkimage.py 用）
- ST-Link 工具：`st-flash`（[stlink](https://github.com/stlink-org/stlink)）或 STM32CubeProgrammer

### 2. 编译

```bat
build.bat            :: 8MHz 晶振
build.bat 25000000   :: 25MHz 晶振
```

脚本会自动在 `../tools/` 里找绿色版工具链（cmake / ninja / arm-none-eabi-gcc），
也可以用环境变量指定：

```bat
set CMAKE_EXE=C:\cmake\bin\cmake.exe
set NINJA_EXE=C:\ninja\ninja.exe
set ARMGCC_DIR=C:\gcc-arm\bin
build.bat
```

或手动：

```bash
mkdir build && cd build
cmake -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=../toolchain.cmake ^
  -DCROSS_COMPILE=<你的工具链>/bin/arm-none-eabi- ^
  -DCMAKE_BUILD_TYPE=Release ^
  ..
ninja canokey-f407.bin
```

晶振是 25MHz 的板子加一个参数：

```bash
cmake -DHSE_VALUE=25000000 ...
```

产物：`build/canokey-f407.bin`

### 3. 烧录

SWD 接线（ST-Link V2）：

| ST-Link | 板子 |
| --- | --- |
| 3.3V | 3V3（若板子独立供电可不接） |
| GND | GND |
| SWDIO | PA13 |
| SWCLK | PA14 |

```bash
st-flash --reset write canokey-f407.bin 0x08000000
```

第一次烧录后建议先做一次全片擦除，保证文件系统区是干净的：

```bash
st-flash erase
st-flash --reset write canokey-f407.bin 0x08000000
```

> 镜像里文件系统区被填成 0xFF，所以后续只更新固件不会动到已写入的密钥。
> 如果 `st-flash` 整片擦除，令牌数据会丢失，需要重新初始化。

## 适配你的板子

### 引脚

编辑 `Inc/main.h`：

```c
#define LED_GPIO_Port GPIOC
#define LED_Pin       GPIO_PIN_13
#define LED_ACTIVE_HIGH 1

#define KEY_GPIO_Port GPIOA
#define KEY_Pin       GPIO_PIN_0
#define KEY_ACTIVE_HIGH 1   /* 按下为高填 1；按下接地填 0 */
```

常见板子：正点原子（LED PF9/PF10，KEY0=PE4 按下为低）、野火（LED PB0/PB1，KEY=PA0 按下为高）。

### 时钟

`Src/main.c` 的 `SystemClock_Config()`：HSE → PLL → 168MHz，PLLQ=7 得到 48MHz 给 USB 和 RNG。
换晶振频率只需改 `-DHSE_VALUE`，PLLM 会自动算（`HSE_VALUE / 1000000`）。

### MPU

`main()` 里的 `SetupMPU()` 默认注释掉。等 USB 能正常枚举后再打开，
它提供栈溢出保护和 SRAM 不可执行。

## 验证

插到电脑的 USB 口（板子的 USB Device 口，不是 ST-Link 那个口）：

1. **CCID / 智能卡**
   ```bash
   pcsc_scan            # Linux；应能看到 CCID 读卡器
   opensc-tool -a       # 列出 ATR
   gpg --card-status    # OpenPGP applet
   ```
2. **FIDO2 / WebUSB**：打开 <https://console.canokeys.org>（Chrome/Edge），
   走 WebUSB 直接管理；或用 `canokey-cli`。
3. **U2F/FIDO2 登录**：访问支持 FIDO2 的站点测试注册/认证，
   需要用户确认时板载 LED 会快闪，按下按键即可（短按=确认，长按>500ms=长触摸）。
4. **调试串口**：USART1 115200，上电会打印
   `CanoKey on STM32F407 starting...` / `Init FS` / `Init USB` / `Main Loop`。
   串口里发 `T` 可以模拟一次触摸，方便无按键时调试。

## 常见问题

| 现象 | 排查 |
| --- | --- |
| 电脑完全没反应 | 先确认 USB 线接的是 PA11/PA12 那个口；量一下 PA12 有没有 1.5k 上拉 |
| USB 枚举成未知设备 | 晶振频率写错了（8M/25M），看 `HSE_VALUE` |
| 串口一直没输出 | 检查 USART1 引脚、波特率 115200，以及是否 `ENABLE_DEBUG_OUTPUT=ON` |
| 卡在 `Init FS` | 第一次上电会自动格式化；若反复失败，整片擦除后重烧 |
| 触摸没反应 | 改 `main.h` 里的按键引脚和 `KEY_ACTIVE_HIGH` |
| 写入凭证后掉电丢失 | 检查 `lfs_init.c` 的 `FS_SECTOR_BASE` 与链接脚本的 LFS 段是否一致 |
| 串口打印正常但 USB 不枚举 | `MX_USB_GPIO_Init()` 里必须置 `GCCFG.PWRDWN` 并设 `NOVBUSSENS`，TinyUSB 不会帮你做 |
| 跑起来但中断全不工作（`uwTick` 恒为 0） | 查 `SCB->VTOR`。应用现在链接在 `0x08000000`，`VTOR` 就必须是 `0x08000000`；如果改动过链接脚本，同步改 `system_stm32f4xx.c` 的 `VECT_TAB_OFFSET` |
| 调试器看到程序"卡住"不动 | st-util + gdb 断开后 CPU 会停在 halted 状态，反复 halt 采样看到同一个 PC 是**假象**。要判断程序有没有真的走到某处，用 `break <函数>` + 前台 `continue`，命中断点才算数 |
| **`SELECT OpenPGP` 返回 `6900`** | 文件系统写满了，`openpgp_install()` 建文件失败。用 admin applet 的 `FLASH_USAGE`(INS `0x41`) 看使用率；若已满，回头检查本节开头那三处尺寸约束（RSA 位数 / 证书长度 / LFS cache） |

## 已验证的构建结果

工具链：gcc-arm-none-eabi 10.3-2021.10 + CMake 3.31 + Ninja，`HSE_VALUE=8000000`，Release(`-Os`)：

```
   text	   data	    bss
 193604	    348	  44160	    canokey-f407    (ELF)
image: 193964 bytes, MSP=0x20020000, Reset=0x08002DAD
wrote canokey-f407.bin (193964 bytes), 68180 bytes free in app area
```

- 应用区占用 ≈ 189KB / 256KB（余量 66KB），RAM 稳态 ≈ 43KB / 128KB
- `.isr_vector` 在 `0x08000000`，`.text` 结束于 `0x0802F5AC`，没越过 `0x08040000`
- 文件系统区 `0x08040000~0x08080000` 不在镜像范围内，烧固件不会动它

## 已验证的设备侧行为

用 PC/SC（`winscard.dll`）实测的结果：

```
读卡器: canokeys.org OpenPGP PIV OATH 0
ATR:    3b f7 11 00 00 81 31 fe 65 43 61 6e 6f 6b 65 79 99   (;.....1.eCanokey)
SELECT PIV      -> 61 11 4F 06 00 00 10 00 01 00 79 07 4F 05 A0 00 00 03 08 9000
PIV GET DATA    -> 53 3B ...（CHUID 正常返回）
SELECT FIDO     -> 55 32 46 5F 56 32            ("U2F_V2")
SELECT OATH     -> 79 03 06 00 00 71 08 ...      (含随机 handle)
SELECT Admin    -> 9000
Admin HW_VARIANT-> "CanoKey DIY (STM32F407)"
```

Windows 侧枚举出的设备：`USB\VID_20A0&PID_42D4`，四个接口分别是
FIDO HID、WebUSB、CCID 智能卡（Microsoft Usbccid Smartcard Reader）、HID 键盘。

## 许可

沿用上游：Apache-2.0（软件）/ CERN + CC-BY-SA（硬件）。
