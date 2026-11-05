# USB2CAN

ESP32 / ESP-IDF 工程，已接入 ctshell 串口交互。开机输出
`AL1S ESP` ASCII 字符画、固件版本和 ctshell 版本，随后显示 `al1s> `。
用户可在 `main/app_info.h` 中修改 `USB2CAN_VERSION`，当前为 `V0.0.1`。

## 编译与串口

VS Code 直接打开本目录，执行 **ESP-IDF: Build your project**。
`.vscode/settings.json` 保留原本已验证的本机 ESP-IDF 和 Python 环境。

```bash
source ./export_idf.sh
idf.py build
# 确认设备端口后再执行：
idf.py -p /dev/实际端口 flash monitor
```

Shell 复用现有 UART0（默认 TX=GPIO1、RX=GPIO3），115200、8N1，走开发板
现有 USB 转串口连接。使用支持 ANSI 转义序列的终端，例如 VS Code 中的
IDF Monitor；关闭终端的本地回显。CR、LF、CRLF 均可，CRLF 只执行一次。
IDF Monitor 默认 Ctrl+] 退出；Ctrl+C 会发给设备取消当前输入行。

```text
al1s> help
al1s> ping
al1s> version
al1s> sys info
al1s> echo hello
```

- Tab 补全，↑/↓ 历史记录，←/→ 光标移动，Backspace 删除。
- `ping` 返回 PONG!；`version` 显示固件、IDF、ctshell 版本。
- `sys info` 显示核心数、Shell 所在核心、运行时间和堆内存。
- 上游内置 `help`、`clear`、`echo`、`set`、`unset`、`hexdump`。
  `hexdump` 直接读取内存，不可访问的地址会导致异常；日常验证使用以上示例即可。
- 行长度上限为 127 字符；超长行整行丢弃，不执行截断内容。

## 任务安排

UART 驱动与 Shell 任务固定在 CPU0，Shell 栈 6144 字节、优先级 4。
接收等待 UART 事件，无输入时阻塞。ctshell 上下文只由 Shell 任务访问；
现有命令均短时返回。不要在命令回调中等待 CAN 或持续打印监测数据，
也不要使用 ctshell_delay 的忙等待。以后通过队列向工作任务提交请求。
当前 Ctrl+C 支持取消输入行；尚未实现阻塞命令的异步中断。

CAN 硬件尚未初始化。本轮只完成 Shell，不会发送控制板启动命令。
未来 CAN 收发任务放 CPU1：

- 工作区 STM32 旧代码 TIM4: 170 MHz / 17000 / 19 ≈ 526.3 次/秒。
- 用户提供的新固件说明每周期尝试发送 0x114、0x115，合计约 1053 帧/秒；
  新 STM32 源码未同步，接入后以实际测量为准。
- 接收应阻塞等待驱动队列并连续取帧，不按固定 Hz 轮询。
- 命令发送独立按 20 ms（50 Hz）调度，接收帧不逐帧打印到串口。
- 初始接收队列可考虑 64 帧，约能缓冲 60 ms 的上述平均流量；
  这不是可接受延迟保证，必须监测队列溢出和帧龄。

拟用 GPIO12=TWAI TX、GPIO14=TWAI RX（GPIO 编号，非排针序号），
必须通过外置 CAN 收发器连接 CAN_H/CAN_L。GPIO12 是启动配置引脚，
接线前须核对模组和收发器上拉，不能在启动时被错误拉高。
此处仅记录拟用接线，固件尚未配置这些引脚。

## 依赖与验证

ctshell 核心固定于 `2f48ab46c24b6a1e0ba094615278bd05c37e004c`，
源码、许可证与本地修改说明在 `components/ctshell/`。无需相邻的
ctshell 仓库或联网下载即可编译。使用 CUSTOM 端口，文件系统后端未包含。
`sdkconfig.defaults` 记录提示符和缓冲区设置；已有 sdkconfig 时以其为准。

```bash
idf.py build
bash tests/run_host_tests.sh
```

主机测试使用真实 ctshell 核心与输入适配层，检查命令链接注册、回车、
补全、历史、编辑、Ctrl+C、超长输入与恢复；不替代板上 UART 测试。
烧录后逐项操作上面的命令，确认 `sys info` 显示 shell core: 0。
硬件 pytest 用例位于 `pytest_hello_world.py`，需配置 pytest-embedded-idf
和已连接的开发板，不能当作普通主机 pytest 运行。
