# VelaGuard 最小离线应用候选 — 2026-09-14

状态：portable 业务 host-tested，实际应用 build-verified；本轮未刷写、复位、格式化或操作串口。不是完整 P0 / hardware-verified。

## 已集成
- Store v2：32 活动任务、128 终态历史，时间/延后元数据、v1 迁移、未来版本拒写、双槽 CRC/代数。Store 是唯一任务事实源。
- runtime：创建→到期提醒→确认/延后→历史→重载；超过到期 300 秒未确认转 MISSED。写入/调度失败阻塞，reload 才能恢复。
- 官方 packages/ai_agent/src/infra/cron_service.c 原文件引用编译，未复制或修改上游；命名空间隔离。只注册一个最近截止时间唤醒，回调在 cron 锁内只置原子标志，不重入 runtime。唤醒 action 不依赖消息 strdup 成功。
- cron 派生文件 /data/velaguard-cron.json，重启清除派生 job 并从 Store 重新对账；不把官方 cron 文件保存返回值当成任务持久化成功。
- 开机入口 velaguard_main，32 KiB 栈；LVGL 390×450 布局、任务/记录/诊断、到期页面抢占、Done/Snooze 60s/Demo 60s。SIMSUN16 CJK 字体是有限字库，不能保证任意汉字显示。
- KEY2 PA11 bit0 短按确认、长按 1 秒延后；KEY1 不能当第二个用户按键。
- /dev/config0 以 LittleFS 挂至 /vgdata；数据目录 /vgdata/velaguard。NULL mount options，不自动格式化。挂载失败显示 errno 并阻止任务写入，device.reload 可重试挂载。
- 文件 fsync 已包含 LittleFS lfs_dir_commit；专用 parent 回调只接受 LITTLEFS_SUPER_MAGIC。物理 NOR 掉电仍未验证，不能泛化为任意文件系统的空屏障。
- UART JSON Lines 命令/响应、8 项成功响应重放缓存、延后的 expected_due_epoch 防二次作用。正式协议见 PROTOCOL_RUNTIME_V1.md。
- device.time 使用系统校时后 /dev/rtc0 RTC_RD_TIME 读回；失败标记 clock_fault，重新对账取消调度，响应 uncertain=true。默认只接受构建时刻前 1 天至后 366 天的合理日期；日期合理性不是绝对正确时钟的证据。
- /data/agent/skills/velaguard.md 在启动时安装（已有文件不覆盖）。仅复用官方 cron 组件和 Skill 文件约定，没有启用完整语音/网络/LLM Agent 或官方 skill_loader。
- PC Mock 中文解析、显式 MiMo 兼容端点适配、串口重试。Windows 当前 Python 无 pyserial，自动调用已有 PowerShell/.NET 传输，无需安装环境。

## 验证
logs/offline_app/ 下 Core/Protocol、Store、File、Runtime、Commands、Gateway 六个 runner 全部退出 0。
后续 Gateway 最终 8 项通过见 logs/gateway/09-green-cli-encoding.txt；Commands 的 RTC 失败回归包含在 logs/offline_app/run_command_tests.txt。
独立 Verifier 实跑 Commands、Gateway，并只读审查 runtime/main/cron。发现并修复其他任务刚到期对账、错误 reload 取消旧调度、严格数字词法、CRLF 边界、RTC 读回、串口 RTS/DTR、分片和原请求重试。
Native PowerShell -Probe 结果 opened=false, dtr=false, rts=false, baud=1000000，仅验证依赖/配置，不是串口联调。
Gateway 用真实 Windows 文件和两个独立进程验证中文创建、到期、延后、确认、重启历史及重放；test.advance 只存在 host harness，固件不包含。
MiMo 仅离线夹具，无真实网络/API 消耗。Gateway 解析/响应规则及 runtime/Store 已先观察缺失模块或行为失败，再实现；后续加强回归不冒称全部 test-first。
runtime 最后 load 错误取消测试初始断言误把非法 JSON 的 IO_ERROR 指定为 CORRUPT，校正为 !=OK 后通过；不将此前的错误分类断言视作完整缺陷捕获证据。

## 资源证据与限制
- Host sizeof(vg_store_t)=53800；ARM ABI不同，以实际链接/设备测量为准。Store/commands 大对象均静态分配，不放线程栈。
- 满容量普通测试快照 35043 bytes；旧接口可达的控制字符最坏转义快照 277362 bytes；硬上限 327680。小于 64 KiB 是尚未全面达到的目标。
- instrumented Store/cJSON 测试分配 40072 次，峰值有效载荷 1511716 bytes，结束剩余 0。包装 realloc 用 allocate-copy-free，保守计入复制重叠；不含 CRT、包装头、静态数组、LVGL/系统开销，不能当板端峰值。
- 链接 SRAM 196004/524288，Flash 1774224/16777216。PSRAM 静态 0 不等于无堆：当前 BSP_USING_PSRAM=y、MM_REGIONS=2，sifli_allocateheap.c 在 PSRAM 初始化成功后把 8 MiB 加到堆。若初始化失败，Store load 可能因堆不足被阻塞；实际余量待板测。
- 未验证真正掉电、NOR fsync 耐久性、触摸/按键手感、UI 汉字完整性、RTC 重启恢复、串口收发及负载峰值。

## 明确缺口
1. RGB：PA32 最终 pinmux 是 GPIO，当前 /dev/pwm0 为 GPTIM1_CH4；RGB 所需 GPTIM2/PWM3 未编，GPIO 节点不含 PA32，不能借 pwm0 冒充。需进一步确认器件/驱动，未接入。
2. 无可信 RTC 时仍拒绝创建，不支持跨重启可靠的相对倒计时。
3. 完整 ai_agent LLM/Skill loader、task.delete、无限事件同步、持久 snooze 命令响应账本尚未实现。当前 event.sync 是有界历史分页；不支持删除的命令返回 INVALID_MESSAGE。
4. 去重只在 32 活动+128 历史窗口；8 项命令响应缓存仅存内存。跨重启 create 返回原 task_id，ACK 终态可重试；snooze 旧 expected_due_epoch 只保证不重复作用，不承诺重启后仍返回原 response.ok。
5. 主机进程恢复测试的 Windows parent 屏障为明确测试替身，不能声称主机断电耐久性。
6. 应用启动进入前台循环，当前无退出至 NSH 命令；旧基线固件完整保留。此候选用于经过批准的实板验证。
