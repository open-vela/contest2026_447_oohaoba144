# Core/Protocol 基础切片验证记录

- 日期：2026-09-14（Asia/Shanghai）
- trace_id：velaguard-7d-20260913 / Round 2
- 状态：host-tested + build-verified；本轮没有 hardware-verified。
- 基础提交：bd7b45c2db3d1467489ab46df77e11200d1e8b70，新增代码尚未提交。
- 范围：固定32槽位任务表、创建校验、request_id去重、合法状态转移、v1 task.create解析、NSH诊断入口。
- 不包含：真实串口收发、响应缓存、TaskStore、cron/RTC接入、UI、完整延后业务、持久化或重启恢复。

## 主机测试

PowerShell实际命令：

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File '\\wsl.localhost\Ubuntu\home\oohb144\workspace\openvela-contest\contest2026_447_oohaoba144\tests\host\run_host_tests.ps1'
```

使用已有 MinGW GCC，C11、-Wall -Wextra -Werror，编译仓库已有 cJSON。
输出：all core/protocol checks passed
退出码：0。主机可执行文件位于 %TEMP%\velaguard-host-tests\core_protocol_tests.exe。
ExecutionPolicy Bypass 仅限这个子进程，没有修改系统策略。

已观察 RED：
1. 初始缺少生产头文件/源文件，编译失败。
2. 缺少version被误分类为UNSUPPORTED_VERSION，用例断言失败；修正为INVALID_MESSAGE后通过。
3. request_id/type包含嵌入NUL的两项断言失败，输出2 test checks failed，exit1；解码前拒绝NUL后通过。
4. title192/193字节及空指针/空输入/超长输入为实现后的补充回归，不声称先写于实现。

独立只读审查指出嵌入NUL截断与RTC用例假覆盖，均已修正并回归。
NUL扫描跳过成对转义，字面量反斜杠加u0000的合法标题仍被接受。

## 独立构建

工作区 /home/oohb144/workspace/openvela-contest。
板级配置 vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh。
团队manifest增加app/velaguard到packages/demos/contest2026_447_velaguard映射。
本机在确认目标不存在后建立对应相对软链接；未repo sync或移动提交。

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake -B /home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol -S /home/oohb144/workspace/openvela-contest/nuttx -GNinja -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh -DCUSTOM_MODULE_PATH=/home/oohb144/workspace/openvela-contest/build/cmake -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"'
```

首次配置后，仅用apply_patch修改独立输出目录的.config，两项从未启用改成：

```text
CONFIG_NETUTILS_CJSON=y
CONFIG_LVX_USE_DEMO_CONTEST2026_447_VELAGUARD=y
```

不修改vendor中的defconfig。之后执行：

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake --build /home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol --parallel 8'
```

首次完整构建退出码0；NUL修正后增量17/17，退出码0。
System.map包含velaguard_main及vg_protocol_parse_create，证明业务对象已链接而不是仅构建基线。
CMake/Ninja路径由envsetup选择；Makefile路径尚未验证。

## 最终产物

- 路径：/home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol/nuttx.bin
- 大小：1555992 bytes
- SHA256：c3fe7f6d9c6910dc24fbfee03f8706b2a20372013f513b9c78b2588d95c84ea6
- SRAM：130672 / 524288 bytes（24.92%）；链接报告PSRAM静态占用0不表示无可用堆。
- 原基线路径：cmake_out/sf32lb52_devkit_lcd/nuttx.bin
- 原基线SHA256复核未变：d401791ee012ac5f8a080edf0256b851fa2abc039ae90707641a9a6bf69f3b02
- 本轮没有刷写命令、串口采集或真机自检结果。诊断命令的selfcheck=PASS只是待真机观察的预期。
- 每次构建后重算SHA256，时间戳影响产物，不复用旧hash。

## 非致命诊断及边界

首次配置的bin_host子配置报缺少Unix Makefiles/C compiler，但外层配置退出0且ARM固件构建成功；未声称主机工具子构建已修复。
链接器有RWX segment与discarded build-id警告，不能称零警告。
一次WSL提示systemd user session启动失败，但bash与后续构建运行成功。
git diff --check通过；nuttx、packages、vendor/sifli的已跟踪内容diff --stat为空。
官方构建触碰lib_utsname.c时间戳，未修改其源码内容。

## 后续风险

- 32是当前总槽位数，完成任务尚不回收；须在TaskStore/历史环实现32活动任务+128历史记录。
- 延后只支持状态转换，还未更新due_epoch/cron任务。
- 去重返回同一索引与DUPLICATE错误，尚非规范要求的响应重放缓存。
- 只有task.create；完整消息类型、分帧、响应、断线恢复均未实现。
- 尚需测试重复JSON键、非法UTF-8、极大数字、64/65请求ID边界、MISSED终态及1024精确边界。
- 解析失败后的out内容不保证不变，调用方只可在VG_OK时消费。
- Core不动态分配；cJSON解析会动态分配，不能宣称整个协议无动态分配。
