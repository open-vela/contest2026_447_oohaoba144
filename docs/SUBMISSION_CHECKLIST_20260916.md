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
- [ ] 使用比赛采集器配置真实 GitHub 用户名并导出 Codex 日志
- [ ] 删除官方示例日志 `logs/your-github-login/` 下的两个示例文件
- [ ] 检查日志中没有密码、Cookie、Token、API Key 或不应公开的个人信息
- [ ] 创建提交分支，签名 commit，推送到个人 fork
- [ ] 向 `open-vela/contest2026_447_oohaoba144` 发起 PR
- [ ] 确认 CLA 检查通过并自行 review 合入
- [ ] 在比赛入口提交作品介绍文件、演示视频和专属仓地址

## 当前仓库阻塞项

1. 本地提交分支已建立并完成提交前测试；尚未推送。
2. 只有组委会专属仓 remote，没有个人 fork remote。
3. `contest-snapshot` 命令和 `~/.claude/contest-collector.env` 尚未配置。
4. `logs/your-github-login/` 仍是官方示例，不能当作真实 AI Coding 日志。
5. 最新表盘的连续演示视频尚未录制。

## 推荐最终顺序

1. 用户检查表盘和六入口启动器的实体显示。
2. 按 `docs/DEMO_RECORDING_20260916.md` 录制视频。
3. 用报名 GitHub 账号运行官方日志采集器安装与健康检查。
4. 预览并导出本次 Codex 会话，确认日志目录和身份正确。
5. 经用户明确确认后删除两个官方示例日志文件。
6. 运行完整测试和提交包审计。
7. 创建分支及本地签名提交。
8. 用户确认后推送、创建 PR 和合入。
9. 用户在比赛入口上传 Word、视频并填写仓库地址。

## 官方要求来源

- 参赛代码提交指南：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md
- AI Coding 日志归集与提交手册：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md
- 大赛总览：https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md
