# USB2CAN

ESP32 / ESP-IDF 工程，已接入 ctshell 串口交互。开机输出
`AL1S CAN` ASCII 字符画、固件版本和 ctshell 版本，随后显示 `al1s> `。
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

## Flash 文件与 CAN 配置

已启用 ctshell 文件命令，通过应用的 POSIX/VFS 适配层访问 FATFS + 磨损均衡。
默认目录为 `/flash`，支持 `ls`、`cd`、`pwd`、`cat`、`mkdir`、`touch`、`rm`、
`echo ... > file`、`echo ... >> file` 和 `sh`。没有内置全屏文本编辑器。
`rm` 只删除文件或空目录；`touch` 保留已有文件内容；文件操作限制在 `/flash`。

```text
al1s> ls
al1s> cat /flash/can.conf
al1s> can config
al1s> can config set bitrate 1000000
al1s> can reload
```

`can config` 展示已加载配置和文件中保存的配置。每次 `set` 校验后立即保存到
`/flash/can.conf`，连续设置基于文件中的最新值；保存成功后才报告成功。
`reload` 校验文件后替换已加载配置，重启也会读取文件。不再提供 `save` 命令。
文件损坏或缺失时，`set` 基于已加载配置修复文件；加载失败保留当前配置。
启动时根据已加载配置初始化 TWAI；`reload` 校验文件后重启 TWAI，使新波特率和引脚生效。

| 参数 | 默认值 | 含义 |
|---|---|---|
| version | 2 | 配置文件格式版本，不是固件版本 |
| bitrate | 1000000 | 总线比特率，单位 bit/s |
| tx_gpio / rx_gpio | 25 / 26 | TWAI 发送／接收的 GPIO 编号 |
| tx_period_ms | 20 | 配置文件中的预留值；命令行 `--20` 明确指定本次发送周期 |
| rx_timeout_ms | 500 | 预留给后续超时告警；当前 `can recv` 读取已有缓存帧 |

配置为 `key=value` 文本，支持空行和整行 `#`/`;` 注释；要求全部字段且不重复。
整数支持十进制和 `0x` 十六进制；未知字段、超长行、非法值均拒绝。
支持比特率：25000、50000、100000、125000、250000、500000、800000、1000000。
周期/超时范围 1..60000 ms。
GPIO 允许 4、13、14、16..19、21..23、25..27、32、33，TX/RX 不得相同或占用控制台。

后续 TWAI 固定正常收发模式，接收所有 CAN ID；发送时由 Shell 命令指定 ID，
不绑定控制板的三个业务 ID。正常模式提供 ACK，但不自动发送业务命令。
支持读取完整旧版 version=1 配置，忽略旧 mode 和三个 ID 字段；下次 set 写为
version=2 的六字段格式。新版文件不接受已移除字段。波特率必须与对端一致。

保存流程：写 `can.conf.new`、同步、读回校验、旧有效文件保留到 `can.conf.bak`，
再将新文件重命名为 `can.conf`。启动时主文件无效会尝试备份；两者无效则使用
内置默认配置并保留坏文件供检查。首次没有配置及备份时创建默认文件。
FATFS 不提供完整掉电事务保证，这种备份方案不能保证任意断电情况下文件系统无损。
只自动格式化全空白的新 storage 分区；已有分区挂载失败不会自动格式化。

工程使用 4 MB Flash 配置，自定义分区保留原 nvs、phy_init、factory 的偏移/大小，
新增 `storage`：偏移 `0x110000`，大小 `0x80000`（512 KiB）。首次升级需要
**完整执行 `idf.py flash`** 写入新分区表，不能只用 `app-flash`。后续常规烧录
不覆盖 storage；`erase-flash` 会删除配置。此实现尚未做板上断电/持久化验证。

### 新芯片首次使用时 `ls` 报文件系统未初始化

如果串口显示 `Error: Filesystem not initialized.`，先检查启动日志中的
`Filesystem:` 行和分区表。若没有 `storage` 分区，执行完整的 `idf.py flash`
写入分区表；仅烧录 app 不会更新分区表。

这次实际遇到的是分区表已有 `storage`，但启动日志显示
`f_mount failed ... No filesystem detected` 和 `Filesystem: mount failed (ESP_FAIL)`。
新分区所在的 Flash 区域含有旧数据，不是全 `0xFF`，因此程序不会自动格式化，
即使重新完整烧录也不会清除这块区域。如果确认 `storage` 内没有要保留的文件，
先退出串口监视器（Ctrl+]），然后**只擦除该分区**：

```bash
python -m esptool --chip esp32 -p /dev/实际串口 erase-region 0x110000 0x80000
```

此命令会永久删除 `storage` 内的数据，包括已有的 `can.conf`；如需保留，
应先备份。换芯片或改分区表后，先核对 `partitions.csv` 中的偏移和大小，
不要照搬擦除地址。擦除后重启，预期看到
`Filesystem: /flash ready (new partition formatted)`，然后 `ls` 能列出自动创建的
`can.conf`。不要为此使用 `erase-flash`，它会擦除整片 Flash。

板上验收：查看文件；使用 set 修改 bitrate 为 500000；确认 reload 前 loaded 仍为
旧值；reload 后确认新值；复位后再次确认；最后恢复 1000000 并 reload。

## CAN 收发

TWAI 任务固定在 CPU1，正常模式、接收所有 CAN ID。默认 1 Mbps 的位时序为
10 个时间单位（1+5+4），采样点 60%。默认 TX=GPIO25、RX=GPIO26。
硬件接入 SN65HVD230 后，发送和接收可使用：

```text
al1s> can status
al1s> can recv 20
al1s> can topic hz /0x114
al1s> can topic list
al1s> can topic echo /0x114
al1s> can send 0x113 00 00 00 00 00 00 00 00
al1s> can send 0x113 00 0A 01 A5 5A 00 00 00 --20
al1s> can send stop
al1s> can config set bitrate 500000
al1s> can reload
```

`can send` 接受 11 位标准 ID 和 0～8 个十六进制数据字节，提交到 CPU1 的
发送队列；`Queued` 只表示入队，并不能证明对端已收到。`can recv` 从软件缓冲区
取最多 20 帧（可指定 1～100），显示标准/扩展 ID、DLC 和原始字节。
状态帧频率较高，接收缓存满时会丢旧帧；`can status` 可查看软件丢帧数。
`can topic hz /0x114` 对标准 ID `0x114` 采样 1 秒，返回收到的帧数与 Hz。
也可查询 `/0x115` 或其他 11 位 ID；采样时 Shell 暂停处理输入，CPU1 仍在收帧。
频率计数不依赖 `can recv` 缓冲区是否溢出。
`can topic list` 列出本次启动以来收到过的标准 11 位 CAN ID 及累计帧数。
`can topic echo /0x114` 持续显示该 ID 的原始帧，按 Ctrl+C 返回提示符。
Echo 使用独立接收队列，不会取走 `can recv` 的缓存；串口速率不足时会丢帧，
结束时显示 Echo 丢帧数。当前 topic 命令只支持标准帧 ID；扩展帧仍可通过
`can recv` 查看。
在末尾加 `--20` 会从本次开始每 20 ms 发送同一帧，周期参数单位为 ms，
范围为系统 tick（当前 10 ms）到 60000 ms，必须是 tick 的整数倍。
同一时间只运行一组周期帧；新 `can send` 会取消旧的周期帧，普通单次发送也会取消。
`can send stop` 只停止周期任务，不会在总线上发送停止帧。
`can reload` 和重启也会取消周期任务。`can status` 显示当前周期帧及间隔。
如需立即向控制板发送停止指令，直接发送 8 字节全零帧；单次发送会先取消
正在执行的周期帧。

按用户提供的低功率测试固件，控制板使用经典 CAN、标准数据帧、DLC 8。
0x113 的 0～1 字节是功率 ×10 的大端 uint16，字节 2 为命令（00 停止、
01 充电、02 放电、03 校准），字节 3～4 启动校验为 A5 5A，余下为 00。
停止可发送全零帧。0x114 是连续位打包的电量、电压、电流；0x115 是
PWM、故障及状态。`can recv` 目前显示原始字节，供联调时与协议逐项对照。
运行中的控制板超过 100 ms 未收到有效 0x113 会关闭 PWM；周期发送失败、
总线故障或 `can send stop` 后都可能触发此超时。使用 `can status` 观察失败计数。

## 依赖与验证

ctshell 核心固定于 `2f48ab46c24b6a1e0ba094615278bd05c37e004c`，
源码、许可证与本地修改说明在 `components/ctshell/`。无需相邻的
ctshell 仓库或联网下载即可编译。使用 CUSTOM 端口，文件系统后端在 main/flash_files.c。
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
