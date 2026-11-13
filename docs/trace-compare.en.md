<a id="同条件-trace-性能对比"></a>
# Comparing Trace performance under matching conditions
[中文](trace-compare.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

tools/trace_compare is a Python standard-library CLI. It reads stopped .utrace recordings through an explicit UE HTTP endpoint, then generates HTML/JSON offline. By default it only reports measurements. Explicit [--budget performance budgets](feature-followups.en.md#性能预算) distinguish exceeded budgets from insufficient evidence by exit code. It does not analyze live recordings or network packets.

CLI tools are supplied with source code, not the plugin installation archive. Obtain matching source first; see [installation](usage-guide.en.md#21-安装).

<a id="采集条件"></a>
## Capture conditions

Stop recording first, with cpu,frame channels enabled, and keep the original Trace on the UE machine. Run commands under the source repository's tools/ directory. File paths belong to the UE host; the Python machine need not read them directly. Authentication uses the local NexusLink token or NEXUS_MCP_TOKEN, never written into snapshots.

Create conditions.json, for example:

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

These conditions are the collector's declarations, not automatic identification or proof of equal workloads. Record additional factors such as drivers, command line, and random seeds; extra fields also participate in equality checks. Change only BuildId when changing builds. After warmup, select an explicit, fully recorded sample window:

```bash
python -m trace_compare export before.json --endpoint http://127.0.0.1:45000/stream --trace-path /recordings/before.utrace --conditions conditions.json --start 10 --end 40
```

--frame-type Game|Render defaults to Game; --timer-limit defaults to 100, range 1–100; --timeout defaults to waiting 30 seconds for analysis. The tool never starts or stops recording: pass a stopped file. Incomplete analysis, an incomplete recorded window, no complete frames, missing CPU data, or invalid statistics return execution error 2. Existing output files are not overwritten.

<a id="离线比较"></a>
## Offline comparison

```bash
python -m trace_compare compare --before before.json --after after.json --output comparison
python -m trace_compare compare --before before-1.json before-2.json before-3.json --after after-1.json after-2.json after-3.json --output comparison-multiple
```

Outputs include report.html, comparison.json, and complete input snapshots for every run. Single samples are labeled explicitly. Repeated samples show per-run values and cross-run medians of each statistic; raw frames are not merged to impersonate one long sample. Frame metrics are median, nearest-rank P95, and maximum. CPU comparison uses TotalInclusiveSeconds and retains counts, exclusive/inclusive time, total entries, and truncation.

Absolute and percentage changes are computed only when all conditions except BuildId match, including warmup, sample window, and FrameType. Different conditions show "not directly comparable" while retaining each group's observations. Zero baselines have no percentage delta. CPU deltas require a uniquely named timer on every endpoint/run. Missing, duplicate, or non-Top-N timers remain unknown and never become zero. CPU totals across threads can exceed the wall-clock sample window.

A valid report returns 0 whether performance improves, regresses, or conditions are incomparable. Invalid/incomplete data, arguments, or file errors return 2. There is no default performance failure threshold. The default trace-compare-results/ directory is ignored and excluded from releases. Keep other output directories ignored too, avoiding uploads of host paths, hardware, or gameplay evidence.

<a id="gettrace-新增输出"></a>
## Additional GetTrace output

- Explicit StartTime and EndTime make Frames and Timers use the same time window. Frames counts only ended frames whose start and end both lie in the window.
- Frames adds MedianSeconds; Timers adds TotalTimerCount and bTimersTruncated.
- Omitting the time window preserves old behavior: Frames covers the whole recording; Timers uses the slowest Game frame.

Validation without UE: python -m unittest trace_compare.test_runner. UE Automation and explicit-endpoint exports verify real Trace window filtering, statistics, and truncation fields.
