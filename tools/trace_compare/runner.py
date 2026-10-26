"""Export a stopped Trace through an explicit UE endpoint; compare snapshots offline."""
import argparse
import copy
import json
import math
from pathlib import Path
from statistics import median
import sys
import time

from gas_retest.runner import Client, RunError, direct_url
from gas_retest.report import document, text

METRICS = ("MedianSeconds", "P95Seconds", "MaxSeconds")
CONDITIONS = {"Scenario", "BuildId", "Engine", "Platform", "Hardware", "RenderConfig", "QualityConfig", "WarmupSeconds"}


def number(value, label, minimum=0):
    if type(value) not in (int, float) or not math.isfinite(value) or value < minimum:
        raise RunError(f"{label} must be finite and >= {minimum}")
    return value


def validate_conditions(conditions):
    if not isinstance(conditions, dict) or not CONDITIONS <= conditions.keys():
        raise RunError("Conditions require " + ", ".join(sorted(CONDITIONS)))
    for key in CONDITIONS - {"WarmupSeconds"}:
        if not isinstance(conditions[key], (str, dict)) or not conditions[key]:
            raise RunError(f"Condition {key} must be nonempty text or an object")
    number(conditions["WarmupSeconds"], "WarmupSeconds")
    json.dumps(conditions, allow_nan=False)


def validate_snapshot(snapshot):
    if not isinstance(snapshot, dict) or snapshot.get("Kind") != "TraceSnapshot" or type(snapshot.get("SchemaVersion")) is not int or snapshot["SchemaVersion"] != 1:
        raise RunError("Expected a SchemaVersion=1 TraceSnapshot")
    conditions, data = snapshot.get("Conditions"), snapshot.get("Data")
    validate_conditions(conditions)
    window = conditions.get("Window", {})
    if not isinstance(window, dict): raise RunError("Window must be an object")
    start, end = number(window.get("StartTime"), "StartTime"), number(window.get("EndTime"), "EndTime")
    if end <= start or start < conditions["WarmupSeconds"]:
        raise RunError("Sampling window must follow warmup and have positive duration")
    if not isinstance(data, dict) or data.get("bAnalysisComplete") is not True or data.get("SectionErrors"):
        raise RunError("Trace collection/analysis is incomplete")
    if number(data.get("StartTime"), "Data.StartTime") != start or number(data.get("EndTime"), "Data.EndTime") != end:
        raise RunError("Snapshot statistics do not use the declared sampling window")
    if end > number(data.get("DurationSeconds"), "DurationSeconds") + 1e-9:
        raise RunError("Recording does not cover the complete sampling window")
    if conditions.get("FrameType") not in ("Game", "Render") or data.get("FrameType") != conditions["FrameType"]:
        raise RunError("FrameType does not match sampling conditions")
    count = number(data.get("FrameCount"), "FrameCount", 1)
    if int(count) != count: raise RunError("FrameCount must be integral")
    for key in (*METRICS, "AverageSeconds"): number(data.get(key), key)
    if not data["MedianSeconds"] <= data["P95Seconds"] <= data["MaxSeconds"] or data["AverageSeconds"] > data["MaxSeconds"]:
        raise RunError("Frame statistics are inconsistent")
    timers = data.get("Timers")
    if data.get("bHasCpuTiming") is not True or not isinstance(timers, list) or not timers:
        raise RunError("CPU data is missing; no zero-cost conclusion can be made")
    total = number(data.get("TotalTimerCount"), "TotalTimerCount", len(timers))
    if int(total) != total or type(data.get("bTimersTruncated")) is not bool or data["bTimersTruncated"] != (total > len(timers)):
        raise RunError("CPU total count/truncation metadata is inconsistent")
    for row in timers:
        if not isinstance(row, dict) or not isinstance(row.get("Name"), str) or not row["Name"]:
            raise RunError("CPU timer requires a name")
        for key in ("TotalInclusiveSeconds", "TotalExclusiveSeconds", "MaxInclusiveSeconds", "MaxExclusiveSeconds"):
            number(row.get(key), key)
        instances = number(row.get("Count"), "CPU Count", 1)
        if int(instances) != instances: raise RunError("CPU Count must be integral")
    json.dumps(snapshot, allow_nan=False)
    return snapshot


def export_snapshot(endpoint, trace_path, conditions, start, end, frame_type="Game", timer_limit=100, timeout=30, factory=Client):
    validate_conditions(conditions)
    number(start, "StartTime"); number(end, "EndTime")
    if end <= start or start < conditions["WarmupSeconds"]:
        raise RunError("Sampling window must follow warmup and have positive duration")
    if not isinstance(trace_path, str) or not trace_path.lower().endswith(".utrace"):
        raise RunError("TracePath must identify a stopped .utrace on the UE host")
    if frame_type not in ("Game", "Render") or type(timer_limit) is not int or not 1 <= timer_limit <= 100:
        raise RunError("Invalid FrameType or TimerLimit")
    number(timeout, "Timeout", .01)
    endpoint = direct_url(endpoint)
    client = factory({"Url": endpoint})
    try:
        deadline = time.monotonic() + timeout
        while True:
            summary = client.call("GetTrace", {"TracePath": trace_path, "Sections": ["Summary"]})
            if summary["IsError"]: raise RunError("GetTrace could not open the recording")
            if summary["Payload"].get("bAnalysisComplete") is True: break
            if time.monotonic() >= deadline: raise RunError("Trace analysis is incomplete; no snapshot exported")
            time.sleep(min(.1, timeout))
        response = client.call("GetTrace", {"TracePath": trace_path, "Sections": ["Summary", "Frames", "Timers"],
                               "StartTime": start, "EndTime": end, "FrameType": frame_type, "TimerLimit": timer_limit})
        if response["IsError"]: raise RunError("Trace collection is incomplete; inspect GetTrace section errors")
        snapshot = {"Kind": "TraceSnapshot", "SchemaVersion": 1,
                    "Conditions": {**copy.deepcopy(conditions), "Window": {"StartTime": start, "EndTime": end}, "FrameType": frame_type},
                    "Source": {"Endpoint": endpoint, "TracePath": trace_path}, "Data": response["Payload"]}
        return validate_snapshot(snapshot)
    finally:
        client.close()


def delta(before, after, comparable=True):
    percent = (after - before) / before * 100 if comparable and before not in (None, 0) and after is not None else None
    if percent is not None and not math.isfinite(percent): raise RunError("Percentage change exceeds finite numeric range")
    return {"Before": before, "After": after,
            "AbsoluteChange": after - before if comparable and before is not None and after is not None else None,
            "PercentChange": percent}


def same_condition(left, right):
    if type(left) is bool or type(right) is bool: return type(left) is type(right) and left == right
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(same_condition(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(same_condition(a, b) for a, b in zip(left, right))
    return left == right


def compare(before, after):
    if not before or not after: raise RunError("Both sample groups must be nonempty")
    for snapshot in [*before, *after]: validate_snapshot(snapshot)
    reference = {k: v for k, v in before[0]["Conditions"].items() if k != "BuildId"}
    differences = set()
    for snapshot in [*before, *after]:
        conditions = {k: v for k, v in snapshot["Conditions"].items() if k != "BuildId"}
        differences.update(key for key in reference.keys() | conditions.keys() if key not in reference or key not in conditions or not same_condition(reference[key], conditions[key]))
    comparable = not differences
    frames = {key: delta(median(s["Data"][key] for s in before), median(s["Data"][key] for s in after), comparable) for key in METRICS}
    def cpu_sample(snapshot, name):
        rows = [row for row in snapshot["Data"]["Timers"] if row["Name"] == name]
        # Top N 之外的条目以及重名计时器都是未知；不会补零或把重名行错误地对应起来。
        return rows[0]["TotalInclusiveSeconds"] if len(rows) == 1 else None
    cpu = []
    names = sorted({r["Name"] for s in [*before, *after] for r in s["Data"]["Timers"]})
    for name in names:
        left, right = [cpu_sample(s, name) for s in before], [cpu_sample(s, name) for s in after]
        left_median = median(left) if all(v is not None for v in left) else None
        right_median = median(right) if all(v is not None for v in right) else None
        cpu.append({"Name": name, "BeforeSamples": left, "AfterSamples": right, **delta(left_median, right_median, comparable)})
    cpu.sort(key=lambda row: max(row["Before"] or 0, row["After"] or 0), reverse=True)
    return {"SchemaVersion": 1, "Kind": "TraceComparison", "ExitCode": 0, "bComparable": comparable,
            "ConditionDifferences": sorted(differences), "Frames": frames, "Cpu": cpu,
            "SampleCounts": {"Before": len(before), "After": len(after)},
            "bSingleSample": len(before) == 1 or len(after) == 1,
            "Samples": {"Before": before, "After": after}}


def render_report(report):
    fmt = lambda value: "—" if value is None else f"{value:.6g}"
    body = '<div class="eyebrow">NEXUSLINK · TRACE COMPARE</div><h1>同条件性能对比</h1>'
    body += '<p>仅报告统计变化，不设置性能通过/失败阈值。负变化表示记录值降低。</p>'
    if not report["bComparable"]:
        body += '<section class="panel warning"><h2>不可直接比较</h2><p>采样条件不一致：' + text(", ".join(report["ConditionDifferences"])) + '</p><p>保留每组数值，暂停计算差值。</p></section>'
    if report["bSingleSample"]: body += '<p class="pill">至少一组只有单次采样，不能据此判断稳定性。</p>'
    def table(rows):
        value = '<div class="scroll"><table><tr><th>指标</th><th>Before（秒）</th><th>After（秒）</th><th>绝对变化（秒）</th><th>变化 %</th></tr>'
        for name, row in rows:
            value += '<tr><td>' + text(name) + '</td>' + ''.join('<td>' + fmt(row[k]) + '</td>' for k in ("Before", "After", "AbsoluteChange", "PercentChange")) + '</tr>'
        return value + '</table></div>'
    body += '<section class="panel"><h2>帧统计：各次统计值的中位数</h2>' + table(report["Frames"].items()) + '</section>'
    body += '<section class="panel"><h2>主要 CPU：TotalInclusiveSeconds</h2><p>每端每次都出现且名称唯一才计算变化。缺项、重名、零基线百分比显示 —；截断列表外的项不是零耗时。CPU 总计跨线程，可能超过采样时长。</p>'
    body += table((row["Name"], row) for row in report["Cpu"]) + '</section>'
    body += '<section class="panel"><h2>每次采样与条件</h2><p>场景、硬件、画质等条件由采集者声明；构建标识可以不同。原始统计和截断信息完整保留。</p>'
    for group, samples in report["Samples"].items():
        for index, sample in enumerate(samples, 1):
            data = sample["Data"]
            body += f'<h3>{group} #{index}</h3><p>Median {fmt(data["MedianSeconds"])} s · P95 {fmt(data["P95Seconds"])} s · Max {fmt(data["MaxSeconds"])} s · CPU {len(data["Timers"])}/{data["TotalTimerCount"]} · 截断 {data["bTimersTruncated"]}</p>'
            body += '<pre>' + text(json.dumps(sample["Conditions"], ensure_ascii=False, indent=2)) + '</pre>'
            body += f'<a href="{group.lower()}-{index}.json">原始快照 JSON</a>'
    return document("Trace comparison", body + '</section>')


def write_json(path, value):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as destination:
        json.dump(value, destination, ensure_ascii=False, indent=2, allow_nan=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    export = sub.add_parser("export")
    export.add_argument("output", type=Path)
    export.add_argument("--endpoint", required=True)
    export.add_argument("--trace-path", required=True)
    export.add_argument("--conditions", required=True, type=Path)
    export.add_argument("--start", required=True, type=float)
    export.add_argument("--end", required=True, type=float)
    export.add_argument("--frame-type", choices=("Game", "Render"), default="Game")
    export.add_argument("--timer-limit", type=int, default=100)
    export.add_argument("--timeout", type=float, default=30)
    comparison = sub.add_parser("compare")
    comparison.add_argument("--before", required=True, nargs="+", type=Path)
    comparison.add_argument("--after", required=True, nargs="+", type=Path)
    comparison.add_argument("--output", type=Path, default=Path("trace-compare-results"))
    args = parser.parse_args(argv)
    read = lambda path: json.loads(path.read_text(encoding="utf-8-sig"))
    try:
        if args.output.exists(): raise RunError("Output already exists; choose a new path")
        if args.mode == "export":
            snapshot = export_snapshot(args.endpoint, args.trace_path, read(args.conditions), args.start, args.end,
                                       args.frame_type, args.timer_limit, args.timeout)
            write_json(args.output, snapshot)
        else:
            report = compare([read(path) for path in args.before], [read(path) for path in args.after])
            args.output.mkdir(parents=True)
            write_json(args.output / "comparison.json", report)
            (args.output / "report.html").write_text(render_report(report), encoding="utf-8")
            for group, samples in report["Samples"].items():
                for index, sample in enumerate(samples, 1): write_json(args.output / f"{group.lower()}-{index}.json", sample)
        print(args.output)
        return 0
    except (RunError, OSError, ValueError, TypeError, KeyError) as exc:
        print("Trace comparison error: " + (str(exc) if isinstance(exc, RunError) else type(exc).__name__), file=sys.stderr)
        return 2
