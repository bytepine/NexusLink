"""Offline baselines and semantic comparisons of recorded GAS retests."""
from datetime import datetime, timezone
from difflib import SequenceMatcher
import json
from pathlib import Path

from gas_retest.report import collect_evidence, outcome
from .report import document, text
from .runner import RunError, write_reports


LABELS = {"NewFailure": "新增失败", "Recovered": "已恢复", "Added": "新增步骤",
          "Missing": "未执行 / 缺失", "Changed": "状态或错误变化", "Unchanged": "无变化"}


def record_key(record):
    return (record.get("Stage", "Steps"), record["Id"], record.get("Endpoint"), record.get("AbilityPath"))


def validate_report(report):
    if (not isinstance(report, dict) or type(report.get("SchemaVersion")) is not int or report["SchemaVersion"] != 1
            or not isinstance(report.get("Name"), str) or not report["Name"]
            or type(report.get("ExitCode")) is not int or report["ExitCode"] not in (0, 1, 2)
            or report.get("Mode", "Replay") not in ("Replay", "Preflight")
            or not isinstance(report.get("Records"), list) or not report["Records"]):
        raise RunError("Expected a SchemaVersion=1 results report with Name, ExitCode and nonempty Records")
    if report.get("Domain", "GAS") not in ("GAS", "BT"):
        raise RunError("Unsupported report domain")
    for field in ("Endpoints", "Contexts"):
        values = report.get(field, {})
        if (not isinstance(values, dict) or any(not isinstance(key, str) or not isinstance(value, dict)
                                               for key, value in values.items())):
            raise RunError(f"Invalid report {field}")
    signature = report.get("CaseSignature")
    if signature is not None and (not isinstance(signature, str) or len(signature) != 64
                                  or any(char not in "0123456789abcdef" for char in signature)):
        raise RunError("Invalid case signature")
    keys = set()
    for record in report["Records"]:
        if (not isinstance(record, dict) or not isinstance(record.get("Id"), str) or not record["Id"]
                or record.get("Status") not in ("Passed", "Failed", "Error")
                or any(record.get(key) is not None and not isinstance(record[key], str)
                       for key in ("Stage", "Endpoint", "AbilityPath", "Type", "Capability", "Message"))
                or (record["Status"] != "Passed" and not isinstance(record.get("Message"), str))):
            raise RunError("Invalid report step or status")
        key = record_key(record)
        if key in keys:
            raise RunError("Duplicate report step identity")
        keys.add(key)
        result = record.get("Result", {})
        payload = result.get("Payload", {}) if isinstance(result, dict) else None
        if not isinstance(payload, dict) or not isinstance(payload.get("Events", []), list):
            raise RunError("Invalid recorded payload or Events")
        if payload.get("Events") and (not isinstance(record.get("Endpoint"), str)
                                      or not isinstance(payload.get("CaptureId", record["Id"]), str)):
            raise RunError("Activation events require a named endpoint and a string capture identity")
        for event in payload.get("Events", []):
            if (not isinstance(event, dict) or type(event.get("Sequence")) is not int
                    or not isinstance(event.get("Event"), str) or not isinstance(event.get("AbilityPath"), str)
                    or (event.get("SpecHandle") is not None and not isinstance(event["SpecHandle"], str))
                    or not isinstance(event.get("FailureTags", []), list)
                    or any(not isinstance(tag, str) for tag in event.get("FailureTags", []))):
                raise RunError("Invalid recorded activation event")
    expected = 2 if any(record["Status"] == "Error" for record in report["Records"]) else int(any(
        record["Status"] == "Failed" for record in report["Records"]))
    if report["ExitCode"] != expected:
        raise RunError("ExitCode contradicts the recorded step statuses")
    # 同时拒绝非有限数值；原始证据不作静默修补。
    json.dumps(report, allow_nan=False)
    return report


def load_report(path):
    value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    if isinstance(value, dict) and value.get("Kind") in ("GasRetestBaseline", "BtRetestBaseline"):
        if type(value.get("SchemaVersion")) is not int or value["SchemaVersion"] != 1:
            raise RunError("Unsupported baseline version")
        expected_domain = "BT" if value["Kind"] == "BtRetestBaseline" else "GAS"
        value = value.get("Report")
        if not isinstance(value, dict) or value.get("Domain", "GAS") != expected_domain:
            raise RunError("Baseline kind and report domain disagree")
    return validate_report(value)


def save_baseline(report, path):
    validate_report(report)
    if report["ExitCode"] != 0 or report.get("Mode") == "Preflight" or evidence(report)["bIncomplete"]:
        raise RunError("A baseline requires a passed replay without dropped or unidentified events")
    baseline = {"Kind": "BtRetestBaseline" if report.get("Domain", "GAS") == "BT" else "GasRetestBaseline", "SchemaVersion": 1,
                "SavedAt": datetime.now(timezone.utc).isoformat(), "Report": report}
    encoded = json.dumps(baseline, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as destination:
        destination.write(encoded)


def evidence(report):
    if report.get("Domain", "GAS") == "BT":
        from bt_retest.report import evidence as bt_evidence
        return bt_evidence(report)
    contexts, events, incomplete = collect_evidence(report)
    # 只去除每轮变化的身份值，保留采集边界、端内顺序、事件次数、技能与失败标签。
    captures, streams = {}, {}
    for event in events:
        endpoint = event["Endpoint"]
        local_captures = captures.setdefault(endpoint, {})
        local_captures.setdefault(event["Capture"], len(local_captures) + 1)
    for event in sorted(events, key=lambda event: (event["Endpoint"], captures[event["Endpoint"]][event["Capture"]], event["Sequence"])):
        endpoint = event["Endpoint"]
        capture = captures[endpoint][event["Capture"]]
        streams.setdefault(endpoint, []).append({"Capture": capture,
            "Event": event["Event"], "AbilityPath": event["AbilityPath"],
            "FailureTags": sorted(set(event.get("FailureTags", []))),
            "bIdentityKnown": event.get("bIdentityKnown") is True})
    return {"Outcome": outcome(report)[0], "bIncomplete": incomplete,
            "Roles": {name: {key: context.get(key) for key in ("bAuthority", "bLocallyControlled")}
                      for name, context in contexts.items()}, "Events": streams}


def compare_reports(before, after):
    validate_report(before)
    validate_report(after)
    if before.get("Domain", "GAS") != after.get("Domain", "GAS"):
        raise RunError("Cannot compare baselines across GAS and BT domains")
    if before["Name"] != after["Name"] or before.get("Mode", "Replay") != after.get("Mode", "Replay"):
        raise RunError("Reports must have the same case Name and Mode")
    roles = lambda report: {name: config.get("bAuthority") for name, config in report.get("Endpoints", {}).items()}
    if roles(before) != roles(after):
        raise RunError("Endpoint aliases or configured authority roles differ")
    signatures = [report.get("CaseSignature") for report in (before, after)]
    if all(signatures) and signatures[0] != signatures[1]:
        raise RunError("Case definitions differ; compare runs of the same case without changing assertions")
    old = {record_key(record): record for record in before["Records"]}
    new = {record_key(record): record for record in after["Records"]}
    changes = []
    counts = dict.fromkeys(LABELS, 0)
    for key in dict.fromkeys([*old, *new]):
        left, right = old.get(key), new.get(key)
        previous, current = left["Status"] if left else None, right["Status"] if right else None
        if current in ("Failed", "Error") and previous in (None, "Passed"):
            change = "NewFailure"
        elif current == "Passed" and previous in ("Failed", "Error"):
            change = "Recovered"
        elif right is None:
            change = "Missing"
        elif left is None:
            change = "Added"
        elif any(left.get(field) != right.get(field) for field in ("Status", "Message", "Type", "Capability")):
            change = "Changed"
        else:
            change = "Unchanged"
        counts[change] += 1
        changes.append({"Stage": key[0], "Id": key[1], "Endpoint": key[2], "AbilityPath": key[3],
                        "Before": previous, "After": current, "Change": change,
                        "BeforeMessage": left.get("Message", "") if left else "",
                        "AfterMessage": right.get("Message", "") if right else ""})
    old_evidence, new_evidence = evidence(before), evidence(after)
    return {"SchemaVersion": 1, "Domain": after.get("Domain", "GAS"), "Name": after["Name"], "Mode": after.get("Mode", "Replay"),
            "bCaseVerified": bool(all(signatures)),
            "BeforeExitCode": before["ExitCode"], "AfterExitCode": after["ExitCode"],
            "ExitCode": int(bool(after["ExitCode"] or counts["Missing"] or counts["NewFailure"])),
            "Counts": counts, "Steps": changes, "BeforeEvidence": old_evidence, "AfterEvidence": new_evidence,
            "bEvidenceChanged": old_evidence != new_evidence}


def render_comparison(comparison):
    domain = comparison.get("Domain", "GAS")
    body = '<div class="eyebrow">NEXUSLINK · ' + text(domain) + ' RETEST</div><h1>复测前后对比</h1>'
    body += ('<p><span class="pill Error">本次存在失败、错误或缺失检查</span></p>' if comparison["ExitCode"]
             else '<p><span class="pill Passed">本次用例通过，无缺失检查</span></p>')
    body += f'<h2>{text(comparison["Name"])}</h2><div class="grid stats">'
    for change, label in LABELS.items():
        body += f'<article class="panel"><h3>{label}</h3><b>{comparison["Counts"][change]}</b></article>'
    body += '</div>'
    if not comparison["bCaseVerified"]:
        body += '<p class="panel evidence warning">旧报告没有用例签名：仅核对名称、模式、端点角色和已执行步骤，无法证明断言定义一致。建议用新版工具重跑后保存基线。</p>'
    body += '<section class="panel"><h2>用例状态与证据</h2><p>' + ('保留每步观察到的状态与清理结果；快照不代表完整节点执行历史。' if domain == "BT" else '状态恢复只表示用例检查恢复；预期拒绝可以通过用例。事件变化不自动等于回归，缺少权威端证据仍为未确认。') + '</p><div class="grid">'
    for side, label, directory in (("Before", "基线 / 修改前", "before"), ("After", "本次 / 修改后", "after")):
        value = comparison[side + "Evidence"]
        status = {0: "预检通过" if comparison["Mode"] == "Preflight" else "用例通过",
                  1: "断言失败", 2: "运行错误"}[comparison[side + "ExitCode"]]
        body += f'<article><h3>{label} · {status}</h3><p>{text(value["Outcome"])}</p><a href="{directory}/report.html">完整报告与原始证据 →</a></article>'
    body += '</div></section><section class="panel"><h2>步骤变化</h2><div class="scroll"><table><tr><th>变化</th><th>阶段 / 步骤</th><th>端点</th><th>之前</th><th>之后</th></tr>'
    for step in comparison["Steps"]:
        if step["Change"] == "Unchanged":
            continue
        body += f'<tr><td>{LABELS[step["Change"]]}</td><td>{text(step["Stage"])} / {text(step["Id"])}</td><td>{text(step["Endpoint"] or "—")}</td>'
        for side in ("Before", "After"):
            body += f'<td>{text(step[side] or "未记录")}<br><small>{text(step[side + "Message"])}</small></td>'
        body += '</tr>'
    if all(step["Change"] == "Unchanged" for step in comparison["Steps"]):
        body += '<tr><td colspan="5" class="muted">已执行步骤无变化</td></tr>'
    body += '</table></div></section><section class="panel"><h2>' + ('观测快照变化' if domain == "BT" else '各端关键事件变化') + '</h2><p class="muted">' + ('保留步骤、树路径、运行状态及类型化 Blackboard 值，忽略每轮变化的本地对象身份。' if domain == "BT" else '保留采集边界、顺序和次数；忽略进程号、端口、采集 ID、SpecHandle、PredictionKey 与时间。跨进程时钟不用于判断因果。') + '</p>'
    left, right = comparison["BeforeEvidence"], comparison["AfterEvidence"]
    for endpoint in dict.fromkeys([*left["Roles"], *right["Roles"], *left["Events"], *right["Events"]]):
        body += f'<h3>{text(endpoint)}</h3>'
        old, new = left["Events"].get(endpoint, []), right["Events"].get(endpoint, [])
        if left["Roles"].get(endpoint) != right["Roles"].get(endpoint):
            body += f'<p>角色绑定变化：<code>{text(json.dumps(left["Roles"].get(endpoint), ensure_ascii=False))}</code> → <code>{text(json.dumps(right["Roles"].get(endpoint), ensure_ascii=False))}</code></p>'
        if old == new:
            body += '<p class="muted">证据无变化' + ('；两次均无观测证据' if not old else '') + '</p>'
            continue
        encode = lambda values: [json.dumps(event, ensure_ascii=False, sort_keys=True) for event in values]
        a, b = encode(old), encode(new)
        def event_text(event):
            if domain == "BT": return text(json.dumps(event, ensure_ascii=False, sort_keys=True))
            label = f'采集 {event["Capture"]} · {event["Event"]} · {event["AbilityPath"]}'
            if event["FailureTags"]:
                label += ' · 失败标签：' + ', '.join(event["FailureTags"])
            if not event["bIdentityKnown"]:
                label += ' · 技能身份未确认'
            return text(label)
        for tag, i, j, k, m in SequenceMatcher(a=a, b=b, autojunk=False).get_opcodes():
            if tag in ("delete", "replace"):
                body += ''.join(f'<p><span class="pill">移除</span> <code>{event_text(event)}</code></p>' for event in old[i:j])
            if tag in ("insert", "replace"):
                body += ''.join(f'<p><span class="pill">新增</span> <code>{event_text(event)}</code></p>' for event in new[k:m])
    body += '</section><p><a href="comparison.json">JSON 对比结果</a> · 完整证据随报告保存在 before/ 与 after/，离线可查看。</p>'
    if domain == "BT":
        body += "<p>仅比较观察到的状态，不代表完整节点执行历史。</p>"
    return document(domain + " 复测前后对比", body)


def write_comparison(comparison, before, after, output):
    encoded = json.dumps(comparison, ensure_ascii=False, indent=2, allow_nan=False)
    html = render_comparison(comparison)
    output = Path(output)
    # 新目录保证原始记录与已有对比不会被覆盖。
    output.mkdir(parents=True, exist_ok=False)
    write_reports(before, output / "before")
    write_reports(after, output / "after")
    (output / "comparison.json").write_text(encoded + "\n", encoding="utf-8")
    (output / "comparison.html").write_text(html, encoding="utf-8")
