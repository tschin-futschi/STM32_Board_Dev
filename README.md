# STM32_Board_Dev

基于 STM32F429ZGT6 的通用电机调试板固件，SPL 标准库开发。

## 环境要求

- **操作系统**：Windows 10/11
- **MSYS2**：安装至默认路径 `C:\msys64`（[下载](https://www.msys2.org/)）
- **烧录器**：ST-Link V2 + OpenOCD 0.12.x（可选，烧录时需要）

## 环境搭建

### 1. 安装 MSYS2

从 https://www.msys2.org/ 下载安装包，安装到 `C:\msys64`（默认路径）。

### 2. 安装工具链

打开 **MSYS2 MINGW64** 终端（开始菜单中找到），执行：

```bash
pacman -S --noconfirm mingw-w64-x86_64-arm-none-eabi-gcc mingw-w64-x86_64-arm-none-eabi-newlib make
```

### 3. 验证安装

```bash
# 在 MSYS2 MINGW64 终端中
arm-none-eabi-gcc --version   # 应显示 13.x
make --version                # 应显示 GNU Make
```

## 编译

### 方式一：MSYS2 MINGW64 终端（推荐）

```bash
cd /d/your/path/to/STM32_Board_Dev
make              # Release 编译（-O2）
make DEBUG=1      # Debug 编译（-Og -g3）
make clean        # 清理构建产物
```

### 方式二：Windows CMD / PowerShell

需要手动将工具链加入 PATH：

```cmd
set PATH=C:\msys64\mingw64\bin;%PATH%
C:\msys64\usr\bin\make.exe DEBUG=1
```

### 方式三：Git Bash / VS Code 终端

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
/c/msys64/usr/bin/make DEBUG=1
```

## 烧录

连接 ST-Link V2，确保 OpenOCD 已安装并在 PATH 中：

```bash
make flash    # 编译并烧录
```

## 构建产物

编译成功后在 `build/` 目录下生成：

| 文件 | 说明 |
|------|------|
| `firmware.elf` | 含调试信息，用于 GDB |
| `firmware.bin` | 纯二进制，用于烧录 |
| `firmware.map` | 链接映射，查看内存占用 |

## 项目结构

```
Core/Inc/        主程序头文件、SPL 配置、中断声明
Core/Src/        main.c、中断实现、system 时钟配置
Core/Startup/    GCC 启动文件
BSP/Inc/         板级驱动头文件（硬件宏定义）
BSP/Src/         板级驱动实现（LED、SysTick 等）
App/Inc/         应用层头文件（协议、电机、采样）
App/Src/         应用层实现
Linker/          链接脚本
TRACKING/        开发进度总结文件
```
