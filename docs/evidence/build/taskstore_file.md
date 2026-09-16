# TaskStore 文件适配构建和独立验收

日期2026-09-14；trace_id=velaguard-7d-20260913 / Round 3B。
HEAD bd7b45c2db3d1467489ab46df77e11200d1e8b70，源码未提交。

## 主机证据

设计/复现：../../TASKSTORE_FILE_P0.md。
主机已有MinGW GCC 13.1.0；C11 -Wall -Wextra -Werror。
logs/taskstore_file/01记录缺实现的编译RED；04-full-green记录真实文件独立save/load、部分槽写入后_exit、recover/load和输入边界；05/06分别为Store与Core/Protocol回归。
07-nuttx-name-fix-host-green.txt是NuttX名称冲突修复后的文件suite最终通过记录。
独立Verifier重跑完整runner退出0，另对最终名称修复只读检查全部引用，无未解决P1/P2。
Verifier保留样本目录：

- C:\Users\oohb144\AppData\Local\Temp\velaguard-file-d86e19a378e84171be86373904851627
- C:\Users\oohb144\AppData\Local\Temp\velaguard-file-inputs-fa5cf57169ad445da0316aa686d6d806

没有清理删除任何样本。Windows真实文件进程测试仅替换目录同步为测试成功回调；文件读写和_commit真实执行，不能推广为断电耐久性证明。

## ARM 首次失败与修复

仍使用既有板级 vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh 和输出 cmake_out/velaguard_core_protocol。
首次加入文件适配触发2224个目标，在内部file_read/file_write与nuttx/fs/fs.h声明重名处失败。首次采集包装退出码未反映失败；以日志FAILED和ninja build stopped为准，不把第一次记为成功。
仅将两个队伍内部函数及回调引用改为vg_store_file_read/write；未改上游。
修复后的增量完成110/110，分支标记VELAGUARD_ARM_BUILD_PASS，命令退出0。

复现构建：

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake --build /home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol --parallel 8'
```

实际两次完整构建输出保留在不提交的构建目录：

- cmake_out/velaguard_core_protocol/taskstore_file_build.log
- cmake_out/velaguard_core_protocol/taskstore_file_build_retry.log

首次配置仍有bin_host缺构建工具/C编译器诊断；共享SiFli头文件警告、链接RWX/build-id警告仍存在。修复后的队伍对象无同名错误，外层ARM成功；不称零警告。

## 产物与证据边界

- bin：/home/oohb144/workspace/openvela-contest/cmake_out/velaguard_core_protocol/nuttx.bin
- 1555992 bytes；SHA256 b27f010c712c566eb6f55f7edca063e1376e43f27dd31bc24471b4ef770d007b。
- 文件适配源SHA256 965a97e7a34f7cc66f22f6aa00945ab3013cc8cfe5b8c42a6ac89702e3020c91。
- ARM对象SHA256 285090522fc2d6c0e59ecc9fd450f14702e4aa428d18fafa9db18005b9a6ccf9。
- 对象：cmake_out/velaguard_core_protocol/apps/packages/demos/contest2026_447_velaguard/CMakeFiles/apps_velaguard.dir/src/velaguard_store_file.c.o。
- nm确认公开vg_store_file_init/io/native_ops及静态read/write存在。
- System.map没有vg_store_file：main未调用Store，链接裁掉未用代码，不能把当前bin当持久化功能已启用。
- 原基线bin的SHA256仍d401791ee012ac5f8a080edf0256b851fa2abc039ae90707641a9a6bf69f3b02。
- git diff --check通过；nuttx/packages/vendor/sifli已跟踪源码diff --stat为空。
- 未删除、刷写、复位、串口操作、commit/push。

状态：文件适配host-tested，ARM对象build-verified。POSIX目录同步和NuttX/LittleFS实际耐久性尚未运行验证；板端持久挂载、资源峰值和应用服务接入仍待完成。
