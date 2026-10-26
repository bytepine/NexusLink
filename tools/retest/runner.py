"""Standard-library MCP scenario runner. Writes are sent exactly once."""
from __future__ import annotations

import copy
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time
from urllib import request
from urllib.parse import urlparse
import xml.etree.ElementTree as ET


class RunError(Exception):
    """Configuration, transport, lifecycle, or incomplete evidence."""


class CheckFailed(Exception):
    """An observed value violated the scenario's assertion."""


def expand_defaults(value):
    if isinstance(value, list):
        return [expand_defaults(item) for item in value]
    if not isinstance(value, dict):
        return value
    result = {key: expand_defaults(item) for key, item in value.items()}
    for key, default in value.items():
        if key.endswith("_defaults") and isinstance(default, dict):
            target = key[:-9]
            if isinstance(result.get(target), list):
                result[target] = [expand_defaults({**default, **item}) if isinstance(item, dict) else item
                                  for item in result[target]]
    return result


def pointer(root, path):
    if path == "":
        return root
    if not isinstance(path, str) or not path.startswith("/"):
        raise RunError("JSON Pointer must start with /")
    try:
        for part in path[1:].split("/"):
            key = part.replace("~1", "/").replace("~0", "~")
            if isinstance(root, list):
                if not key.isascii() or not key.isdigit() or (len(key) > 1 and key.startswith("0")):
                    raise RunError(f"Invalid JSON Pointer array index: {path}")
                root = root[int(key)]
            else:
                root = root[key]
        return root
    except (KeyError, IndexError, ValueError, TypeError) as exc:
        raise RunError(f"Missing JSON Pointer: {path}") from exc


def resolve(value, state):
    if isinstance(value, dict):
        if set(value) == {"Ref"}:
            return copy.deepcopy(pointer(state, value["Ref"]))
        return {key: resolve(item, state) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve(item, state) for item in value]
    return value


def check(actual, spec):
    op = spec.get("Op", "Equal")
    expected = spec.get("Value")
    if op == "Exists":
        passed = actual is not None
    elif op == "Equal":
        passed = type(actual) is type(expected) and actual == expected
    elif op == "Contains":
        passed = isinstance(actual, (str, list, dict)) and expected in actual
    elif op == "Approx":
        numbers = (actual, expected, spec.get("Tolerance", 0.0001))
        if any(type(item) not in (float, int) or not math.isfinite(item) for item in numbers) or numbers[2] < 0:
            raise RunError("Approx requires finite numbers and nonnegative Tolerance")
        passed = abs(actual - expected) <= numbers[2]
    else:
        raise RunError(f"Unknown assertion: {op}")
    if not passed:
        raise CheckFailed(f"{op}: actual={actual!r}, expected={expected!r}")


def machine_token():
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData/Local")))
    elif sys.platform == "darwin":
        base = Path.home() / "Library/Application Support"
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
    try:
        return (base / "NexusLink/mcp-auth-token").read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def direct_url(value):
    try:
        if not isinstance(value, str):
            raise ValueError()
        value = value.rstrip("/")
        parsed = urlparse(value)
        if (parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.port is None or parsed.port < 1
                or parsed.path != "/stream" or parsed.username or parsed.password or parsed.query or parsed.fragment):
            raise ValueError()
    except ValueError as exc:
        raise RunError("Url must be a direct UE http(s)://host:port/stream endpoint without credentials, query or fragment") from exc
    return value


class Client:
    def __init__(self, config):
        self.url = direct_url(config["Url"])
        self.token = os.environ.get(config.get("TokenEnv", "NEXUS_MCP_TOKEN"), "") or machine_token()
        self.session = ""
        self.counter = 0
        self._rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                 "clientInfo": {"name": "nexus-gas-retest", "version": "1"}})
        self._rpc("notifications/initialized", {}, notification=True)

    def _rpc(self, method, params, notification=False):
        self.counter += 1
        payload = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            payload["id"] = self.counter
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        if self.session:
            headers["Mcp-Session-Id"] = self.session
        try:
            req = request.Request(self.url, json.dumps(payload, allow_nan=False).encode(), headers)
            with request.urlopen(req, timeout=15) as response:
                if method == "initialize":
                    self.session = response.headers.get("Mcp-Session-Id", "")
                    if not self.session:
                        raise RunError("initialize did not return a session id")
                if notification:
                    return {}
                body = json.load(response)
        except (OSError, ValueError) as exc:
            # 不输出请求头、凭证或可包含凭证的底层异常文本。
            raise RunError(f"MCP transport failed during {method} ({type(exc).__name__})") from exc
        if body.get("id") != self.counter or body.get("jsonrpc") != "2.0":
            raise RunError("MCP response id or protocol mismatch")
        if "error" in body:
            raise RunError(f"MCP protocol error: {body['error'].get('code')}")
        return body.get("result", {})

    def call(self, capability, arguments):
        result = self._rpc("tools/call", {"name": "CallCapability",
                           "arguments": {"Capability": capability, "Arguments": arguments}})
        payload = result.get("structuredContent")
        if not isinstance(payload, dict):
            # NexusLink 也支持把结构化 JSON 放在 MCP text content 中。
            content = result.get("content", [])
            try:
                payload = json.loads(next(item["text"] for item in content if item.get("type") == "text"))
            except (StopIteration, ValueError, KeyError, TypeError):
                payload = {"ErrorText": "\n".join(item.get("text", "") for item in content)} if result.get("isError") else None
        if not isinstance(payload, dict):
            raise RunError("Capability returned no structured evidence")
        return {"IsError": bool(result.get("isError")), "Payload": expand_defaults(payload)}

    def close(self):
        if self.session:
            headers = {"Mcp-Session-Id": self.session}
            if self.token:
                headers["Authorization"] = "Bearer " + self.token
            try:
                with request.urlopen(request.Request(self.url, headers=headers, method="DELETE"), timeout=2):
                    pass
            except OSError:
                pass


def case_signature(case):
    # 地址和凭证来源随部署变化，不影响用例定义；断言、步骤及角色配置必须一致。
    definition = {**case, "Endpoints": {name: {key: value for key, value in config.items()
                  if key not in ("Url", "TokenEnv")} for name, config in case["Endpoints"].items()}}
    return hashlib.sha256(json.dumps(definition, sort_keys=True, allow_nan=False).encode()).hexdigest()


class BaseRunner:
    DOMAIN = "GAS"
    READ_CAPS = set()

    def __init__(self, case, factory=Client):
        if case.get("SchemaVersion") != 1 or not isinstance(case.get("Endpoints"), dict) or not case["Endpoints"]:
            raise RunError("SchemaVersion=1 and nonempty Endpoints are required")
        if case.get("Domain", self.DOMAIN) != self.DOMAIN:
            raise RunError("Case belongs to a different domain")
        self.case = case
        self.factory = factory
        self.clients = {}
        self.identities = {}
        self.contexts = {}
        self.object_identities = {}
        self.state = {"Actors": {}, "Steps": {}}
        self.records = []


    def invoke(self, step):
        endpoint = step["Endpoint"]
        args = resolve(step.get("Arguments", {}), self.state)
        capability = step["Capability"]
        if endpoint not in self.clients:
            raise RunError(f"Unknown endpoint {endpoint}")
        # 每个动作前验证所有已绑定目标，避免换图后写入另一场景。
        for actors in self.state["Actors"].values():
            for bound_endpoint, actor in actors.items():
                self.context(bound_endpoint, actor)
        result = self.clients[endpoint].call(capability, args)
        self.state["Steps"][step["Id"]] = result
        if result["IsError"] != step.get("ExpectError", False):
            raise RunError(f"Unexpected capability error state: {capability}")
        self.validate_result(result)
        return result

    def execute_step(self, step):
        kind = step.get("Type")
        if kind == "Assert":
            spec = resolve(step, self.state)
            check(spec["Actual"], spec)
            return {"Passed": True}
        if kind == "Call":
            return self.invoke(step)
        if kind != "Await" or step.get("Capability") not in self.READ_CAPS:
            raise RunError("Await must use a supported read-only capability; unknown step Type")
        timeout = step.get("Timeout", 10)
        interval = step.get("Interval", 0.1)
        if type(timeout) not in (int, float) or not 0 < timeout <= 120 or type(interval) not in (int, float) or not 0.01 <= interval <= timeout:
            raise RunError("Await requires 0 < Timeout <= 120 and 0.01 <= Interval <= Timeout")
        deadline = time.monotonic() + timeout
        while True:
            result = self.invoke(step)
            spec = resolve(step["Check"], self.state)
            try:
                try:
                    actual = pointer(result, spec["Path"])
                except RunError:
                    if spec.get("Op") != "Exists":
                        raise
                    actual = None
                check(actual, spec)
                return result
            except CheckFailed:
                if time.monotonic() >= deadline:
                    self.state["Steps"][step["Id"]] = result
                    raise CheckFailed("Await timed out; last observation retained, expected outcome unconfirmed")
                time.sleep(interval)


    def run_steps(self, steps, cleanup=False, stage="Steps"):
        for step in steps:
            started = time.monotonic()
            record = {"Id": step.get("Id", "?"), "Endpoint": step.get("Endpoint"), "Status": "Passed",
                      "Stage": stage, "Type": step.get("Type"), "Capability": step.get("Capability"),
                      "StartedAt": datetime.now(timezone.utc).isoformat()}
            try:
                result = self.execute_step(step)
                self.state["Steps"][step["Id"]] = result
                record["Result"] = result
            except Exception as exc:
                record["Status"] = "Failed" if isinstance(exc, CheckFailed) else "Error"
                record["Message"] = str(exc) if isinstance(exc, (RunError, CheckFailed)) else type(exc).__name__
                if step.get("Id") in self.state["Steps"]:
                    record["Result"] = self.state["Steps"][step["Id"]]
            record["DurationMs"] = round((time.monotonic() - started) * 1000, 2)
            self.records.append(record)
            if record["Status"] != "Passed" and not cleanup:
                break


    def run(self, preflight=False):
        all_steps = [step for key in ("Setup", "Steps", "Teardown") for step in self.case.get(key, [])]
        ids = [step.get("Id") for step in all_steps]
        if any(not isinstance(item, str) or not item for item in ids) or len(set(ids)) != len(ids):
            raise RunError("Step Id values must be unique, nonempty strings")
        # 在任何写操作之前拒绝结构错误；不尝试执行自由表达式。
        for step in all_steps:
            kind = step.get("Type")
            if kind not in ("Call", "Await", "Assert"):
                raise RunError("Unknown step Type")
            if kind in ("Call", "Await"):
                if step.get("Endpoint") not in self.case["Endpoints"] or not isinstance(step.get("Capability"), str):
                    raise RunError("Call/Await requires known Endpoint and Capability")
                if not isinstance(step.get("Arguments", {}), dict):
                    raise RunError("Arguments must be an object")
                if kind == "Await" and (step["Capability"] not in self.READ_CAPS or not isinstance(step.get("Check"), dict)):
                    raise RunError("Await requires supported read capability and Check")
            if kind == "Assert" and "Actual" not in step:
                raise RunError("Assert requires Actual")
        json.dumps(self.case, allow_nan=False)
        self.validate_case()
        scenario_started = False
        try:
            for endpoint, config in self.case["Endpoints"].items():
                self.clients[endpoint] = self.factory(config)
            self.bind_actors()
            self.records.append({"Id": "BindActors", "Stage": "Preflight", "Status": "Passed",
                                 "Result": {"Actors": copy.deepcopy(self.state["Actors"]),
                                            "BindingEvidence": {name: "UserDeclared" if "Bindings" in config else "PlayerId"
                                                for name, config in self.case.get("Actors", {}).items()}}})
            self.preflight_targets()
            if not preflight:
                scenario_started = True
                self.run_steps(self.case.get("Setup", []), stage="Setup")
                if all(record["Status"] == "Passed" for record in self.records):
                    self.run_steps(self.case.get("Steps", []))
        except Exception as exc:
            if not self.records or self.records[-1]["Status"] != "Error":
                self.records.append({"Id": "Initialize", "Stage": "Preflight", "Status": "Error", "Message": str(exc) if isinstance(exc, RunError) else type(exc).__name__})
        finally:
            if scenario_started:
                self.run_steps(self.case.get("Teardown", []), cleanup=True, stage="Teardown")
            for client in self.clients.values():
                client.close()
        code = 2 if any(item["Status"] == "Error" for item in self.records) else int(any(item["Status"] == "Failed" for item in self.records))
        return {"SchemaVersion": 1, "Domain": self.DOMAIN, "Name": self.case.get("Name", self.DOMAIN + " retest"), "ExitCode": code,
                "CaseSignature": case_signature(self.case),
                "Mode": "Preflight" if preflight else "Replay", "Contexts": self.contexts,
                "Endpoints": {name: {key: config[key] for key in ("Url", "bAuthority") if key in config and (key != "Url" or name in self.clients)}
                              for name, config in self.case["Endpoints"].items()},
                "Records": self.records, "Actors": self.state["Actors"], "Identities": self.identities,
                "FailureTagHints": self.case.get("FailureTagHints", {}),
                "EvidenceAbilityPath": self.case.get("EvidenceAbilityPath")}


    def validate_case(self):
        pass

    def validate_result(self, result):
        pass

def write_reports(report, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    records = report["Records"]
    root = ET.Element("testsuite", name=report["Name"], tests=str(len(records)),
                      failures=str(sum(item["Status"] == "Failed" for item in records)),
                      errors=str(sum(item["Status"] == "Error" for item in records)))
    for record in records:
        node = ET.SubElement(root, "testcase", name=record["Id"])
        if record["Status"] != "Passed":
            ET.SubElement(node, "failure" if record["Status"] == "Failed" else "error", message=record["Message"])
        ET.SubElement(node, "system-out").text = json.dumps(record.get("Result", {}), ensure_ascii=False)
    ET.ElementTree(root).write(output / "junit.xml", encoding="utf-8", xml_declaration=True)
    if report.get("Domain", "GAS") == "BT":
        from bt_retest.report import render_report
    else:
        from gas_retest.report import render_report
    (output / "report.html").write_text(render_report(report), encoding="utf-8")
