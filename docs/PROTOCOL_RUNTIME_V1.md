# VelaGuard UART 协议 v1（运行入口）

一行 UTF-8 JSON，最多 1024 bytes（不含 LF/可选 CR）。信封恰好 version/request_id/type/payload，无重复或未知键。
version=1；request_id 为 1..64 ASCII 字母、数字、下划线、点或短横线。所有数字使用普通十进制整数，不接受指数/小数/前导零。
标题 1..192 bytes UTF-8，不接受控制字符/NUL/非法 UTF-8；最多 8 层嵌套。未知命令返回 INVALID_MESSAGE。正式入口使用 commands 模块；历史 parse_create 只保留兼容测试。

| type | payload |
| --- | --- |
| task.create | title, due_epoch, priority (0..2) |
| task.list | offset (0起), 可选 history=false |
| event.sync | offset, 可选 history=true |
| task.ack | task_id |
| task.snooze | task_id, seconds (1..86400), expected_due_epoch |
| device.status | {} |
| device.time | epoch |
| device.reload | {} |

create 的 task_id 等于创建请求的 request_id；create/ack/snooze 成功返回固定 payload.task_id，不随任务后续状态改变。
list 每次一条 payload.task，空时为 null；next_offset 为下一序号。终态 ACKNOWLEDGED/MISSED 位于历史。分页期间有并发用户操作时应重新查询，未提供稳定快照游标。
status 返回 ready/blocked/rtc_valid/now_epoch/next_epoch/active_count/history_count/alerting_count；now_epoch 是最近业务对账采样，不是实时 PC 校时接口。
设备 time_t 当前 32bit，create 超出其范围在修改前拒绝；host 解析单独验证了 2038 之后 int64，不能当设备接受所有 int64 的证据。
device.time 接受构建日期前 1 天至后 366 天，系统时钟设置后读取 RTC 核验；未确认硬件时间前先校时再创建。

response.ok 与 response.error 都回显 version/request_id。错误 payload.code/result/uncertain；uncertain=true 表示可能已有持久或校时作用，应 reload+query 核实。
不要用新 request_id 盲重试不确定操作。保持原始完整 envelope；gateway --save-request/--request-file 可保存/重发。
8 个最近成功写命令响应在 RAM 缓存中；相同 ID、不同规范化 JSON 返回 DUPLICATE_REQUEST。JSON 键顺序仍参与缓存比较。
cache 淘汰/重启后 create 按 Store 已有 ID 返回原 task_id，不对旧 ID 的新标题/日期执行更新。去重不超出有界历史。
snooze 必须携带当前 task.list 的 expected_due_epoch，防止缓存丢失后旧延后请求作用于新一轮提醒。
没有认证层，按物理串口受控访问使用。网关不能编辑 Store 或直接改 cron 派生文件。
