"""Standard-library MCP scenario runner. Writes are sent exactly once."""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
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


class Runner:
    READ_CAPS = {"GetRuntimeActorAbilitySystem", "GetRuntimeActorProperty", "ListRuntimeActors"}

    def __init__(self, case, factory=Client):
        if case.get("SchemaVersion") != 1 or not isinstance(case.get("Endpoints"), dict) or not case["Endpoints"]:
            raise RunError("SchemaVersion=1 and nonempty Endpoints are required")
        self.case = case
        self.factory = factory
        self.clients = {}
        self.identities = {}
        self.contexts = {}
        self.state = {"Actors": {}, "Steps": {}}
        self.records = []

    def context(self, endpoint, actor):
        response = self.clients[endpoint].call("GetRuntimeActorAbilitySystem", {"ActorName": actor, "Sections": ["Context"]})
        if response["IsError"]:
            raise RunError("Cannot verify endpoint/Actor identity")
        context = response["Payload"].get("Context", {})
        identity = (context.get("ProcessId"), context.get("WorldId"))
        if None in identity or not all(type(item) is int and item > 0 for item in identity):
            raise RunError("Missing process/World identity")
        if endpoint in self.identities and self.identities[endpoint] != identity:
            raise RunError("Endpoint process or World changed; replay aborted")
        expected_authority = self.case["Endpoints"][endpoint].get("bAuthority")
        if expected_authority is not None and context.get("bAuthority") is not expected_authority:
            raise RunError("Endpoint authority role differs from the configured expectation")
        self.identities[endpoint] = identity
        self.contexts[endpoint] = context
        return context

    def preflight_abilities(self):
        for required in self.case.get("RequiredAbilities", []):
            endpoint, alias = required["Endpoint"], required["Actor"]
            actor = self.state["Actors"][alias][endpoint]
            result = self.clients[endpoint].call("GetRuntimeActorAbilitySystem", {"ActorName": actor, "Sections": ["Abilities"]})
            record = {"Id": f"Ability:{alias}/{endpoint}", "Stage": "Preflight", "Endpoint": endpoint,
                      "Status": "Passed", "Result": result, "AbilityPath": required["AbilityPath"]}
            self.records.append(record)
            matches = [ability for ability in result["Payload"].get("Abilities", [])
                       if ability.get("AbilityPath") == required["AbilityPath"]]
            if result["IsError"] or len(matches) != 1 or not matches[0].get("SpecHandle"):
                record.update(Status="Error", Message="Required granted ability missing or ambiguous: " + required["AbilityPath"])
                raise RunError(record["Message"])
            if matches[0].get("InstancingPolicy") != 1:
                record.update(Status="Error", Message="Required ability must use InstancedPerActor for this replay format")
                raise RunError(record["Message"])

    def bind_actors(self):
        # 先定位所属客户端，再以复制的 PlayerId 在其他进程中关联；Actor 名不跨进程复用。
        for alias, config in self.case.get("Actors", {}).items():
            owner_endpoint = config.get("OwnerEndpoint")
            player_id = config.get("PlayerId")
            endpoints = list(self.clients)
            if owner_endpoint:
                if owner_endpoint not in self.clients:
                    raise RunError("Unknown OwnerEndpoint")
                endpoints.remove(owner_endpoint)
                endpoints.insert(0, owner_endpoint)
            bound = {}
            for endpoint in endpoints:
                response = self.clients[endpoint].call("ListRuntimeActors", {"ClassFilter": config["ClassFilter"], "Limit": 500})
                if response["IsError"]:
                    raise RunError("Actor discovery failed")
                payload = response["Payload"]
                actors = payload.get("Actors", [])
                if payload.get("TotalCount", len(actors)) > len(actors):
                    raise RunError("Actor discovery truncated; narrow ClassFilter")
                matches = []
                for actor in actors:
                    name = actor.get("Name", actor.get("name"))
                    context = self.context(endpoint, name)
                    if endpoint == owner_endpoint and player_id is None:
                        if not context.get("bLocallyControlled"):
                            continue
                    elif player_id is None or context.get("PlayerId") != player_id:
                        continue
                    matches.append((name, context))
                if len(matches) != 1 or matches[0][1].get("PlayerId", -1) < 0:
                    raise RunError(f"Actor binding {alias}/{endpoint} is missing or ambiguous")
                name, context = matches[0]
                player_id = context["PlayerId"]
                bound[endpoint] = name
            self.state["Actors"][alias] = bound
        if self.state["Actors"] and len({identity[0] for identity in self.identities.values()}) != len(self.clients):
            raise RunError("Each endpoint must identify a distinct UE process")

    def invoke(self, step):
        endpoint = step["Endpoint"]
        args = resolve(step.get("Arguments", {}), self.state)
        capability = step["Capability"]
        if endpoint not in self.clients:
            raise RunError(f"Unknown endpoint {endpoint}")
        # 每个动作前验证所有已绑定目标，避免换图后写入另一场景。
        for actors in self.state["Actors"].values():
            self.context(endpoint, actors[endpoint])
        result = self.clients[endpoint].call(capability, args)
        self.state["Steps"][step["Id"]] = result
        if result["IsError"] != step.get("ExpectError", False):
            raise RunError(f"Unexpected capability error state: {capability}")
        if result["Payload"].get("Dropped", 0):
            raise RunError("Activation evidence overflow; outcome unconfirmed")
        events = result["Payload"].get("Events", [])
        if any(not event.get("bIdentityKnown") for event in events):
            raise RunError("Activation event identity unknown; outcome unconfirmed")
        identities = {(event.get("AbilityPath"), event.get("SpecHandle")) for event in events}
        if len(identities) > 1 or sum(event.get("Event") == "Activated" for event in events) > 1:
            raise RunError("Concurrent activation evidence ambiguous; use a fresh capture per attempt")
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
                    raise CheckFailed("Await timed out; last observation retained, authority outcome unconfirmed")
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
        requirements = self.case.get("RequiredAbilities", [])
        if not isinstance(requirements, list):
            raise RunError("RequiredAbilities must be an array")
        for required in requirements:
            if (not isinstance(required, dict) or required.get("Endpoint") not in self.case["Endpoints"]
                    or required.get("Actor") not in self.case.get("Actors", {})
                    or not isinstance(required.get("AbilityPath"), str) or not required["AbilityPath"]):
                raise RunError("RequiredAbilities needs a known Actor, Endpoint and nonempty AbilityPath")
        scenario_started = False
        try:
            for endpoint, config in self.case["Endpoints"].items():
                self.clients[endpoint] = self.factory(config)
            self.bind_actors()
            self.records.append({"Id": "BindActors", "Stage": "Preflight", "Status": "Passed",
                                 "Result": {"Actors": copy.deepcopy(self.state["Actors"])}})
            self.preflight_abilities()
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
        return {"SchemaVersion": 1, "Name": self.case.get("Name", "GAS retest"), "ExitCode": code,
                "Mode": "Preflight" if preflight else "Replay", "Contexts": self.contexts,
                "Endpoints": {name: {key: config[key] for key in ("Url", "bAuthority") if key in config and (key != "Url" or name in self.clients)}
                              for name, config in self.case["Endpoints"].items()},
                "Records": self.records, "Actors": self.state["Actors"], "Identities": self.identities}


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
    from .report import render_report
    (output / "report.html").write_text(render_report(report), encoding="utf-8")


def create_template(path, name, endpoints):
    if not all(endpoints.values()):
        raise RunError("Template requires --server, --owner and --observer direct UE URLs")
    endpoints = {role: direct_url(url) for role, url in endpoints.items()}
    case = json.loads((Path(__file__).parent / "examples" / (name + ".json")).read_text(encoding="utf-8"))
    for role, url in endpoints.items():
        case["Endpoints"][role]["Url"] = url
    with path.open("x", encoding="utf-8") as destination:
        destination.write(json.dumps(case, ensure_ascii=False, indent=2) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", type=Path)
    parser.add_argument("--output", default="gas-retest-results")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--preflight", action="store_true", help="Read-only endpoint, Actor and RequiredAbilities checks; never runs Setup/Steps/Teardown")
    mode.add_argument("--template", choices=("PredictionRejected", "Success"), help="Create an example case without connecting or overwriting a file")
    for role in ("server", "owner", "observer"):
        parser.add_argument("--" + role, help="Direct UE /stream URL for template generation")
    args = parser.parse_args(argv)
    try:
        if args.template:
            create_template(args.case, args.template, {role.title(): getattr(args, role) for role in ("server", "owner", "observer")})
            print(f"Created {args.case}; check with: python -m gas_retest \"{args.case}\" --preflight")
            return 0
        if any(getattr(args, role) for role in ("server", "owner", "observer")):
            raise RunError("--server/--owner/--observer require --template")
        case = json.loads(args.case.read_text(encoding="utf-8"))
        report = Runner(case).run(preflight=args.preflight)
        write_reports(report, args.output)
    except (OSError, ValueError, KeyError, TypeError, RunError) as exc:
        message = str(exc) if isinstance(exc, RunError) else type(exc).__name__
        print(f"GAS retest configuration error: {message}", file=sys.stderr)
        report = {"SchemaVersion": 1, "Name": args.case.stem, "ExitCode": 2, "Mode": "Preflight" if args.preflight else "Replay",
                  "Records": [{"Id": "Configuration", "Status": "Error", "Message": message}]}
        try:
            write_reports(report, args.output)
        except OSError:
            print("Could not write configuration error report", file=sys.stderr)
        return 2
    print(f"{report['Name']}: exit={report['ExitCode']}; " + ", ".join(f"{r['Id']}={r['Status']}" for r in report["Records"]))
    return report["ExitCode"]
