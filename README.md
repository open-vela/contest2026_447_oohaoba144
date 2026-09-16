# VelaGuard 离线主动提醒手表

> 2026 首届 openvela AI 硬件开发者大赛 · 队伍 `contest2026_447_oohaoba144` · AI 硬件产品创新赛道

## 作品简介

VelaGuard 是运行在 Huangshan Pi SF32LB52-DevKit-LCD 上的离线主动提醒助手。它把任务创建、设备端定时、主动弹窗、完成或延后确认、历史记录和掉电持久化组成一条本地闭环。网络、BLE 和云端模型均不是核心提醒链路的前置条件。

当前版本提供手表式表盘、圆形应用启动器、守护、任务、记录、设备和关于页面。用户可在设备上创建 60 秒演示提醒；任务到期后，AMOLED 显示全屏提醒，RGB 灯同步提示，用户可以完成或延后 60 秒。完成记录写入板载 NOR，并可在复位后恢复。

## 选题方向

本作品参加 **AI 硬件产品创新** 赛道。项目使用 openvela 的 NuttX 运行环境、LVGL 图形栈、官方 cron 唤醒机制、Skill 枚举及工具注册能力，将自然语言网关和设备端确定性任务执行分开。设备负责可靠执行，PC 网关可选用严格规则解析或 MiMo 解析，但密钥只保留在电脑端。

## 核心能力

- **离线主动闭环**：创建任务后，无需持续连接电脑或网络即可倒计时、提醒和确认。
- **两类时间域**：支持绝对 UTC 时间任务及 1 至 86400 秒的相对任务。
- **可靠持久化**：双槽 TaskStore、CRC、版本迁移和 LittleFS 同步；不会自动格式化 NOR。
- **防重复操作**：`request_id` 重放、任务状态和 revision 守卫用于拒绝过期确认或延后请求。
- **设备端交互**：390×450 AMOLED 表盘、六入口启动器、任务页、记录页、设备状态页及全屏提醒。
- **Agent 接口**：注册 `velaguard_create`、`velaguard_list`、`velaguard_snooze`、`velaguard_ack`、`velaguard_rearm` 五个工具。
- **可选 PC 网关**：支持中文相对时间解析、保存完整请求和串口重放；MiMo 接入为显式可选项。

## 界面与操作

1. 启动后默认显示时间、日期、下一提醒圆环、历史记录数和设备健康状态。
2. 从表盘上滑进入应用启动器。
3. 选择守护、任务、记录或设备页面；功能页左右滑动切换，下滑返回表盘。
4. 点击 `Demo` 创建“喝水提醒”60 秒相对任务。
5. 到期后点击“延后 60 秒”或“完成”。
6. 在“记录”页面查看完成结果；复位后记录仍可恢复。

RTC 无效时，界面会明确显示“时间未同步”和“日期待同步”，不会生成伪造的墙钟时间。相对提醒仍以单调时钟运行；未完成的相对任务跨重启后进入 `NEEDS_RESET`，必须由用户明确重新计时。

## 系统结构

```text
PC 网关 可选
  中文规则解析或 MiMo
          │ JSON Lines 1M baud
          ▼
VelaGuard 命令与五工具适配
          │
          ▼
Runtime + TaskStore ── 官方 cron 唤醒
          │                    │
          ├── NOR LittleFS     ├── AMOLED LVGL
          ├── RTC 单调时钟     ├── 触摸与 KEY2
          └── 状态守卫         └── RGB 提示
```

TaskStore 是任务状态的唯一事实源。cron 只负责唤醒和对账，UI、串口命令与 Agent 工具均通过同一 Runtime 操作任务。

## 目录结构

- `app/velaguard/`：设备端应用、Skill、工具桥接、存储、时钟、UI 和字体。
- `gateway/`：PC 端中文命令解析、请求保存和串口传输。
- `tests/host/`：Core、协议、TaskStore、Runtime、工具、网关、字体和 UI 主机测试。
- `docs/`：设计说明、协议、真机证据索引、演示步骤和作品介绍。
- `logs/`：比赛要求的 AI Coding 日志及精选测试记录。
- `contest2026_447_oohaoba144.xml`：将 `app/velaguard` 映射到 openvela 构建树。

## 构建环境

- 开发板：SF32LB52-DevKit-LCD V1.2
- 屏幕：390×450 AMOLED
- 配置：`vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh`
- 控制环境：Windows 11 + Ubuntu WSL2
- 串口：CH343，1,000,000 baud，8N1，无流控，DTR/RTS 关闭

拉取比赛工作区：

```bash
repo init -u https://github.com/open-vela/contest2026_447_oohaoba144 \
  -b dev-ai-contest-2026 -m contest2026_447_oohaoba144.xml
repo sync -c -j8
```

当前验证构建目录由官方 LCD `configs/nsh` 配置生成，并启用 `CONFIG_LVX_USE_DEMO_CONTEST2026_447_VELAGUARD=y`、FT6146 触摸及 PA31 中断。增量构建：

```bash
cd <openvela-workspace>
source build/envsetup.sh
cmake --build cmake_out/velaguard_core_protocol --parallel 8
```

输出：`cmake_out/velaguard_core_protocol/nuttx.bin`。

当前实机候选 SHA256 为 `91153ef6c5a7eb326753324439efc2ef66c1bff83af43467fd24c4275db82f5a`，大小 2,669,780 bytes。固件仅写入应用区 `0x12010000`，结束地址 `0x1229BCD4`，低于任务数据区起点 `0x129A0000`。仓库不提交固件二进制。

## 主机测试

Windows PowerShell 可分别运行：

```powershell
powershell -ExecutionPolicy Bypass -File tests/host/run_host_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_taskstore_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_taskstore_file_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_runtime_timer_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_command_timer_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_tools_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_agent_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_gateway_tests.ps1
powershell -ExecutionPolicy Bypass -File tests/host/run_ui_logic_tests.ps1
python tests/host/test_font_cn.py
```

测试覆盖严格 JSON、请求去重、TaskStore 损坏恢复、双时钟任务、旧 revision 拒绝、五工具 schema、串口网关、中文字体和表盘导航。

## PC 网关

离线规则解析示例：

```powershell
python gateway/velaguard_gateway.py `
  --text "1分钟后提醒我喝水" `
  --save-request water-request.json
```

连接设备：

```powershell
python gateway/velaguard_gateway.py `
  --port COM7 `
  --text "1分钟后提醒我喝水" `
  --save-request water-device-request.json
```

完整参数和重放约束见 [`gateway/README.md`](gateway/README.md)。

## 已验证结果

- 主机 Core、协议、存储、Runtime、工具、Agent、网关、UI 与字体测试通过。
- ARM 构建通过；静态 Flash 15.91%，SRAM 38.88%。
- `sftool --verify` 真机写入通过。
- 启动后 storage、cron/runtime、RGB、Skill loader 和五工具注册成功。
- 真实触摸、Demo、到期、延后、再次提醒、完成、KEY2 和历史恢复均已有分项真机证据。
- 最新表盘固件启动状态为 ready；显示、触摸、存储就绪，app/ui error 均为 0。

证据入口：[`docs/DEMO_EVIDENCE_INDEX_20260916.md`](docs/DEMO_EVIDENCE_INDEX_20260916.md)。

## 已知边界

- 当前作品不声称设备端运行云端大模型；MiMo 仅为 PC 网关可选解析器。
- 完全断电后 RTC 连续计时未通过。任务数据能够保留，但绝对时间可能需要重新同步。
- 相对任务跨重启后不会猜测断电期间经过的时间，而是进入 `NEEDS_RESET` 等待用户明确操作。
- 简单周期提醒和任务取消未纳入本次完赛演示版。
- LSM6DS3 在当前板上未注册；核心提醒链路不依赖 IMU。

## AI Coding 使用说明

本项目使用 Codex 协助完成需求拆解、接口设计、测试先行实现、边界审查、构建诊断、真机证据整理和文档编写。关键规则、协议、持久化和 UI 修改均先加入主机失败测试，再实现并回归。AI Coding 原始日志按比赛采集器要求放入 `logs/<github_login>/`，不手工修改日志内容。

设备端 Skill 位于 `app/velaguard/src/velaguard_skill.c`，描述五个工具的参数、时间域、revision 守卫和不确定结果处理规则。

## 演示与提交材料

- 演示流程：[`docs/DEMO_RECORDING_20260916.md`](docs/DEMO_RECORDING_20260916.md)
- 证据索引：[`docs/DEMO_EVIDENCE_INDEX_20260916.md`](docs/DEMO_EVIDENCE_INDEX_20260916.md)
- 作品介绍：`docs/VelaGuard_作品介绍_20260916.docx`（可编辑）/ `docs/VelaGuard_作品介绍_20260916.pdf`（兼容预览）
- 提交检查：[`docs/SUBMISSION_CHECKLIST_20260916.md`](docs/SUBMISSION_CHECKLIST_20260916.md)

## 开源与第三方资源

项目基于 openvela 及其第三方依赖的原有许可开发。中文字体使用 Noto Sans SC，许可文本保存在 `app/velaguard/fonts/OFL-Noto.txt`。表盘和应用启动器使用 LVGL 基础组件自行实现，没有包含 Apple 商标、界面图片或第三方手表素材。
