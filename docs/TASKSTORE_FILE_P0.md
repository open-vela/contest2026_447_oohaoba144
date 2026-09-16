# TaskStore 文件适配基础切片（Round 3B，2026-09-14）

## 本轮完成范围

新增 `velaguard_store_file.h/.c`，通过原有 `vg_store_io_t` 接入两个文件槽。
默认文件名 `slot0.json`、`slot1.json`，目录由调用者显式传入并预先创建；没有猜测板端挂载路径。
Makefile/CMake 已加入新源文件，main 仍未接入持久存储服务。
本轮没有创建运行环境、添加依赖、修改共享上游、删除文件、操作设备或提交代码。

IO 通过可注入的 open/read/write/sync/close/sync_parent/check_directory 系统调用表实现。
Windows 文件调用使用 `_open/_read/_write/_commit/_close`（二进制模式），POSIX/NuttX 使用 open/read/write/fsync/close。
路径容量 384 bytes，包含目录、分隔符、固定槽文件名及结尾 NUL；Windows 原生 CRT 路径使用当前代码页，本轮真实文件测试路径为 ASCII。

初始化检查父目录，读取槽遇到 ENOENT 后再次确认父目录仍然存在。
缺少父目录或目录验证失败返回 IO_ERROR，不作为全新空库；确实只缺少槽文件返回 missing。
读取循环处理短读/EINTR，恰好满上限时继续探测一个字节，超长失败；空文件读取成功、长度为 0，由 Store 解码判定损坏。
关闭读取文件失败仍返回错误，不发布成功长度。

保存只截断/写入 Store 指定的非当前槽：完整短写/EINTR 循环 → 文件同步 → 单次 close → 目录项同步。
零字节写入和超长输入在打开文件前拒绝；零进展写入、文件同步失败、关闭失败或目录屏障失败均返回错误。
open/read/write/fsync 的 EINTR 可以重试；close 不重试，因为返回 EINTR 时 fd 是否已释放跨平台不确定。
一个失败保存可能已经留下完整的新代数快照，所以之后恢复可能选择旧快照，也可能选择完整新快照。
不能把保存错误解释为“确定没有生效”；Store 的 dirty 和代数在错误返回时不更新，调用者须重载确认持久状态。

## 耐久性闸门

POSIX 默认目录屏障是打开目录、fsync、close；任一失败不忽略。
Windows CRT 不提供目录同步能力，默认 sync_parent 显式 ENOTSUP，**默认 Windows 保存不能报告成功**。
跨进程测试显式替换 sync_parent 为测试用成功回调，只证明文件数据在独立进程之间可恢复；没有提供真实目录项断电屏障。

NuttX/LittleFS 的专用屏障尚未接入。主任务正在单独核查相关源码和 ARM 编译；源码调用链不等于真机断电耐久性。
不能用无验证的成功回调绕过生产闸门，也不能将当前主机恢复测试写成硬件掉电可靠性验证。

## 调用契约

- 只能在 init 成功后取得并使用 IO，file 本体及回调 context 必须保持有效。
- 目录由单一写者控制，不得同时替换/改名目录或由外部写者修改槽。
- 两个槽必须是独立普通文件，不得预设符号链接/硬链接指向同一文件；本模块没有验证链接身份。
- 必须处理 load/save 的错误；load 失败后不得继续修改/保存旧内存覆盖现有介质。
- 不增加锁或跨进程互斥。子进程测试是依次执行，不支持同时多进程写入。
- 延续 Round 3A：最近 128 条终态去重窗口、320 KiB 快照上限、约 46 KiB Store，以及尚未测峰值的 cJSON 堆占用。

## 证据和复现

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File '\\wsl.localhost\Ubuntu\home\oohb144\workspace\openvela-contest\contest2026_447_oohaoba144\tests\host\run_taskstore_file_tests.ps1'
```

测试脚本使用已安装 Windows gcc，编译产物放在 TEMP/velaguard-host-tests。
每次创建新的 GUID TEMP 测试目录并全部保留，不清理、不删除。

`logs/taskstore_file/`：

- `01-red-missing-implementation.txt`：先编写测试再运行，因实现源文件不存在而编译失败；这是编译 RED，不是运行断言 RED。
- `02-first-green.txt`：调用注入模型通过，尚未执行跨进程分支。
- `03-real-process-green.txt`：首次真实文件独立 save/load 进程验证通过。
- `04-full-green.txt`：扩展完整测试通过，包含以下顺序。

1. 注入模型：缺文件/缺父目录/空文件、短读短写、open/read/write/fsync EINTR、exact cap、超长、读错误、关闭读错误保持输出长度、零进展写、sync/close/目录屏障错误、半槽回退、完整新槽但目录屏障失败后的恢复。
2. 新进程 save：一个活动任务、一个 ACK 历史写入真实文件，执行原生文件同步，显式模拟目录屏障。
3. 另一个进程 load：确认活动与历史，并验证两个 request_id 都被去重。
4. tear 进程：向非当前槽写入 8-byte 不完整 JSON，然后 `_exit(0)`，未调用文件同步或显式 close。
5. recover 进程：从完整旧槽恢复，保留历史去重，重写损坏槽。
6. 再次 load 进程：验证修复后的活动/历史仍可加载。
7. 独立真实输入目录：不存在父目录/槽、空文件、327680-byte 恰好上限、327681-byte 超长，以及默认 Windows 目录屏障拒绝成功。

这是进程终止和重新读取验证，进程退出时操作系统仍在运行，不等于整机掉电或物理断电。
后续加强回归并非全部具有先行运行 RED，不能把本轮所有断言声称为逐条 RED→GREEN。

下一步：确认目标文件系统的实际耐久性屏障、测量峰值内存，再接入应用持久化生命周期；本轮不启动 Scheduler。
