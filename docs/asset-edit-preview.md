# Asset edit preview / 资产修改预览

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

节点/连线及控件树的首批结构计划、StructureHash 前置条件和 bVerifyStructure 回读见[功能补充](feature-followups.md#结构预览与回读)。下文介绍原有标量模式。

`GetAssetEditPreview` 在临时属性存储中解析值，返回 `PropertyType`、`CurrentValue`、规范化的 `TargetValue`、`bValid` 与逐项 `Error`。它不写入真实属性、不编译、不保存、不创建 Undo。请求包含单个 `AssetPath`、目标 `Capability` 和 `Operations`，参数保持原有写操作格式：

```json
{
  "AssetPath": "/Game/YOUR_TABLE",
  "Capability": "ManageAssetDataTable",
  "Operations": [{"Action": "Set", "RowName": "YOUR_ROW", "FieldName": "YOUR_FIELD", "Value": "12"}]
}
```

路径、行和字段名须先通过读取工具确认。首批支持 `ManageAssetBlueprint/SetDefaults`、`ManageAssetDataTable/Set`、`ManageAssetUserWidget/SetProperty`。叶子类型限 bool、数值、枚举、字符串、Name、Text；支持结构体中的标量字段及目标拥有的子对象，例如 `Mesh.RelativeLocation.Z`。拒绝空指针、外部对象/资产引用、容器、固定数组及经容器跳转的路径。BP 必须已有可用的生成类和 CDO；预览不会为缺失的目标编译或创建 CDO。每条预览读取当前真实值，不模拟前序预览的写入。

写入时在 operation 中传入 `ExpectedValue`（建议使用预览的 `CurrentValue`），在顶层设置 `bVerify=true`。`ExpectedValue` 按相同类型规范化后比较，空字符串也是有效前值。前值不匹配时不写入。

```json
{
  "AssetPath": "/Game/YOUR_TABLE",
  "bVerify": true,
  "bSaveToDisk": true,
  "Operations": [{"Action": "Set", "RowName": "YOUR_ROW", "FieldName": "YOUR_FIELD", "Value": "12", "ExpectedValue": "7"}]
}
```

`bVerify` 默认关闭；启用后仅接受上述对应操作。蓝图 `SetDefaults` 先检查前值、在 Undo 记录后写入并即时回读；整批完成后，仅在 `bCompile=true` 时编译一次，再重新定位最终属性并回读，验证成功后按 `bSaveToDisk` 保存。`bCompile=false`（默认）只验证当前内存中的值，`ExpectedValue` 与 `bVerify` 均不会触发完整蓝图编译。同一属性顺序写多次时，收尾验证最后一次写入；此前各项保留即时回读结果。WBP `SetProperty` 的验证仍会编译后回读。结果提供 `OldValue`、`NewValue`、`ReadbackValue`、`bWritten`、`bVerified`、`Verification`；编译错误、读取不一致及保存错误分别报告。未请求验证时为 `Verification=NotRequested`。

单次 Manage 保持部分成功：前项成功的修改保留，任何验证失败阻止该调用自动保存，不能描述为资产已还原。跨调用 `CallCapability.Calls` 批次沿用事务回滚，保存在内存事务完成后进行，保存失败不宣称磁盘整体回滚。失败写入不自动重试。

Scalar preview uses temporary property storage and never mutates, compiles, saves or records Undo. Owned subobjects are allowed; external references are rejected. ExpectedValue is an optional optimistic precondition. Blueprint bVerify respects bCompile: false verifies memory only; true compiles once after the operations and verifies final values before saving. Widget verification still compiles and reads back. Structural plans and bVerifyStructure cover the bounded operations linked above. Existing partial-success and batch rollback boundaries remain in effect.

`ManageAssetBlueprint/RenameVariable` 使用 `VariableName` 与 `NewVariableName`，调用引擎原生成员重命名并保留变量 GUID 与引用。仅允许无子蓝图的叶子蓝图；资产注册表仍在扫描、存在已登记或已加载的子蓝图、变量带 RepNotify、成员继承或名称冲突时均提前拒绝。部分引擎的原生重命名会重置子类默认值，因此当前接口不提供带子蓝图的安全重命名。`VariableType` 支持基本类型、对象类、BlueprintType 结构体名/路径，以及 `T[]`、`Array<T>`、`Set<T>`、`Map<K,V>`；不支持嵌套容器，Set 元素和 Map 键须可哈希。

`GetAssetBlueprint` 的 `Component` 使用目标蓝图 CDO 中的原生组件和目标生成类的 SCS 继承覆盖模板。`TemplatePath` 标明值来源，`bOverridesResolved=false` 表示目标覆盖无法解析、已回退到祖先模板。

关卡属性检查使用 `GetAssetLevel` 的 `ActorProperties`：先从 `Actors` 节获取精确 `ActorName`，再传 1–32 个 `PropertyPaths`，返回 `Value`、`DefaultValue`、`bDiffersFromDefault`；默认值来自 Actor 类 CDO，与 `ResetActorPropertyToDefault` 一致。无需导出整个关卡。`ExportAsset` 则按输出扩展名选导出器，并以自动化模式执行；不保证大资产导出时间。

重设普通蓝图父类使用 `ManageAssetBlueprint` 的 `Operations=[{"Action":"Reparent","ParentClass":"Character"}]`，或传入完整生成类路径。保留 Undo，完整编译和保存分别由顶层 `bCompile`、`bSaveToDisk` 控制；不隐式完整编译。离开当前父类继承链时必须显式设置该 operation 的 `bAllowPotentialDataLoss=true`，可能丢失原父类默认值或使引用失效。拒绝 PIE 期间操作、继承循环、专用蓝图类型不匹配、Actor/ActorComponent/普通对象跨族重设，以及与新父类重复声明的接口。

只读路径查询使用 `GetNavigationPath`，传 `Start={X,Y,Z}`、`End={X,Y,Z}`，`World=Auto|Editor|Play`（Auto 优先 PIE/Game）。可指定普通 Actor 的 `ActorName` 作为导航 Agent 上下文，无需 AIController。接口不移动 Actor、不创建或重建导航；无现有导航时返回 `bNavigationAvailable=false`、`QueryResult=Unavailable`，不可达时 `bPathFound=false`。部分路径须显式允许 `bAllowPartialPath=true`；`MaxPoints`（1–2048，默认 256）只限制返回路径点，`Length` / `Cost` / `PointCount` 来自完整路径，`bTruncated` 标明截断。

打开资产编辑器使用 `ControlEditorAssetEditor(AssetPath=…, Action=Open)`，已打开时聚焦；`Focus` 只聚焦现有编辑器，`Status` 查询打开状态，`bValidateOnly=true` 不打开或聚焦。Open/Focus 要求可渲染的 GUI Editor，headless 宿主可执行 Status 和只验证。返回 `bWasOpen`、`bIsOpen`；不保存或关闭资产，不允许进入原子多调用批次。地图须使用带未保存关卡保护的 `ControlEditorLevel`。
