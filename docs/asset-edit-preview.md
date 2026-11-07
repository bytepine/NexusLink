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

重设普通、Widget、Anim、原生 GameplayAbility 蓝图的父类使用 `ManageAssetBlueprint` 的 `Operations=[{"Action":"Reparent","ParentClass":"Character"}]`，或传入蓝图资产/完整生成类路径。可先通过 `GetAssetEditPreview` 传同一操作获得兼容性、成员冲突、受影响继承属性和子蓝图列表；该结果不是编译证明。保留 Undo，完整编译和保存分别由顶层 `bCompile`、`bSaveToDisk` 控制；不隐式完整编译。离开当前父类继承链时必须显式设置该 operation 的 `bAllowPotentialDataLoss=true`，可能丢失原父类默认值或使引用失效。拒绝 PIE 期间操作、继承循环、专用蓝图类型不匹配、Actor/ActorComponent/普通对象跨族重设、专用蓝图基类或动画骨架不兼容、成员冲突，以及与新父类重复声明的接口。

只读路径查询使用 `GetNavigationPath`，传 `Start={X,Y,Z}`、`End={X,Y,Z}`，`World=Auto|Editor|Play`（Auto 优先 PIE/Game）。可指定普通 Actor 的 `ActorName` 作为导航 Agent 上下文，无需 AIController。接口不移动 Actor、不创建或重建导航；无现有导航时返回 `bNavigationAvailable=false`、`QueryResult=Unavailable`，不可达时 `bPathFound=false`。部分路径须显式允许 `bAllowPartialPath=true`；`MaxPoints`（1–2048，默认 256）只限制返回路径点，`Length` / `Cost` / `PointCount` 来自完整路径，`bTruncated` 标明截断。

打开资产编辑器使用 `ControlEditorAssetEditor(AssetPath=…, Action=Open)`，已打开时聚焦；`Focus` 只聚焦现有编辑器，`Status` 查询状态，`Close` 只关闭已保存且不与其他资产共享实例的编辑器。未保存时须先显式 `SaveAsset`，接口不隐式保存或丢弃修改。`GetEditorContext(Sections=["AssetEditors"])` 返回资产、编辑器名称和脏状态。`bValidateOnly=true` 不改变界面；Open/Focus/Close 要求可渲染的 GUI Editor，headless 可执行 Status 和只验证。返回 `bWasOpen`、`bIsOpen`、`bDirty`、`bCanClose`；不允许进入原子多调用批次。地图使用 `ControlEditorLevel`。


变量类型回读同时提供 `ContainerType`、`CanonicalType`、`ElementType`、Map 的 `KeyType` / `ValueType`、类型对象完整路径和 `Guid`；函数列表提供局部变量类型及 GUID。`RenameLocalVariable` 使用 `FunctionName`、`VariableName`、`NewVariableName`。成员重命名、局部重命名和 `ChangeVariableType` 均保护子蓝图，等待资产注册表扫描完成；类型变更还拒绝隐式改变 RepNotify 或 Set/Map 复制设置。先用 `GetAssetEditPreview` 预检 `ChangeVariableType`，确认后显式 `bAllowPotentialDataLoss=true`；它保留 GUID、清空旧默认值并重建本蓝图引用节点，不保证旧连接或外部依赖仍可编译。

标量预览及写后校验新增 `ManageAssetDataAsset/Set`（`PropertyName`）、`ManageAssetLevel/SetActorProperty`（精确 `ActorName` 和 `PropertyPath`）、`ManageAssetBlueprint/SetComponentProperty`（`ComponentName` 和 `PropertyPath`），均可用 `ExpectedValue` 防止过期写入。仍只支持 bool/数值/枚举/String/Name/Text 叶子及结构体/目标拥有子对象路径，拒绝容器和外部对象遍历。浮点值接受 `1e3` 等科学计数法，普通写入、预览、前值检查和写后验证保持一致，非法或非有限值不写入。关卡普通属性写入统一支持点分路径、空字符串和解析失败前置拒绝。

组件查询在 `GetAssetBlueprint(Sections=["Component"])` 中指定 `ComponentName`、最多 32 个 `PropertyPaths` 和 `Baseline=ClassDefault|ParentTemplate`，返回 `Value`、`DefaultValue`、`bDiffersFromDefault` 及 `BaselinePath`。不指定路径时有界列出可编辑字段。原有组件摘要保持组件类 CDO 基准；显式 ParentTemplate 比较目标父蓝图的实际模板。`ResetComponentPropertyToDefault` 使用相同基准，默认 ParentTemplate；自有组件无父模板时须明确选 ClassDefault。恢复仅限组件及其拥有的子对象，基准和目标的实际属性 Owner 均须在相应组件范围内；修改记录在实际属性 Owner 上，普通继承组件写入和恢复支持 Undo。拒绝通过复制父模板恢复实例化子对象或组件引用。

集中编译与 `CompileBlueprint` 返回 `CompilerErrorCount`、`CompilerWarningCount`、最多 128 条 `CompilerMessages`（严重级别、消息及可解析的 `GraphName` / `NodeId`）和截断标记。所有编译保存辅助路径在失败时不保存，磁盘回滚仍不属于内存 Undo 事务。

导航投影使用 `GetNavigationPoint(Point={X,Y,Z})`；导航路径可传 `bProjectStart` / `bProjectEnd`、`ProjectionExtent`（默认 100/100/250，非负轴）、`FilterClass`、正值 `AgentRadius` / `AgentHeight`，并用 `WorldName` 选择精确世界。返回导航系统/数据存在和构建状态；坐标投影失败有明确结果，不自动重建导航。`InteractRuntimeActorAI` 的 MoveTo 可显式传相同 Filter、目的地投影、部分路径策略和 `AcceptanceRadius`，并用 Stop 停止。`GetRuntimeActorAI` 返回当前移动状态与本工具捕获的最近请求结果，含请求 ID、完成标记及 Success/Blocked/OffPath/Aborted 等结果；无捕获时为 Unknown。请求接受不等于到达，捕获仅在当前进程有效。

`GetAssetExportOptions` 发现已注册导出器及格式，并可检查主输出路径存在状态。`ExportAsset/bValidateOnly=true` 不创建目录或写文件；`ConflictPolicy=Fail|Unique|Replace` 作用于主输出路径，默认 Replace 保持已有行为。导出器可能写附属文件，它们取决于具体格式和导出器选项，格式发现不宣称所有格式在所有配置下均能导出成功。
