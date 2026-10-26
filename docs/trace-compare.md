# 同条件 Trace 性能对比

`tools/trace_compare` 是 Python 标准库命令行工具。通过显式 UE HTTP 端点读取已停录的 `.utrace`，随后离线生成 HTML / JSON。只提供测量报告，不设置性能通过/失败阈值，不分析实时录制或网络包。

## 采集条件

先停止录制（启用 `cpu,frame` 通道），在 UE 所在机器上保留原始 Trace。在插件 `tools/` 下运行命令。文件路径属于 UE 主机；不要求运行 Python 的机器能直接读取它。身份验证沿用本机 NexusLink token 或 `NEXUS_MCP_TOKEN`，不写入快照。

创建 `conditions.json`，例如：

```json
{
  "Scenario": "Fixed camera route",
  "BuildId": "commit-before",
  "Engine": "5.7",
  "Platform": "Win64",
  "Hardware": {"CPU": "record actual model", "GPU": "record actual model"},
  "RenderConfig": {"RHI": "D3D12", "Resolution": "1920x1080", "VSync": false},
  "QualityConfig": {"Preset": "Epic", "DynamicResolution": false},
  "WarmupSeconds": 10
}
```

这些条件是采集者的声明，不代表工具自动识别或证明了相同负载。应记录影响结果的驱动、命令行、随机种子等附加条件；附加字段也参与一致性检查。更换构建时仅改变 `BuildId`。预热后选择明确且完整覆盖的采样时间窗：

```bash
python -m trace_compare export before.json --endpoint http://127.0.0.1:45000/stream --trace-path /recordings/before.utrace --conditions conditions.json --start 10 --end 40
```

`--frame-type Game|Render` 默认 Game；`--timer-limit` 默认 100、范围 1–100；`--timeout` 默认等待分析完成 30 秒。工具不会开始或停止录制，请传入已停录文件。分析未完成、时间窗未录完整、无完整帧、缺少 CPU 数据或非法统计值会返回执行错误 `2`。已有输出文件不覆盖。

## 离线比较

```bash
python -m trace_compare compare --before before.json --after after.json --output comparison
python -m trace_compare compare --before before-1.json before-2.json before-3.json --after after-1.json after-2.json after-3.json --output comparison-multiple
```

输出 `report.html`、`comparison.json` 及完整的每次输入快照。单次采样明确标注；重复采样展示每次值和各项统计值的跨次中位数，不合并原始帧冒充一个长样本。帧指标为中位数、P95（nearest-rank）、最大值；CPU 按 `TotalInclusiveSeconds` 比较，保留次数、独占/包含耗时、总条目数和截断信息。

只有除 `BuildId` 外的所有条件（包括预热、采样窗、FrameType）一致才计算绝对及百分比变化；条件不同显示“不可直接比较”，仍保留各组观测值。零基线不计算百分比。CPU 名称在每端每次都唯一存在才计算差值；缺失、重名或未进入 Top N 的项为未知，绝不补成零。CPU 跨线程汇总可以超过墙钟采样时长。

有效报告返回 `0`，无论性能变好、变差或条件不可比；非法/不完整数据、参数及文件错误返回 `2`。没有性能失败阈值。默认目录 `trace-compare-results/` 已忽略且不打入发布包；其他输出目录也应放在忽略目录中，避免上传主机路径、硬件或业务证据。

## GetTrace 新增输出

- 显式提供 `StartTime`、`EndTime` 时，Frames 与 Timers 采用同一时间窗；Frames 只统计开始和结束都落入窗口的已结束帧。
- Frames 新增 `MedianSeconds`；Timers 新增 `TotalTimerCount`、`bTimersTruncated`。
- 未提供时间窗时保留旧行为：Frames 统计完整录制，Timers 选择最慢 Game 帧。

无需 UE 的验证：`python -m unittest trace_compare.test_runner`。真实 Trace 的窗口筛选、统计和截断字段由 UE Automation 与显式端点导出验证。
