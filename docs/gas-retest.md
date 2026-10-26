# GAS activation diagnostics / 技能激活诊断

Read-only snapshots, bounded activation event capture, and a standard-library Python replay tool.
只读快照、限量事件捕获，以及仅依赖 Python 标准库的复测工具。

## Runtime capabilities / 运行时工具

`GetRuntimeActorAbilitySystem` keeps its default `Abilities`, `Effects`, `Attributes` sections. Explicit diagnostic sections require `ActorName`:

Existing `All` queries still work without skill selectors or a capture: optional activation sections are omitted until their selector/`CaptureId` is provided.

- `Context`: process/World identity, Owner/Avatar, PlayerId, authority and local-control state.
- `Tags`: owned tags and blocked ability tags.
- `ActivationDiagnostics`: exactly one of `SpecHandle` (string from `Abilities`) or `AbilityPath` (full class path). Reports grant visibility, missing/blocked tags and configured cooldown GE remaining time. Does not invoke custom activation checks. Cost and custom rules remain unknown until reproduced.
- `ActivationEvents`: `CaptureId`; returns `Events`, `EventTypes`, `FailureTags`, `Dropped`, `LatestSequence`.

`InteractRuntimeActorAbilitySystem` adds:

- `TryActivateGrantedAbility`: explicit `ActorName` and exactly one selector above. Does not grant an ability. `bRequestAccepted` describes the local request only; inspect the server capture to determine the authority outcome. The existing `ActivateAbility` still grants before activating.
- `StartActivationCapture`: explicit `ActorName`; returns `CaptureId`.
- `StopActivationCapture`: matching `ActorName` and `CaptureId`; releases subscriptions and events.

Captures expire after 120 seconds, retain 256 events, and report dropped records. At most 16 captures may be active per process. Captures are removed on World destruction or MCP shutdown; destroyed ASCs are pruned. Capture before the attempt on each endpoint. `Activated`, `Committed`, `Failed`, and `Ended` are distinct events. A prediction is not a server confirmation. A missing server event is unconfirmed, not a proven rejection. Non-instanced/ambiguous identities are explicitly marked `bIdentityKnown=false`; do not attribute them to a specific attempt.

诊断不授予或激活技能，也不调用项目自定义检查。冷却只按配置的 GE 类匹配，不能证明自定义冷却逻辑允许激活；消耗、自定义规则应结合真实失败事件。其他玩家的技能可能只对所属客户端可见，未看到技能不等于服务器未授予。采集溢出、超时、身份不明确时，结果应标记为未确认。

## Replay / 复测

From the plugin's `tools` directory:

```sh
python -m gas_retest case.json --output results
python -m gas_retest case.json --preflight --output preflight-results
python -m unittest gas_retest.test_runner gas_retest.test_report gas_retest.test_compare
```

### First connection / 首次接入

With the public NexusUnreal example compiled, run `python Script/gas_network_retest.py --ue-root <UE-directory> --quickstart` from that project. It starts one server and two clients, performs read-only binding/ability checks, reproduces prediction rejection, restores the condition, and verifies a successful activation. Open `Saved/GasRetest/index.html` to compare reports. Existing runs without `--quickstart` retain all eight scenarios and their configured repeat count.

已有三个运行中的诊断进程时，可从插件 `tools/` 目录生成端口已填写的用例；URL 必须是发现后确认的 UE 直连端点。模板生成不连接 UE，也不覆盖现有文件：

```sh
python -m gas_retest rejected.json --template PredictionRejected --server http://127.0.0.1:45010/stream --owner http://127.0.0.1:45011/stream --observer http://127.0.0.1:45012/stream
python -m gas_retest rejected.json --preflight --output preflight-results
python -m gas_retest rejected.json --output rejected-results
```

The example endpoints above must be replaced with the actual discovered addresses. `--template Success` creates the repaired-case template. Templates depend on `NexusDiagnosticGameMode` and are not generic production skill definitions.

`--preflight` initializes MCP and reads actor identity, ownership, process/World, and any explicit `RequiredAbilities`. It never executes `Setup`, `Steps`, or `Teardown`. Without `Actors` it can only check MCP initialization; role/World identity remains unverified. A replay runs the same checks before any scenario writes. If a binding or required ability fails, scenario cleanup is also skipped because the scenario has not started.

Only explicit **direct UE** `/stream` endpoints are supported in v1. One process must host one play World. Credentials come from the endpoint's `TokenEnv` environment variable (default `NEXUS_MCP_TOKEN`) or the shared local NexusLink token file; never put tokens in the case JSON.

For multiplayer cases set endpoint `bAuthority=true` for the server and `false` for clients. Actor binding verifies this expectation and rejects duplicate processes, preventing a client-side event from being mistaken for server evidence.

Minimal read-only example (replace the endpoint and Actor name with discovered values):

```json
{
  "SchemaVersion": 1,
  "Name": "GAS context",
  "Endpoints": {"Server": {"Url": "http://127.0.0.1:45000/stream"}},
  "Steps": [{
    "Id": "context", "Type": "Call", "Endpoint": "Server",
    "Capability": "GetRuntimeActorAbilitySystem",
    "Arguments": {"ActorName": "ACTOR_FROM_DISCOVERY", "Sections": ["Context"]}
  }]
}
```

Cases have optional `Actors`, `Setup`, and `Teardown`. Actor bindings use `ClassFilter` and `OwnerEndpoint` to find the owning client's unique locally controlled Pawn, then match its replicated `PlayerId` on other endpoints. Alternatively supply a `PlayerId`. Missing or ambiguous matches fail before scenario writes. Reference an endpoint's Actor with `{"Ref":"/Actors/Hero/Server"}`; actual names are resolved separately per process.

`RequiredAbilities` is an optional array of `{"Actor":"Hero","Endpoint":"Server","AbilityPath":"/Script/Nexus.NexusDiagnosticAbility"}`. Each item must match exactly one granted InstancedPerActor spec on that bound Actor/endpoint. It reads `Abilities`, never grants or activates anything. List only prerequisites that should exist: an intentionally ungranted skill used in a negative test must not be a requirement. Other players' skill visibility on an observer is not required.

Steps:

- `Call`: `Id`, `Endpoint`, `Capability`, `Arguments`; `ExpectError=true` permits an expected MCP tool error while preserving its structured payload.
- `Assert`: `Actual`, `Op` (`Equal`, `Contains`, `Exists`, `Approx`), `Value`; numeric `Approx` accepts `Tolerance` (default 0.0001).
- `Await`: same call fields plus `Check` containing `Path` (JSON Pointer into the response), `Op`, `Value`. Only `GetRuntimeActorAbilitySystem`, `GetRuntimeActorProperty`, and `ListRuntimeActors` are permitted. Default timeout 10 seconds, interval 0.1 seconds; configurable maximum timeout 120 seconds.

A complete `{"Ref":"/Steps/context/Payload/Context/ProcessId"}` object resolves a prior response without converting its type. No expressions, loops, or scripts are evaluated. List `*_defaults` are restored before checks. Reports retain each endpoint, raw structured evidence and assertion result.

No writes are retried. Process/World changes abort replay. `Teardown` runs best-effort after failures, and cleanup failures are reported separately. Supply explicit cleanup for runtime modifications; this runner does not roll back arbitrary gameplay side effects. Captures expire even if cleanup cannot reach the process.

Output: offline `report.html`, `results.json`, `junit.xml`, terminal summary. Exit `0`: passed; `1`: assertion failed; `2`: configuration, transport, or incomplete evidence. An Await timeout keeps its final observation and reports the authority outcome as unconfirmed.

HTML reports show endpoint roles, the first failing steps, activation snapshots, deduplicated per-endpoint events, next checks, and expandable raw evidence. Case pass/fail is separate from the observed gameplay outcome: an expected rejection may pass its test. Missing authority events, dropped events, unknown identity or conflicting activations remain unconfirmed. Events stay in each capture's observed order; clocks across processes do not establish causation. Reports work offline with no external scripts or assets.

### Baselines and comparison / 保存基线与前后对比

Both operations consume recorded results offline; they never initialize MCP, activate skills, or rerun case steps:

```sh
python -m gas_retest previous/results.json --save-baseline gas-retest-results/baselines/Success.json
python -m gas_retest current/results.json --compare gas-retest-results/baselines/Success.json --output gas-retest-results/comparison-01
```

Save one baseline per case, such as `Success` and `PredictionRejected`. A baseline preserves the full report and UTC save time, requires a passed replay without dropped/unidentified events, and refuses to overwrite an existing file. A passed rejection case is a valid baseline: it asserts expected rejection and does not claim successful gameplay. Preflight cannot be saved as a replay baseline. Read-only cases with no authority activation evidence retain their unconfirmed outcome.

Compare runs of the same case. Name, mode, endpoint aliases and configured authority roles must agree. New reports carry a `CaseSignature` of the case definition, including steps, assertions and expected roles while excluding deployment URLs and token environment variable names. Different signatures are rejected, so weakening an assertion cannot masquerade as recovery. Legacy reports without signatures remain readable but prominently warn that the original assertion definition cannot be verified.

`comparison.html` and `comparison.json` distinguish newly failed checks, recovered checks, added checks and missing/unexecuted checks, including setup and cleanup. Missing checks are not treated as successful. The report separately compares authority conclusions, observed roles, event order/count, capture boundaries, ability paths and failure tags. Process/World IDs, URLs, timing, capture IDs, SpecHandle and PredictionKey are excluded from semantic event comparison; raw values remain in the evidence. Repeated capture snapshots are deduplicated. Event changes alone are observations, not proof of regression or repair; incomplete/ambiguous evidence remains unconfirmed.

The comparison directory must not already exist. `before/` and `after/` contain independent HTML, JSON and JUnit reports, so the comparison remains navigable offline after moving the directory. No original input or existing baseline is overwritten. To inspect recovery from a failed run, use `--compare failed/results.json` instead of saving that failed run as a baseline.

Comparison exit codes: `0` current case passed with no missing checks; `1` current case failed/errored or checks are missing; `2` invalid/incompatible input or an output error. These describe the comparison operation; the original replay exit codes remain visible in each side's report.

这两个模式的首个参数为已保存的结果文件，而非用例定义；与 `--preflight` / `--template` 互斥。固定用例的修改前、修改后结果分别对比；不要用预期不同的 `PredictionRejected` 和 `Success` 相互替代。基线和对比记录保留完整原始证据，步骤状态与实际技能结果分别展示。

用例中的 `Setup` / `Steps` / `Teardown` 顺序执行；不自动重试写操作。JSON Pointer 引用保留原始类型；遇到响应默认值压缩会先还原再断言。执行前后检查进程与 World，换图或重启后不继续写入。失败和清理失败分别进入报告。

## Multiplayer example / 联机示例

The public [NexusUnreal example project](https://github.com/bytepine/NexusUnreal) provides the opt-in `NexusDiagnosticGameMode` and `Script/gas_network_retest.py`. It starts independent server/owner/observer processes and saves runnable cases plus reports for missing grants, required/blocked tags, cost, cooldown, prediction rejection, and successful LocalPredicted/ServerOnly activation. It does not change the default game mode. Use the example project's test guide for build and execution instructions.

The first release validates InstancedPerActor abilities. Serialize attempts for each ASC/skill and start a fresh capture per attempt. For unrelated concurrent gameplay, class/handle or PredictionKey conflicts must be treated as ambiguous; wall clocks across machines do not establish causation.
