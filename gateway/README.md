# VelaGuard PC gateway

默认 Mock 完全离线。`1分钟后提醒我喝水` 生成 `delay_seconds: 60`，不读取当前日期、不自动校时、不联网。支持阿拉伯数字的秒/分钟/小时，范围 1..86400 秒。未指定 `--port` 时只打印 JSON；`--sync-time` 才显式请求电脑时间校准。

```powershell
python gateway/velaguard_gateway.py --text "1分钟后提醒我喝水" --save-request water-request.json
```

创建 payload 严格为 `title`、`priority` 与两种时间字段之一：

- REL：`delay_seconds` 是 1..86400 整数；不依赖 RTC。
- ABS：`due_epoch` 是未来整数，不超过电脑当前时间后 366 天和 2147483647。MiMo 或保存的完整请求可表达 ABS。

本地任务校验拒绝额外字段、两种时间混用、布尔值冒充整数、浮点数、控制字符与超过 192 UTF-8 字节的标题。完整请求最终由设备的严格协议解析器再校验；重放不重新计算时间或版本。

以下操作用于支持相对计时的新固件；本轮只完成 host 验证，旧固件不会自动获得这些能力。端口由操作者指定：

```powershell
python gateway/velaguard_gateway.py --port COM7 --text "1分钟后提醒我喝水" --save-request water-request.json
python gateway/velaguard_gateway.py --port COM7 --command task.list --offset 0
python gateway/velaguard_gateway.py --port COM7 --command task.snooze --task-id <创建ID> --expected-revision <timer_revision> --seconds 60 --save-request snooze-request.json
python gateway/velaguard_gateway.py --port COM7 --command task.rearm --task-id <创建ID> --expected-revision <timer_revision> --save-request rearm-request.json
python gateway/velaguard_gateway.py --port COM7 --command task.ack --task-id <创建ID>
python gateway/velaguard_gateway.py --port COM7 --command event.sync --offset 0
```

`task.list` 返回 `timer_domain` 为 `REL` 或 `ABS`。REL Snooze 使用最新 `timer_revision`；ABS Snooze 使用 `--expected-due-epoch`，两种 expected 参数互斥。revision 在线上是规范十进制字符串，范围 1..18446744073709551615，不接受前导零、符号或指数。`task.rearm` 仅适用于重启后 `NEEDS_RESET` 的相对任务，按最初的 delay 重新计时；不会自动重新启动。重启前已 `ALERTING` 的相对任务保持提醒，可直接 Done/ACK 或携 revision 延后。

原请求重试：

```powershell
python gateway/velaguard_gateway.py --port COM7 --request-file snooze-request.json
```

`--request-file` 优先读取保存的完整 envelope，不要求再次填写 task ID 或 revision。`--save-request` 独占创建新文件，不覆盖已有文件。超时只重复同一 envelope，不读取新 revision 盲重试；设备返回的 `response.error` 与 `uncertain` 原样保留。结果不确定时先查询，跨进程或重启后的旧 expected revision 可能被拒绝；不要换新请求 ID 自动重做有副作用的操作。打印、原生和 pyserial 路径均限制完整请求 1024 UTF-8 字节。

串口使用现有 Python + PowerShell。Windows Python 没有 pyserial 时，`--port` 调用 `serial_transport.ps1`。两条路径均在打开前将 RTS/DTR 置低，默认 1000000 8N1，无流控。显式 `--sync-time` 失败则不发送后续任务。也可将 JSON Lines 传入 `pwsh -NoProfile -ExecutionPolicy Bypass -File gateway/serial_transport.ps1 -Port COM7`；`-Probe` 只检查对象设置，不打开端口。

MiMo 是显式 `--parser mimo`，由用户在本机设置 `MIMO_API_KEY`、HTTPS `MIMO_BASE_URL` 和 `MIMO_MODEL`，端点须支持 chat/completions。提示要求相对表达保留 `delay_seconds`，明确日期才使用 `due_epoch`，然后执行相同的本地严格校验。不提供默认端点、不打印密钥、不在固件放密钥；测试只使用模拟响应，未验证真实 MiMo 服务。

测试命令：`tests/host/run_gateway_tests.ps1`。runner 复用现有 GCC/Python，在独立临时目录构建真实 Store/Runtime/Commands 主机进程并保留文件。`gateway_device store_dir epoch [mono_ms rtc_valid]` 每次启动都执行 timed runtime boot advance；省略可选参数仍支持旧 ABS 测试。`test.advance` 可独立设置 `epoch`、字符串 `mono_ms` 和 `rtc_valid`，`fire:false` 仅设置测试时钟，默认调用 tick。这些控制及无操作的目录同步屏障仅存在于 host harness，绝不能编入固件。

本轮证据在 `logs/gateway_timers`：旧 ABS、拆分 UTF-8、SGR 过滤、串口打开设置、Mock 无墙钟依赖、模拟 MiMo 两域、CLI revision、精确重传、四进程 REL 恢复，以及旧 boot ALERTING 的 ACK/Snooze。它们证明主机进程持久恢复，不是硬件断电、真实 cron worker 或本轮串口实测。
