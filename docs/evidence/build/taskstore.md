# TaskStore 基础切片 ARM 构建证据

- 日期：2026-09-14；trace_id=velaguard-7d-20260913 / Round 3A。
- HEAD：bd7b45c2db3d1467489ab46df77e11200d1e8b70，工作树未提交。
- 主机测试：见 ../../TASKSTORE_P0.md 和 ../../../logs/taskstore/，最终 TaskStore 与 Core/Protocol 均退出0；独立只读Verifier复跑两项均退出0。
- 证据等级：TaskStore host-tested，ARM对象 build-verified；真实持久化运行未集成，未刷写。

## 构建命令与实际结果

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake --build /home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol --parallel 8'
```

板级配置：vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh。
CMake重新生成后安排2223个目标；外层构建最终exit0。实际输出包含：

```text
[2113/2223] Building C object apps/packages/demos/contest2026_447_velaguard/CMakeFiles/apps_velaguard.dir/src/velaguard_store.c.o
[2221/2223] Linking CXX executable final_nuttx
[2222/2223] Generating System.map
flash: 1555992 B / 16 MB
sram: 130672 B / 512 KB
```

不是零诊断构建：bin_host配置报告Unix Makefiles构建工具/C编译器缺失，但外层ARM继续并退出0；共享SiFli头文件有UNUSED重定义、非prototype声明、未定义回调宏警告；链接仍有RWX及discarded build-id警告。未为这些历史/上游诊断修改共享源码或安装环境。

## 产物身份及接入边界

- 诊断固件：/home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol/nuttx.bin
- 大小：1555992 bytes
- SHA256：f9dfb0f7da3f0254eb4e7562952bf54e18ca1a2cb39a3d2a9f871f9ccb69d0d7
- Store源文件SHA256：830d49109b2c002e883a4c9af50594c86e4b22f1d0e546e0bae262a814c8832c
- Store ARM对象SHA256：301aab6d3d4e7d9d313c0b72e15ee5a8ee4a57a01d1dbc59f2fec7c8e831b3c7
- 对象路径：cmake_out/velaguard_core_protocol/apps/packages/demos/contest2026_447_velaguard/CMakeFiles/apps_velaguard.dir/src/velaguard_store.c.o
- arm-none-eabi-nm确认对象含vg_store_init/create/transition/save/load。
- System.map中vg_store命中0：诊断入口未调用Store，链接丢弃未使用函数。不能把该bin宣称为已运行TaskStore的固件。
- 原基线cmake_out/sf32lb52_devkit_lcd/nuttx.bin的SHA256仍为d401791ee012ac5f8a080edf0256b851fa2abc039ae90707641a9a6bf69f3b02。
- git diff --check通过；nuttx、packages、vendor/sifli已跟踪源码diff --stat为空。
- 未刷写、未复位、未操作串口、未提交/推送。

## 独立主机验收

最终EXE复跑：TaskStore exit0，Core/Protocol exit0。
TaskStore EXE SHA256：be53a9c7a74ed1e58a6e0fe2460c5098ed0458f7b0880b7bef3174a9ce0faa8c。
已修复单槽read错误误回退；审查未发现未解决P1/P2。
这只是当前基础切片审查，完整TaskStore字段、真实文件/NOR、跨进程重启、Scheduler和固件业务接入尚未完成。
