# TaskStore P0 基础切片与主机证据（2026-09-14）

## 范围

本轮增加 `velaguard_store.h/.c`，复用 Core 状态迁移、cJSON 编解码。
仅实现 Core 已有字段的存储：request_id、title、due_epoch、priority、state。
**未实现** cron id、created/updated/ack 时间戳、snooze 元数据、调度恢复、真实 NOR/文件系统适配、设备运行服务。
Makefile/CMake 已列入 store 源文件；main 未调用 store，交叉编译不等于运行接入。

活动区最多 32 条，ACKNOWLEDGED/MISSED 终态移入最近 128 条历史环并回收活动位置。
`request_id` 是稳定身份；index 是瞬时活动数组位置，任何终态回收后应重新查询。
去重覆盖所有活动任务和最近 128 个终态任务。第 129 条终态会逐出最老历史，因此不能承诺无限期去重。
单调用者串行访问，不允许中断上下文调用，也没有并发锁。

## 保存协议与偏离原规格

采用双独立槽快照，区别于此前建议的临时文件加 rename。快照格式为
`{"data":{"version":1,"generation":N,"active":[...],"history":[...]},"checksum":"CRC32"}`。
CRC32 覆盖 cJSON 紧凑打印后的整个 data 对象（包含版本和代数），只用于意外损坏检测，不是认证签名。
时间戳使用正十进制字符串，精确保留 1 到 INT64_MAX，避免 cJSON double 舍入；代数为 1 到 UINT32_MAX，不允许回绕。
重复字段、未知字段、非法状态/容量、跨活动/历史重复 request_id、空或越界字符串、NUL 转义及过深嵌套均拒绝。

save 写入当前槽以外的槽，write_sync 只有完整写入并完成耐久性屏障后才可返回 0。
失败允许目标槽被部分覆盖，但不得破坏另一槽；失败不清 dirty，也不提升代数。
load 选取最高合法代数，遇到已确认损坏可回退并置 dirty，供下一次 save 修复。
任一槽读取错误或 CRC 合法的未来版本均停止加载，保持调用前内存。
cJSON 解析返回 NULL 不能区分语法错误与 OOM，保守报告 IO_ERROR，不回退；因此不是所有语法损坏都自动恢复。
**load 失败后调用方必须停止 create/transition/save**；模块未设置持久的封锁标志，不能忽略返回值继续写入。

本轮 IO 回调只是主机内存双槽模型，故障注入是半写入后返回错误；恢复通过重新 load 到另一 store 模拟。
没有 POSIX 文件、跨进程重启、真实断电、文件系统 fsync/rename 或 NOR 写入证据。
下一步先实现文件适配并做进程级恢复验证，再确认目标板持久介质契约。

## 资源限制

主机 `sizeof(vg_store_t)=46120` bytes，必须静态/堆分配，不能放在当前 8192-byte 应用栈。
快照硬上限 327680 bytes（320 KiB），明显偏离早期 64 KiB 目标：

- 满 32 活动 + 128 历史普通短字段：15683 bytes。
- 满容量、192-byte 标题和64-byte request_id，以控制字符最坏转义填充：258002 bytes。

因此不是用 64 KiB 拒绝本来合法的最大字段。load 另分配两个 store 和一个上限读取缓冲，约 420 KiB，
此外还有 cJSON 节点、字符串和紧凑打印缓冲；save 也有 cJSON 树和两份打印字符串。
峰值堆内存尚未实测，这个实现尚不能视为已满足板端 RAM/Flash 预算。
真实介质将保存两份快照；后续应考虑流式校验/紧凑表示或明确调整字段容量。

## 实际验证

Windows 主机已有 gcc，未搭建新环境。仓库以 WSL UNC 路径访问，所有 Linux 命令保持 WSL 路由。
复现命令（PowerShell）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '\\wsl.localhost\Ubuntu\home\oohb144\workspace\openvela-contest\contest2026_447_oohaoba144\tests\host\run_taskstore_tests.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '\\wsl.localhost\Ubuntu\home\oohb144\workspace\openvela-contest\contest2026_447_oohaoba144\tests\host\run_host_tests.ps1'
```

证据在 `logs/taskstore/`：

- `02-red-missing-implementation.txt`：先写首轮测试，再运行；失败原因为 store.c 尚不存在。不是运行期断言 RED。
- `03-first-green.txt`：首轮实现后通过。
- `04-expanded-tests.txt`：版本/字段篡改及最坏输入扩展回归通过。
- `05-red-single-read-error.txt`：单槽读取错误仍回退的运行期失败断言。
- `06-green-read-fail-closed.txt`：修复读取失败策略后通过。
- `08-core-protocol-regression.txt`：既有 Core/Protocol 回归通过。
- `10-final-taskstore-green.txt`：最终扩展回归通过。

`01-red.txt` 保留最初主机 printf %zu 不兼容的编译输出；`07-final-green.txt` 名称有误，实际保留补 NUL 测试时转义写错的编译失败，已由后续 09/10 纠正，不能当通过证据。
新增扩展测试包含满槽拒绝、ACK/MISSED 回收、128 环溢出与逻辑顺序 roundtrip、load 后活动/历史去重、INT64_MAX、代数溢出、半写、CRC 损坏回退、双槽损坏、空文件、读取错误内存不变、未来版本与旧槽共存、非法字段、大整数溢出、原始 NUL 转义及无 dirty 不写入。
只有首轮测试和单槽读取错误修复有明确先行 RED，其余属于后续加强回归。

未刷写、复位、删除或 commit/push。新增源码当前仍为未提交工作区，交叉编译结果由主任务单独记录。
