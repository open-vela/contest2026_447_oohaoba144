# VelaGuard 完赛提交检查表

更新时间：2026-09-16

## 当前结论

设备端代码、可运行固件、核心真机闭环和本地技术材料已经具备。正式提交仍需完成三项人工或账户相关动作：录制不超过五分钟的演示视频、配置并导出真实 AI Coding 日志、通过 GitHub PR 合入专属仓库并在比赛入口提交作品介绍、视频和仓库地址。

## 已完成

- [x] VelaGuard 设备端源码和 manifest 映射
- [x] 离线相对提醒、完成、延后、历史和复位恢复
- [x] NOR LittleFS 持久化及不自动格式化保护
- [x] 五个 Agent 工具和设备端 Skill
- [x] PC 中文规则网关及可选 MiMo 解析边界
- [x] 表盘、六入口启动器、四个业务页和全屏提醒
- [x] 主机测试、中文字体测试和 ARM 构建
- [x] 最新固件 `sftool --verify` 写入及启动健康检查
- [x] 提交前 18 组完整主机回归
- [x] 根 README 作品化
- [x] 演示步骤和证据索引
- [x] Word 作品介绍

## 提交前必须完成

- [ ] 在最新表盘固件上完成一次连续实体界面检查
- [ ] 录制不超过五分钟的 MP4 或 MOV 演示视频
- [ ] 在视频中覆盖 Demo 创建、到期、延后、再次到期、完成和历史恢复
- [x] 使用比赛采集器配置真实 GitHub 用户名 `oohb144`，安装检查 11/11 通过
- [ ] 导出一段由官方采集器生成并通过校验的真实 AI Coding 日志
- [x] 删除官方示例日志 `logs/your-github-login/` 下的两个示例文件
- [ ] 检查日志中没有密码、Cookie、Token、API Key 或不应公开的个人信息
- [x] 创建提交分支，签名 commit，推送到个人 fork
- [x] 向 `open-vela/contest2026_447_oohaoba144` 发起 PR #1
- [x] 确认 CLA 检查通过
- [ ] AI Coding 日志补齐后将 PR 转为正式状态并自行 review 合入
- [ ] 在比赛入口提交作品介绍文件、演示视频和专属仓地址

## 当前仓库阻塞项

1. 官方采集器已配置，但安装前开始的 Codex Desktop 会话无法回填；新的 CLI 审查会话又被当前网络连接拒绝，尚未生成可提交日志。
2. 最新表盘的连续演示视频尚未录制。
3. PR #1 保持草稿，等待真实 AI Coding 日志和最终 review。

## 推荐最终顺序

1. 用户检查表盘和六入口启动器的实体显示。
2. 按 `docs/DEMO_RECORDING_20260916.md` 录制视频。
3. 网络恢复后在 openvela 工作区完成一段真实 AI CLI 会话。
4. 用官方采集器预览、导出并校验该会话日志。
5. 提交日志，将 PR 转为正式状态并完成 review、合入。
6. 用户在比赛入口上传 Word/PDF、视频并填写仓库地址。

## 官方要求来源

- 参赛代码提交指南：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md
- AI Coding 日志归集与提交手册：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md
- 大赛总览：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md
