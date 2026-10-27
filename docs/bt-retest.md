# 行为树 / Blackboard 复测

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

`tools/bt_retest` 提供固定的“阻断条件 → 改变条件 → 等待预期状态 → 恢复原值”流程。只接受显式绑定的 Pawn 或 AIController，不选择第一个 AIController。Python 标准库即可运行；与 `gas_retest` 共用 MCP 传输、步骤执行、HTML 布局和离线基线代码。

## 配置与命令

先用 `ListRuntimeActors` 找到准确的 Actor 名，再用 `GetRuntimeActorBehaviorTree` 获取完整根树路径 `RootTreePath`、运行状态和 Blackboard 键，将根树路径填入配置的 `TreePath`。正常进入/退出子树不会被误判为目标替换。在插件 `tools/` 下创建配置：

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

替换示例中的 Actor、资产、键和节点名。`Before` 可省略，此时验证阻断值；`Expected` 必填，可用 `Node`、`Blackboard` 或两者，例如 `{"Blackboard":{"Key":"State","Type":"Name","Value":"Moving"}}`。节点名匹配运行时 `ActiveNode`。条件支持 Bool、Int、Float、Enum（0–255）、String、Name；类型化值必须匹配声明，浮点断言使用 `1e-5` 绝对容差。对象引用和容器不作为首批可写条件。

```bash
python -m bt_retest case.json --template BlackboardCondition --config config.json
python -m bt_retest case.json --preflight --output preflight-results
python -m bt_retest case.json --output run-results
python -m bt_retest run-results/results.json --save-baseline baseline.json
python -m bt_retest next-results/results.json --compare baseline.json --output comparison-results
```

预检只读：检查显式对象、权限、树路径、运行状态、继承键和类型；不执行设置、等待或清理。执行前保存条件原值，成功或失败后均尝试恢复并读回确认。每次动作前核对进程、World、Actor、Controller/Pawn、行为树和 Blackboard 本地身份；替换或换图后停止，不向替代目标发送清理写入。写入不重试。超时保留末次快照；恢复失败单独记录为执行错误。

输出 `report.html`、`results.json`、`junit.xml`。退出码 `0` 表示检查通过，`1` 表示断言/等待失败，`2` 表示接入、生命周期、传输或清理错误。报告仅证明轮询时观察到的状态，不代表完整节点执行历史。原始快照保留树路径、运行状态、旧文本值及新增类型值。

离线比较的退出码 `1` 表示本次报告存在失败、错误或缺失检查；输入格式/领域错误返回 `2`。

## 接口与兼容性

- `GetRuntimeActorProperty` 新增 `View=Context`，返回与 GAS 无关的进程、World、Actor、Pawn/Controller 本地身份及权限；本地身份不证明跨端对象是同一玩家。
- `GetRuntimeActorBehaviorTree` 保留 `BehaviorTree`、`ActiveNode`、`BlackboardKeys[].name/type/Value`，新增活动树 `TreePath/TreeId`、根树 `RootTreePath/RootTreeId`、`BlackboardId`、`ControllerId`、`bRunning`。键新增 `TypeName`、`TypedValue`、`bTypedValueAvailable`、`bInherited`；覆盖继承键，未支持的自定义类型保留文本值并标记类型化值不可用。
- `SetBlackboard` 支持继承键，拒绝错误的 Bool/Int/Float/Enum 字符串和溢出值。旧工具默认定位行为保持兼容；复测入口始终显式绑定。
- 新报告带 `Domain=BT|GAS`，旧报告默认 GAS，禁止跨领域比较。BT 基线为 `BtRetestBaseline`；旧 `GasRetestBaseline`、GAS CLI、Python 导入和报告文件名继续可用。只保存已通过且观察/清理均确认的 BT 复测，拒绝覆盖已有基线及比较目录。

无需 UE 的验证：`python -m unittest bt_retest.test_runner`。默认 `bt-retest-results/` 已忽略且不进入发布包；其他输出也应放在忽略目录中，避免上传业务证据。
