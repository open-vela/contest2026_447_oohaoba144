# Runtime P0：离线任务闭环与 Store v2（2026-09-14）

## 可集成接口

`velaguard_runtime.h/.c` 不直接访问设备或文件，依赖由 `vg_runtime_deps_t` 注入：

- `io`：现有 Store 文件或测试适配。
- `clock(context, &epoch, &valid)`：当前秒级 epoch 及可信标志；返回非 0 表示读取失败。
- `scheduler_reconcile(context, has_next, next_epoch)`：单个最近唤醒源；false 取消。回调不能重入 runtime。

`vg_runtime_t` 是 Store 唯一所有者，必须静态/堆分配，并由一个串行工作线程调用。
建议 cron 回调只发送通知，由主工作线程调用 `vg_runtime_tick`，避免在 cron 持锁回调中执行 scheduler 对账。

| API | 行为 |
|---|---|
| start(runtime,deps) | 初始化并 load，迁移保存，处理已到期任务，重建单一最近唤醒 |
| reload(runtime) | 故障后重新确认持久状态并恢复；失败继续阻塞 |
| create(runtime,request,out) | 创建并 SCHEDULED，保存后对账；重复 request_id 返回当前原任务 |
| tick(runtime) | 消费到期通知，更新 ALERTING/MISSED，保存并重建唤醒 |
| ack(runtime,task_id,out) | 仅 ALERTING 可确认；已 ACK 历史可幂等成功 |
| snooze(runtime,task_id,seconds,out) | 仅 ALERTING 可延后 1..86400 秒，due=now+seconds，再 SCHEDULED |
| find / list | 按稳定 request_id 或分页逻辑位置取得任务值拷贝；阻塞状态不返回确定任务结果 |
| status | 返回 ready/blocked/rtc_valid、最近已读取时钟/唤醒、活动/历史/响铃计数 |

返回 VG_OK 后才允许 command 层发送 response.ok。保存失败或调度对账失败后进入 blocked，不能继续写操作；错误可能已经产生持久效果，不能回复“确定未执行”。仅 reload 能解除该阻塞。
重复 create 按 request_id 身份返回已经存在的任务，即使请求中的 due/title/priority 与原记录不同也不覆盖原记录；不是 update 接口。这个保留原任务策略已由主控确认，命令层可稳定返回 task_id。
create 去重覆盖活动及最近 128 终态历史。snooze 命令 request_id 未进入本层持久元数据，本层不保证跨重启重放同一 snooze 命令；由命令层明确处理范围。

## 时间策略

默认 `VG_RUNTIME_MISS_GRACE_SEC=300`：

- now < due：继续 SCHEDULED。
- due <= now <= due+300：ALERTING。
- now > due+300：MISSED 并移入历史。
- ALERTING 的下次唤醒是 due+301；若超过 INT64_MAX 可表示范围，不创建溢出唤醒。
- snooze 更新时间、次数和 due；ACK/MISSED 记录对应事件时间。

每次 commit 在保存前统一处理其他已经到期任务，防止 UART 命令先于 cron 通知到来时把过去时间交给 scheduler。
终态回收可能移动活动数组，因此命令结果始终按 request_id 重新查找，不保存跨操作 index。

绝对 create、tick、ACK/SNOOZE 状态推进要求可信且正值的 epoch。
RTC 未信任时加载仍可供查询，但取消 scheduler、停止时间驱动更新，不宣称已恢复准确时间。
clock 回调失败或检测到时钟倒退时取消唤醒；倒退导致运行层阻塞，等待校时后 reload。
status 时间为最近一次操作读取的时钟快照，并非每次查询都重新读时钟。
**本轮未实现无可信 RTC 的相对倒计时 create_after**；设备入口可先完成时间同步，再接受绝对任务。

## Store v2

`vg_task_t` 增加 `created_epoch/updated_epoch/acknowledged_epoch/snoozed_epoch/missed_epoch/snooze_count`。
所有时间以十进制字符串保存，保留完整 int64；snooze_count 精确 uint32 并防止递增溢出。
非零事件时间必须位于 created..updated，ACK/MISSED 非零时间只允许相应终态，snooze_count 与 snoozed_epoch 的零值状态保持一致。

兼容读取 v1 的五个原字段，新增元数据全部为 0（未知），dirty=true，下一次成功保存升级 v2。
不发明旧历史时间。v2 元数据 roundtrip、合法 CRC 的未来 v3 单槽与旧槽共存保护均有测试。
原 Store 双槽、CRC、读错误停止、目录屏障限制保持；本轮未接入真实板端持久介质。

资源实测（Windows host）：`sizeof(vg_store_t)=53800` bytes；满槽普通短字段快照 35043 bytes；最大标题/request_id 控制转义案例 277362 bytes（该案例时间元数据使用普通值）。
320 KiB 上限未变。load 固定分配两份 Store 和上限缓冲共 435280 bytes，另有 cJSON 节点/打印临时内存，峰值尚未测量。

## 主机证据

复现：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '\\wsl.localhost\Ubuntu\home\oohb144\workspace\openvela-contest\contest2026_447_oohaoba144\tests\host\run_runtime_tests.ps1'
```

`logs/runtime/`：

- 01：缺 runtime 和 metadata 的编译 RED。
- 02：初版闭环 GREEN。
- 03→04：元数据状态/次数一致性运行断言 RED→GREEN。
- 05→06：命令期间其他任务到期导致过去唤醒，运行断言 RED→GREEN；同时补 clock 失败取消旧调度。
- 10→11：事件早于创建时间的运行断言 RED→GREEN。
- 12：最终运行层边界通过：精确300/301秒、RTC无效重载、时钟倒退恢复、uint32次数及int64时间溢出、保存/调度失败后blocked/reload、稳定ID结果、ACK历史幂等、v1迁移和v3保护。
- 13：最终 Store v2 回归通过。
- 14：文件适配与不同进程 save/load/半写退出/recover/load 的 v2 回归通过；目录屏障仍是明确测试替身，不是硬件断电证据。
- 09：既有 Core/Protocol 回归通过；本轮没有升级 JSONLines 命令协议。

本阶段未修改 main/CMake/Kconfig。命令协议、UART、LVGL、官方 cron 桥及 LittleFS 设备入口由主控下一阶段接入并交叉编译；不能将本轮主机闭环报告为已完成整机可刷写验证。
