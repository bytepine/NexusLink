# GAS activation diagnostics

[中文](gas-retest.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

Runner, HTML layout, and baseline code have moved into tools/retest. The gas_retest CLI, Python import paths, and legacy reports remain usable. New reports carry Domain=GAS; historical reports without Domain are interpreted as GAS and cannot be compared to BT reports.

Read-only snapshots, bounded activation event capture, and a standard-library Python replay tool.


CLI tools ship with source, not the plugin installation archive. Obtain the matching source and enter tools/ first; see [installation](usage-guide.en.md#21-安装).

## Runtime capabilities

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

Diagnostics neither grant nor activate abilities and do not invoke project custom checks. Cooldown matches configured GE classes only and does not prove custom cooldown logic permits activation. Interpret costs/custom rules together with actual failure events. Other players' abilities may be visible only to their owning client; absence does not prove the server never granted them. Overflow, timeout, and ambiguous identity remain unconfirmed.

## GenericActivation

Generic integration does not depend on example abilities, attributes, tags, or GameModes. Discover endpoints, Actor names, and full ability-class paths first; replace placeholders with real values and save project.json:

```json
{
  "Name": "Project activation",
  "Endpoints": {"Authority": {"Url": "http://127.0.0.1:45000/stream", "bAuthority": true}},
  "Actors": {"Target": {"Bindings": {"Authority": "ACTOR_FROM_DISCOVERY"}}},
  "AbilityPath": "/Script/YOUR_MODULE.YOUR_ABILITY",
  "ActivationEndpoint": "Authority",
  "AuthorityEndpoint": "Authority",
  "ExpectedEvent": "Committed",
  "FailureTagHints": {}
}
```

```sh
python -m gas_retest case.json --template GenericActivation --config project.json
python -m gas_retest case.json --preflight --output preflight
python -m gas_retest case.json --output results
```

ExpectedEvent accepts Committed or Failed. FailureTagHints maps actual failure tags to project explanations and appears only when matching FailureTags are captured. Generation is fully offline, refuses overwrites, and does not grant abilities or change runtime conditions. Explicit project configuration produces Setup/Teardown conditions; alternatively restore manually between runs. Retain the same case definition for failure/recovery comparisons. Configuration accepts only declared fields and explicit endpoints, without scanning and selecting automatically.

Initially Actors contains one logical target. Bindings names an Actor per endpoint, supporting ordinary Actors without PlayerState and projects whose ASC lives on Pawn/PlayerState. Mappings are user declarations; reports confirm only local identities, not automatic cross-endpoint equality. Existing ClassFilter + OwnerEndpoint or explicit PlayerId can associate by PlayerId instead. Each endpoint needs an independent process and explicit bAuthority. The initiating endpoint must have Authority or local control; initiating/authority endpoints each need exactly one matching InstancedPerActor ability. Observer ability visibility is not required.

Context adds ActorId, AscId, OwnerId, AvatarId, WorldIdentity, and Actor/Owner/Avatar class paths. Object identities comprise local slots/serials and are meaningful only within that process. Generic templates require initialized Owner/Avatar and available identity fields. Check all bound endpoints before each step; process/World/object replacement immediately stops all writes, including cleanup. Legacy cases can read older Context and check identity changes when newer fields exist.

Capture begins before each attempt and steps execute serially. Read-only diagnostics do not invoke project CanActivateAbility. Reports record snapshots and real activation events only. Missing server events, unknown identities, overflow, or observing other abilities remain unconfirmed. Templates/reports are independent of NexusDiagnosticGameMode.

The GenericActivation template accepts explicit endpoint/Actor mappings, ability class, activation/authority endpoints, expected server event, and optional observed failure-tag explanations. It supports component-owned ASCs on ordinary actors, pawns and player states. Binding declarations are not automatic cross-process identity proofs. Each attempt uses a fresh capture; only InstancedPerActor is supported. Existing templates, CLI and reports remain usable.

## Replay

From the source repository's `tools` directory (download the matching release's source code; the plugin installation archive does not include these tools):

```sh
python -m gas_retest case.json --output results
python -m gas_retest case.json --preflight --output preflight-results
python -m unittest gas_retest.test_runner gas_retest.test_report gas_retest.test_compare
```

### First connection

With the public NexusUnreal example compiled, run `python Script/gas_network_retest.py --ue-root <UE-directory> --quickstart` from that project. It starts one server and two clients, performs read-only binding/ability checks, reproduces prediction rejection, restores the condition, and verifies a successful activation. Open `Saved/GasRetest/index.html` to compare reports. Existing runs without `--quickstart` retain all eight scenarios and their configured repeat count.

With three diagnostic processes already running, generate cases with filled ports under the source tools/ directory. URLs must be discovered, confirmed direct UE endpoints. Template generation never connects to UE or overwrites files:

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

### Baselines and comparison

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

Both modes take a saved results file as the first argument, not a case definition, and are mutually exclusive with --preflight/--template. Compare before/after results of each fixed case separately; do not substitute PredictionRejected and Success, whose expectations differ. Baseline/comparison records retain complete raw evidence and distinguish step status from actual ability outcomes.

Case Setup/Steps/Teardown execute in order without automatic write retries. JSON Pointer references retain types; response default compression is restored before assertions. Check process/World before/after execution and do not continue writes after map changes/restarts. Scenario and cleanup failures are reported separately.

## Multiplayer example

The public [NexusUnreal example project](https://github.com/bytepine/NexusUnreal) provides the opt-in `NexusDiagnosticGameMode` and `Script/gas_network_retest.py`. It starts independent server/owner/observer processes and saves runnable cases plus reports for missing grants, required/blocked tags, cost, cooldown, prediction rejection, and successful LocalPredicted/ServerOnly activation. It does not change the default game mode. Use the example project's test guide for build and execution instructions.

To manually start fixed-case regression, run `python Script/gas_regression.py --ue-root <UE-directory>` in NexusUnreal. It validates both reviewed, signed baselines before starting UE, runs `PredictionRejected` and `Success`, compares each result, and archives the full HTML/JSON/JUnit/log evidence in `reports.zip`. Missing or changed baselines fail instead of being regenerated; unexecuted cases and child-process errors cannot pass the gate. The GitHub workflow only runs tool checks on GitHub-hosted Ubuntu machines. UE compilation and multiplayer regression require an explicit user command and are not scheduled onto a local runner. See [the sample test guide](https://github.com/bytepine/NexusUnreal/blob/master/Tests/README.md) for manual execution and baseline review.

The first release validates InstancedPerActor abilities. Serialize attempts for each ASC/skill and start a fresh capture per attempt. For unrelated concurrent gameplay, class/handle or PredictionKey conflicts must be treated as ambiguous; wall clocks across machines do not establish causation.
