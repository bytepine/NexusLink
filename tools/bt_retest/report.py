"""Snapshot observations are not a complete node execution history."""
import json
from retest.report import document, text


def evidence(report):
    snapshots = {}
    for record in report["Records"]:
        payload = record.get("Result", {}).get("Payload", {})
        if "TreePath" not in payload: continue
        snapshots.setdefault(record.get("Endpoint", "UE"), []).append({"Step": record["Id"],
            "TreePath": payload["TreePath"], "RootTreePath": payload.get("RootTreePath", payload["TreePath"]), "bRunning": payload.get("bRunning"), "ActiveNode": payload.get("ActiveNode"),
            "Blackboard": sorted([{key: entry.get(key) for key in ("name", "TypeName", "TypedValue", "bTypedValueAvailable", "bInherited")}
                                 for entry in payload.get("BlackboardKeys", [])], key=lambda entry: entry["name"])})
    complete = all(any(record["Id"] == step and record["Status"] == "Passed"
                       and record.get("Result", {}).get("Payload", {}).get("MatchesExpected") is True
                       for record in report["Records"]) for step in ("expected", "restored"))
    return {"Outcome": "预期快照已观察到且条件已恢复" if complete else "预期状态或清理未确认",
            "bIncomplete": not complete, "Roles": {name: {"bAuthority": ctx.get("bAuthority")} for name, ctx in report.get("Contexts", {}).items()},
            "Events": snapshots}


def render_report(report):
    state = evidence(report)
    body = '<div class="eyebrow">NEXUSLINK · BT RETEST</div><h1>' + text(report["Name"]) + '</h1>'
    body += '<p>领域：行为树 / Blackboard。仅记录轮询时观察到的状态，不代表完整节点执行历史。Actor 绑定由用户显式声明。</p>'
    if report.get("Mode") == "Preflight": body += '<p>只读预检：尚未改变条件或执行场景。</p>'
    else: body += '<p>' + text(state["Outcome"]) + '</p>'
    for record in report["Records"]:
        body += '<section class="panel"><h2>' + text(record["Id"]) + ' · ' + text(record["Status"]) + '</h2>'
        if record.get("Message"): body += '<p>' + text(record["Message"]) + '</p>'
        body += '<pre>' + text(json.dumps(record.get("Result", {}), ensure_ascii=False, indent=2)) + '</pre></section>'
    return document("BT 复测", body)
