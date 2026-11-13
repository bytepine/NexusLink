<a id="行为树--blackboard-复测"></a>
# Behavior tree / Blackboard retesting
[中文](bt-retest.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

tools/bt_retest provides a fixed "blocked condition → change condition → wait for expected state → restore original value" flow. It accepts only explicitly bound Pawns or AIControllers, never selecting the first AIController. It runs on the Python standard library and shares MCP transport, step execution, HTML layout, and offline baseline code with gas_retest.

The CLI tools are supplied with source code, not the plugin installation archive. Obtain the matching version's source first; see [installation](usage-guide.en.md#21-安装).

<a id="配置与命令"></a>
## Configuration and commands

Find the exact Actor name with ListRuntimeActors, then use GetRuntimeActorBehaviorTree to obtain the full root tree path RootTreePath, running state, and Blackboard keys. Put the root path in the configuration's TreePath. Normal subtree entry/exit is not mistaken for target replacement. Create configuration under the source repository's tools/ directory:

```json
{
  "Name": "AI target condition",
  "Endpoint": {"Url": "http://127.0.0.1:45000/stream", "bAuthority": true},
  "ActorName": "Guard_0",
  "TreePath": "/Game/AI/BT_Guard.BT_Guard",
  "Condition": {"Key": "CanRun", "Type": "Bool", "BlockedValue": false, "ChangedValue": true},
  "Before": {"Node": "Wait for target"},
  "Expected": {"Node": "Move to target"},
  "Timeout": 10,
  "Interval": 0.1
}
```

Replace the example Actor, asset, key, and node names. Before is optional; when omitted, the blocked value is checked. Expected is required and accepts Node, Blackboard, or both, such as {"Blackboard":{"Key":"State","Type":"Name","Value":"Moving"}}. Node names match runtime ActiveNode. Conditions support Bool, Int, Float, Enum (0–255), String, and Name. Typed values must match declarations; float assertions use absolute tolerance 1e-5. Object references and containers are not writable conditions in this initial version.

```bash
python -m bt_retest case.json --template BlackboardCondition --config config.json
python -m bt_retest case.json --preflight --output preflight-results
python -m bt_retest case.json --output run-results
python -m bt_retest run-results/results.json --save-baseline baseline.json
python -m bt_retest next-results/results.json --compare baseline.json --output comparison-results
```

Preflight is read-only: it checks the explicit object, authority, tree path, running state, inherited keys, and types without setup, waiting, or cleanup. Execution saves the original condition value and attempts to restore and verify it after success or failure. Before each action it checks local identities for the process, World, Actor, Controller/Pawn, behavior tree, and Blackboard. Replacement or map changes stop execution; cleanup writes are not sent to a replacement target. Writes are not retried. Timeouts preserve the final snapshot; restoration failures are recorded separately as execution errors.

Outputs are report.html, results.json, and junit.xml. Exit 0 means checks passed; 1 means assertion/wait failure; 2 means an access, lifecycle, transport, or cleanup error. Reports prove only states observed while polling, not a complete node execution history. Raw snapshots retain tree paths, running state, old text values, and new typed values.

For offline comparison, exit 1 means the current report has failed, errored, or missing checks; input-format/domain errors return 2.

<a id="接口与兼容性"></a>
## Interfaces and compatibility

- GetRuntimeActorProperty adds View=Context, returning GAS-independent process, World, Actor, Pawn/Controller local identities and authority. Local identity does not prove an object represents the same player across endpoints.
- GetRuntimeActorBehaviorTree retains BehaviorTree, ActiveNode, and BlackboardKeys[].name/type/Value, and adds active-tree TreePath/TreeId, root-tree RootTreePath/RootTreeId, BlackboardId, ControllerId, and bRunning. Keys add TypeName, TypedValue, bTypedValueAvailable, and bInherited, including inherited keys. Unsupported custom types retain text values and explicitly mark typed values unavailable.
- SetBlackboard supports inherited keys and rejects invalid Bool/Int/Float/Enum strings and overflow. Legacy tools keep default target behavior; retesting always uses explicit binding.
- New reports carry Domain=BT|GAS; old reports default to GAS. Cross-domain comparison is forbidden. BT baselines use BtRetestBaseline; legacy GasRetestBaseline, GAS CLI, Python imports, and report filenames remain usable. Save only passed BT replays with confirmed observation and cleanup. Existing baselines and comparison directories are never overwritten.

Validation without UE: python -m unittest bt_retest.test_runner. The default bt-retest-results/ is ignored and excluded from release archives. Place other outputs in ignored directories too, to avoid uploading gameplay evidence.
