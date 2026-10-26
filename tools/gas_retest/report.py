"""GAS evidence views; shared HTML helpers retain their historical imports."""
import json
from pathlib import Path
from urllib.parse import quote
from retest.report import STYLE, text, document


def collect_evidence(report):
    contexts = dict(report.get("Contexts", {}))
    events = {}
    incomplete = False
    for record in report["Records"]:
        endpoint = record.get("Endpoint")
        payload = record.get("Result", {}).get("Payload", {})
        if isinstance(payload.get("Context"), dict):
            contexts[endpoint] = payload["Context"]
        incomplete |= bool(payload.get("Dropped", 0))
        for event in payload.get("Events", []):
            incomplete |= event.get("bIdentityKnown") is not True
            incomplete |= bool(report.get("EvidenceAbilityPath") and event.get("AbilityPath") != report["EvidenceAbilityPath"])
            key = (endpoint, payload.get("CaptureId", record["Id"]), event.get("Sequence"))
            events[key] = {**event, "Endpoint": endpoint, "Capture": key[1]}
    return contexts, list(events.values()), incomplete


def outcome(report):
    contexts, events, incomplete = collect_evidence(report)
    if report.get("Mode") == "Preflight":
        return "仅预检，未激活技能", "预检只读取端点、角色和已授予技能；不执行场景或清理步骤。", False
    authority = [event for event in events if contexts.get(event["Endpoint"], {}).get("bAuthority") is True]
    classes = {event.get("AbilityPath") for event in events}
    for endpoint in contexts:
        local = [event for event in events if event["Endpoint"] == endpoint]
        incomplete |= len({event.get("SpecHandle") for event in local}) > 1 or sum(event.get("Event") == "Activated" for event in local) > 1
    incomplete |= len(classes) > 1
    failed = [event for event in authority if event.get("Event") == "Failed"]
    committed = [event for event in authority if event.get("Event") == "Committed"]
    if incomplete or (failed and committed):
        return "结果未确认：采集证据不完整或存在歧义", "检查丢失事件、技能身份和多次尝试；每次激活使用新采集。", True
    if failed:
        predicted = any(event.get("Event") == "Activated" and contexts.get(event["Endpoint"], {}).get("bAuthority") is False for event in events)
        title = "客户端已预测激活，权威端观察到失败" if predicted else "权威端观察到技能激活失败"
        return title, "失败事件和标签来自实际采集；用例通过表示预期得到了验证。", True
    if committed:
        return "权威端已提交技能", "已观察到 Committed；具体业务效果由用例中的断言验证。", False
    for record in report["Records"]:
        payload = record.get("Result", {}).get("Payload", {})
        if (contexts.get(record.get("Endpoint"), {}).get("bAuthority") is True
                and payload.get("ActivationDiagnostics", {}).get("bGranted") is False):
            return "权威端未找到所选技能", "此结论来自该权威角色的授予快照；请核对技能路径与授予时机。", True
    return "技能结果未确认：未取得权威端提交或失败事件", "客户端请求被接受、断言通过或只读快照，都不能单独证明服务器激活成功。", True


def next_checks(report):
    _, events, incomplete = collect_evidence(report)
    if report.get("Mode") == "Preflight":
        return ["处理标记为 Error 的绑定或技能检查；预检通过后，去掉 --preflight 执行同一用例。"]
    if incomplete:
        return ["缩短复现步骤，并为每次激活启动新采集；先解决事件丢失或身份未知，再判断服务器结果。"]
    tags = {tag for event in events for tag in event.get("FailureTags", [])}
    hints = {"Nexus.Diagnostics.Rejected": "示例服务器返回了 Rejected：检查 bRejectActivation；恢复为 false 后用 Success 用例复测。",
             "Nexus.Diagnostics.Cost": "示例返回了 Cost：检查权威端 Mana 与技能配置的消耗，再复测。",
             "Nexus.Diagnostics.Cooldown": "示例返回了 Cooldown：核对权威端冷却效果及剩余时间。",
             "Nexus.Diagnostics.Required": "核对权威端必需标签与实际拥有的标签。",
             "Nexus.Diagnostics.Blocked": "核对权威端阻止标签与技能配置。"}
    hints.update({tag: "配置说明（实际观察到 " + tag + "）：" + hint
                  for tag, hint in report.get("FailureTagHints", {}).items()})
    selected = [hint for tag, hint in hints.items() if tag in tags]
    if selected:
        return selected
    if report["ExitCode"]:
        return ["从首个失败步骤及其原始记录开始检查；清理失败单独记录在 Teardown 阶段。"]
    return ["查看权威端事件及用例断言；技能提交后的业务效果需要项目自己的断言验证。"]


def render_report(report):
    records = report["Records"]
    code = report["ExitCode"]
    status = {0: "预检通过" if report.get("Mode") == "Preflight" else "用例通过", 1: "断言失败", 2: "运行错误"}.get(code, "未知状态")
    title, description, warning = outcome(report)
    body = f'<div class="eyebrow">NEXUSLINK · GAS RETEST</div><h1>{text(report["Name"])}</h1>'
    body += f'<span class="pill {"Passed" if code == 0 else "Error"}">{status}</span> <span class="muted">{len(records)} 个已执行检查 / 步骤</span>'
    body += f'<section class="panel evidence {"warning" if warning else ""}"><h2>{text(title)}</h2><p>{text(description)}</p></section>'
    failed = [record for record in records if record["Status"] != "Passed"]
    if failed:
        body += '<section class="panel"><h2>需要处理的步骤</h2>'
        for record in failed:
            body += f'<p><span class="pill Error">{text(record.get("Stage", "Steps"))}</span> <b>{text(record["Id"])}</b> · {text(record.get("Endpoint") or "—")}<br>{text(record.get("Message", ""))}</p>'
        body += '</section>'
    body += '<section class="panel"><h2>下一步检查</h2><ul>' + ''.join(f'<li>{text(hint)}</li>' for hint in next_checks(report)) + '</ul></section>'
    contexts, events, _ = collect_evidence(report)
    endpoints = list(dict.fromkeys([*report.get("Endpoints", {}), *contexts]))
    body += '<section class="panel"><h2>端点与角色绑定</h2><div class="grid">'
    for record in records:
        evidence = record.get("Result", {}).get("BindingEvidence", {})
        if "UserDeclared" in evidence.values():
            body += '<p class="muted">显式 Actor 映射由用户声明。各端本地身份已核验，不代表自动证明了跨端 Actor 身份。</p>'
    for endpoint in endpoints:
        context = contexts.get(endpoint, {})
        role = "权威端" if context.get("bAuthority") is True else "客户端" if context.get("bAuthority") is False else "角色未验证"
        endpoint_config = report.get("Endpoints", {}).get(endpoint, {})
        body += f'<article class="panel"><h3>{text(endpoint)} · {role}</h3><code>{text(endpoint_config.get("Url", "未记录地址"))}</code><dl>'
        for label, value in (("进程 / World", f'{context.get("ProcessId", "—")} / {context.get("WorldId", "—")}'),
                             ("PlayerId", context.get("PlayerId", "—")), ("Actor", context.get("Avatar", "—"))):
            body += f'<dt>{label}</dt><dd>{text(value)}</dd>'
        body += '</dl></article>'
    body += '</div></section>'
    body += '<section class="panel"><h2>各端激活事件</h2><p class="muted">按各端采集顺序展示并去重。不同进程的时钟不能用于证明事件因果关系。</p><div class="grid">'
    for endpoint in endpoints:
        body += f'<article><h3>{text(endpoint)}</h3>'
        local = [event for event in events if event["Endpoint"] == endpoint]
        if not local:
            body += '<p class="muted">未记录激活事件证据</p>'
        for event in local:
            body += f'<div class="event"><span class="pill">#{text(event.get("Sequence", "—"))} {text(event.get("Event", "—"))}</span><small>{text(event.get("Time", ""))}</small>'
            body += f'<p><code>{text(event.get("AbilityPath", ""))}</code><br><small>Spec {text(event.get("SpecHandle", "—"))} · Prediction {text(event.get("PredictionKey", "—"))}</small></p>'
            if event.get("FailureTags"):
                body += '<p>失败标签：' + text(", ".join(event["FailureTags"])) + '</p>'
            body += '</div>'
        body += '</article>'
    body += '</div></section>'
    diagnostics = [(record, record.get("Result", {}).get("Payload", {}).get("ActivationDiagnostics")) for record in records]
    if any(value for _, value in diagnostics):
        body += '<section class="panel"><h2>激活前快照</h2><p class="muted">快照不调用项目自定义检查。消耗和自定义规则保持未知，失败原因以实际事件为证据。</p>'
        for record, diagnostic in diagnostics:
            if diagnostic:
                body += f'<h3>{text(record.get("Endpoint"))} · {text(record["Id"])}</h3><pre>{text(json.dumps(diagnostic, ensure_ascii=False, indent=2))}</pre>'
        body += '</section>'
    body += '<section class="panel"><h2>步骤与原始证据</h2><div class="scroll"><table><tr><th>阶段</th><th>步骤 / 工具</th><th>端点</th><th>状态</th><th>耗时</th></tr>'
    for record in records:
        body += f'<tr><td>{text(record.get("Stage", "Steps"))}</td><td>{text(record["Id"])}<br><small>{text(record.get("Capability") or record.get("Type") or "")}</small></td><td>{text(record.get("Endpoint") or "—")}</td><td><span class="pill {text(record["Status"])}">{text(record["Status"])}</span></td><td>{text(record.get("DurationMs", "—"))} ms</td></tr>'
    body += '</table></div>'
    for record in records:
        body += f'<details><summary>{text(record["Id"])} · 原始记录</summary><pre>{text(json.dumps(record, ensure_ascii=False, indent=2))}</pre></details>'
    body += '</section><p class="muted">离线报告 · 全部内容来自本次复测记录 · <a href="results.json">JSON 证据</a> · <a href="junit.xml">JUnit</a></p>'
    return document(report["Name"], body)


def write_index(entries, output):
    body = '<div class="eyebrow">NEXUSLINK · GAS RETEST</div><h1>联机诊断与复测</h1><p>先复现，再恢复条件验证成功。用例状态与实际技能结果分别展示。</p><div class="grid">'
    for directory, report in entries:
        title, _, _ = outcome(report)
        label = ("预检" if report.get("Mode") == "Preflight" else "用例") + ("通过" if report["ExitCode"] == 0 else "失败 / 错误")
        name = "接入预检" if report.get("Mode") == "Preflight" else report["Name"]
        body += f'<article class="panel"><span class="pill {"Passed" if report["ExitCode"] == 0 else "Error"}">{label}</span><h2>{text(name)}</h2><p>{text(title)}</p><a href="{quote(directory, safe="")}/report.html">查看报告 →</a></article>'
    body += '</div>'
    Path(output, "index.html").write_text(document("GAS 联机复测", body), encoding="utf-8")
