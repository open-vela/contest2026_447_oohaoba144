# VelaGuard 提交前回归摘要

日期：2026-09-16

## 结果

提交前主机回归全部通过，退出码为 0。

| 测试组 | 结果 |
| --- | --- |
| Core 与严格协议 | PASS |
| TaskStore v3 与容量边界 | PASS |
| 文件适配与跨进程恢复 | PASS |
| 真实板载 v2 数据迁移 fixture | PASS |
| Runtime 绝对时间任务 | PASS |
| Runtime 双时间域与重启规则 | PASS |
| Commands 与 JSON Lines | PASS |
| 相对任务命令与 revision 守卫 | PASS |
| 官方 cron 单调时钟适配 | PASS |
| RTC 世纪与日历边界 | PASS |
| NOR 代理与写前清缓存 | PASS |
| RGB 编码与时序计划 | PASS |
| 官方 Skill loader 适配 | PASS |
| 五工具注册与守卫 | PASS |
| Agent 行协议适配 | PASS |
| PC 网关 16 项测试 | PASS |
| UI 外壳与任务交互逻辑 | PASS |
| 中文字体 7540 字形 | PASS |

## 构建和真机身份

- 源码基线：`bd7b45c2db3d1467489ab46df77e11200d1e8b70` 加本次提交内容
- Board：`SF32LB52-DevKit-LCD V1.2`
- Config SHA256：`2749e2f85b99c9feb60678e4dbae41b4c823906d328bef1a4ecdae616781a4c7`
- 实体已刷写候选 SHA256：`91153ef6c5a7eb326753324439efc2ef66c1bff83af43467fd24c4275db82f5a`
- 最终源码复现构建 SHA256：`ca627e881c1b0a96be20ef6b4f703c0f03122f64f87662eed6b2529b678c4ddf`
- 固件大小：2,669,780 bytes
- 静态 SRAM：203,844 bytes
- 实体候选写入：`sftool --verify` exit 0
- 最终设备状态：ready，RTC/display/touch/storage ready，app/ui error 0

两份固件大小和静态 SRAM 占用相同；哈希差异来自不同时间的完整链接输出。实体状态对应已刷写候选，最终源码的可构建性对应复现构建。完整主机输出保存在开发机 `.artifacts/submission/host-regression-20260916.txt`，不将临时可执行文件和完整本地日志放入比赛仓库。
