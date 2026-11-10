# ESP32-S3 USB LED 控制器

本项目由 ESP32-S3 设备固件和 Linux USB 内核驱动组成。固件使用 PlatformIO + Arduino ESP32 框架，通过原生 USB 的 **vendor-specific bulk 接口**接收命令；Linux 驱动创建字符设备，用户通过 `ledctl` 控制三个输出引脚。设备不提供 CDC 串口接口，也不依赖 Linux 的 `cdc_acm` 驱动。

## 已完成的工作

- 固件使用框架自带的 `USBVendor`，配置 vendor-specific USB 接口和 bulk IN/OUT 端点；USB 产品名为 `ESP32-S3 LED Device`。
- LED 1、2、3 分别映射到 **GPIO16、GPIO17、GPIO18**。启动时三个引脚均输出低电平。
- 固件支持设置单个 LED、同时设置三个 LED，以及查询单个 LED；对无效命令、编号和值返回错误状态。接收端按字节组帧，并在不完整帧超时后重新同步。
- `include/led_protocol.h` 定义设备与主机共用的帧格式、命令和状态码。
- Linux 内核模块 `usb_led.c` 匹配 USB VID `303a`、PID `1001` 和 vendor 接口，并检查产品名。模块查找 bulk 端点，注册字符设备，处理 `open`、`read`、`write`、`release` 和拔出。
- `ledctl` 通过字符设备发送命令并解析响应；`bind.sh` 可在手动构建模块后绑定指定 USB 接口，并在系统未自动填充 `/dev` 时按 sysfs 中的设备号创建节点。

## 项目文件

| 路径 | 用途 |
| --- | --- |
| `platformio.ini` | ESP32-S3 DevKitC-1、Arduino 和 USB 构建配置 |
| `src/main.cpp` | USB 设备与 GPIO 控制逻辑 |
| `include/led_protocol.h` | 共用的二进制协议常量 |
| `linux_driver/usb_led.c` | Linux USB 内核字符设备驱动 |
| `linux_driver/ledctl.c` | Linux 命令行控制程序 |
| `linux_driver/bind.sh` | 手动加载并绑定模块 |
| `linux_driver/Makefile` | 主机程序与内核模块构建规则 |
| `windows/ledctl.py` | Windows 下通过 WinUSB 控制设备的命令行程序 |

## 硬件连接

| 命令中的 LED 编号 | ESP32-S3 引脚 | 启动状态 |
| --- | --- | --- |
| 1 | GPIO16 | 低电平 |
| 2 | GPIO17 | 低电平 |
| 3 | GPIO18 | 低电平 |

LED 和限流电阻需按所用开发板的电气规格连接。协议中的 `on` 对应 GPIO 高电平，`off` 对应低电平。

## 构建和使用

在项目根目录编译、烧录固件：

```sh
pio run -e esp32-s3-devkitc-1
pio run -e esp32-s3-devkitc-1 -t upload
```

在目标 Linux 系统安装与**当前运行内核**匹配的构建头文件，然后编译模块和控制程序：

```sh
make -C linux_driver module
make -C linux_driver
```

插入设备后，查找 `/sys/bus/usb/devices/` 中 `bInterfaceClass` 为 `ff` 的接口。以下以接口 `1-2:1.0` 为例，实际名称以机器上显示的为准：

```sh
sudo bash linux_driver/bind.sh 1-2:1.0
ls -l /dev/usb_led*
cat /sys/class/usbmisc/usb_led0/dev
linux_driver/ledctl /dev/usb_led0 set 1 on
linux_driver/ledctl /dev/usb_led0 get 1
linux_driver/ledctl /dev/usb_led0 set all off
```

内核 USB 字符设备使用主设备号 **180**。次设备号通常从 **192** 开始；启用 `CONFIG_USB_DYNAMIC_MINORS` 的内核会动态分配。以 `/sys/class/usbmisc/usb_led0/dev` 中的实际 `主设备号:次设备号` 为准。`ledctl` 进程须有该设备节点的读写权限。驱动的 USB ID 表支持模块安装后的自动匹配；手动加载模块时可使用 `bind.sh`，重新插拔后可能需要再次运行。

## Windows 使用

设备运行本项目固件、Windows 已为 **ESP32-S3 LED Device** (`303A:1001`) 安装 **WinUSB** 后，在 Windows PowerShell 中安装 Python 依赖：

```powershell
py -m pip install pyusb libusb-package
```

在本项目根目录执行：

```powershell
py windows/ledctl.py set 1 on    # GPIO16 输出高电平
py windows/ledctl.py get 1
py windows/ledctl.py set 2 on    # GPIO17 输出高电平
py windows/ledctl.py set 3 on    # GPIO18 输出高电平
py windows/ledctl.py set all off
```

程序会按产品名识别设备、查找 bulk IN/OUT 端点，并校验设备的 4 字节响应。`on` 表示高电平，`off` 表示低电平。若报找不到设备，检查设备管理器中 `303A:1001` 对应接口是否使用 WinUSB；若 Python 报缺少包，确认安装依赖与运行程序使用的是同一个 Python 环境。Windows 不使用 Linux 的 `.ko` 模块，也不会生成 `/dev/usb_led*`。

## 二进制协议

每个请求和响应固定为 **4 字节**：

| 方向 | 字节 0 | 字节 1 | 字节 2 | 字节 3 |
| --- | --- | --- | --- | --- |
| 主机 → 设备 | `0xA5` | 命令 | LED 编号 | 值 |
| 设备 → 主机 | `0x5A` | 状态 | LED 编号 | 当前状态 |

命令 `1` 为设置、`2` 为查询。LED 编号 `1`～`3` 分别对应三个引脚；编号 `0` 仅用于同时设置三个 LED。设置值 `0` 为关闭、`1` 为打开。状态 `0` 表示成功，`1`、`2`、`3` 分别表示命令、LED 编号、值无效。该协议替代最初的两字节命令，旧主机程序需要更新。

## 验证状态

- `ledctl` 已在 WSL 中使用 `-Wall -Wextra -Werror` 编译通过；`bind.sh` 已通过 Bash 语法检查。
- 当前环境缺少 ESP32-S3 构建工具链，固件尚未完成编译验证。
- 当前 WSL 缺少运行内核对应的头文件，内核模块尚未完成编译验证。
- 尚未在 ESP32-S3 实物及目标 Linux 主机上验证 USB 枚举、设备节点和 GPIO 输出。

## Linux 未生成设备节点时

依次检查以下三个阶段：

```sh
lsusb -d 303a:1001
lsusb -t
sudo dmesg | tail -n 80
ls /sys/class/usbmisc/usb_led*/dev 2>/dev/null
ls -l /dev/usb_led* 2>/dev/null
```

如果 `lsusb` 中没有 `303a:1001`，说明 Linux 尚未枚举该 USB 设备，先检查连接和线缆。如果 `lsusb` 能看到设备，但 `dmesg` 没有 `usb_led` 的 `probe` 日志，检查固件实际 VID/PID、接口类和模块是否加载。如果有 `probe` 日志，按其报错检查产品字符串、bulk 端点或 `usb_register_dev()`。若 `/sys/class/usbmisc/usb_led*/dev` 存在而 `/dev/usb_led*` 不存在，说明内核已注册设备号，但系统的设备节点管理尚未创建节点；此时以 sysfs 中的实际主、次设备号为准配置 `udev`/`devtmpfs` 或手动创建节点。

若 `lsusb -t` 显示设备接口但 `Driver=` 为空，内核尚未把接口绑定到模块。先用 `modinfo -F alias usb_led.ko` 确认上传的模块包含 `303A:1001` 的 USB alias，再用 `dmesg | grep usb_led` 查看 `probe()` 日志。修改驱动源码后必须重新编译并上传 `.ko`，仅重新加载旧文件不会改变匹配表。

如果外部模块执行 `make` 时没有出现 `CC [M] ... usb_led.o` 和 `LD [M] ... usb_led.ko`，说明 Kbuild 认为旧产物已是最新。将本项目最新的 `linux_driver/usb_led.c` 复制到实际构建目录后，先运行该目录的 `make clean`，再运行 `make`。可用 `strings usb_led.ko | grep 'loading USB driver'` 确认新产物包含模块初始化日志。若干净构建时出现 `Module.symvers` 缺失或符号版本错误，需要用开发板正在运行的内核源码、配置和对应 `Module.symvers` 准备交叉编译环境。

如果 `insmod` 返回成功且 `/sys/module/usb_led/initstate` 为 `live`，却没有 `usb_led: loading USB driver` 日志，也没有 `/sys/bus/usb/drivers/usb_led`，应优先核对构建使用的内核配置与目标机实际配置。`uname -r` 与 `.ko` 的 `vermagic` 一致只表示版本标签一致，不能证明内核内部结构布局一致。内核 5.10 在模块初始化指针为空时仍可把模块标为 `live`；使用不同配置生成的模块可能触发这一现象。先从目标机的 `/proc/config.gz`、`/boot/config-*` 或厂商提供的完整内核构建产物取得实际配置，再重新准备内核构建目录和模块。

本项目排障中已发现一个实际配置差异：WSL 构建树启用了 `CONFIG_JUMP_LABEL=y`，而目标板的 `/proc/config.gz` 未启用；Linux 5.10 的 `struct module` 包含受该选项控制的字段。应以目标板完整配置替换交叉编译树的 `.config`，执行 `olddefconfig` 和 `modules_prepare`，清理旧外部模块产物后重编。目标板和构建树还需使用同一厂商内核源码。缺少 `Module.symvers` 时，若目标内核启用了 `CONFIG_MODVERSIONS`，必须取得对应完整内核构建产物。

KICKPI K11C 使用 RK3566。若开发板运行的是 KICKPI 发布的系统镜像，不能仅凭 `uname -r` 中的 `5.10.160` 使用 kernel.org 发布的同版本上游源码编译模块。应取得与**当前镜像**配套的 KICKPI/Rockchip SDK 内核源码、目标配置和构建产物，再编译外部模块。仅把 `/proc/config.gz` 复制到上游源码树可以修正部分配置差异，但不能补齐厂商内核补丁或保证模块 ABI 一致；也不要用另一个镜像版本的 6.1 SDK 为当前 5.10.160 内核编译模块。

Linux 驱动的更多说明见 [`linux_driver/README.md`](linux_driver/README.md)。
