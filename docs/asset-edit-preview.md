# Asset edit preview / 资产修改预览

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

节点/连线及控件树的首批结构计划、StructureHash 前置条件和 bVerifyStructure 回读见[功能补充](feature-followups.md#结构预览与回读)。下文介绍属性值预览与验证。

`GetAssetEditPreview` 在临时属性存储中解析值，返回 `PropertyType`、`CurrentValue`、规范化的 `TargetValue`、`bValid` 与逐项 `Error`。它不写入真实属性、不编译、不保存、不创建 Undo。请求包含单个 `AssetPath`、目标 `Capability` 和 `Operations`，参数保持原有写操作格式：

```json
{
  "AssetPath": "/Game/YOUR_TABLE",
  "Capability": "ManageAssetDataTable",
  "Operations": [{"Action": "Set", "RowName": "YOUR_ROW", "FieldName": "YOUR_FIELD", "Value": "12"}]
}
```

路径、行和字段名须先通过读取工具确认。支持 `ManageAssetBlueprint/SetDefaults`、`ManageAssetDataTable/Set`、`ManageAssetUserWidget/SetProperty` 等下文列出的属性操作。叶子类型支持 bool、数值、枚举、字符串、Name、Text、满足类型约束的对象/类引用（含 None），以及完整结构体、数组、Set、Map。引用本身可以赋值，继续沿引用跳转时仅允许目标持有的子对象；拒绝空指针或外部对象跳转、固定数组及经容器跳转的路径。含实例化对象引用的整个值返回 `Verification=Unsupported`，须改用所属子对象叶子路径。BP 必须已有可用的生成类和 CDO；预览不会为缺失的目标编译或创建 CDO。每条预览读取当前真实值，不模拟前序预览的写入。

写入时在 operation 中传入 `ExpectedValue`（建议使用预览的 `CurrentValue`），在顶层设置 `bVerify=true`。`ExpectedValue` 按相同类型规范化后比较，空字符串也是有效前值。前值不匹配时不写入。

```json
{
  "AssetPath": "/Game/YOUR_TABLE",
  "bVerify": true,
  "bSaveToDisk": true,
  "Operations": [{"Action": "Set", "RowName": "YOUR_ROW", "FieldName": "YOUR_FIELD", "Value": "12", "ExpectedValue": "7"}]
}
```

`bVerify` 默认关闭；启用后校验上述对应的反射属性操作和蓝图 `SetArray` / `ClearArray`。关卡及蓝图组件批次允许混入 `ResetActorPropertyToDefault` / `ResetComponentPropertyToDefault`（含数组恢复），恢复操作不接受 `ExpectedValue`，以完整反射复制和比较恢复默认值，包括文本导出中省略的零值字段；其他不支持的操作在任何写入前拒绝。蓝图 `SetDefaults` 先检查前值、在 Undo 记录后写入并即时回读；整批完成后，仅在 `bCompile=true` 时编译一次，再重新定位最终属性并回读，验证成功后按 `bSaveToDisk` 保存。`bCompile=false`（默认）只验证当前内存中的值，`ExpectedValue` 与 `bVerify` 均不会触发完整蓝图编译。同一属性顺序写多次时，收尾验证最后一次写入；此前各项保留即时回读结果。WBP `SetProperty` 的验证仍会编译后回读。结果提供 `OldValue`、`NewValue`、`ReadbackValue`、`bWritten`、`bVerified`、`Verification` 和 `VerificationSupport`；不支持的属性类型明确返回 Unsupported，编译错误、读取不一致及保存错误分别报告。未请求验证时为 `Verification=NotRequested`。

单次 Manage 保持部分成功：前项成功的修改保留，任何验证失败阻止该调用自动保存，不能描述为资产已还原。跨调用 `CallCapability.Calls` 批次沿用事务回滚，保存在内存事务完成后进行，保存失败不宣称磁盘整体回滚。失败写入不自动重试。

Scalar preview uses temporary property storage and never mutates, compiles, saves or records Undo. Object/class reference values are supported with native type checks; traversal is limited to owned subobjects, so external objects cannot be edited through a reference. ExpectedValue is an optional optimistic precondition. Blueprint bVerify respects bCompile: false verifies memory only; true compiles once after the operations and verifies final values before saving. Widget verification still compiles and reads back. Structural plans and bVerifyStructure cover the bounded operations linked above. Existing partial-success and batch rollback boundaries remain in effect.

`ManageAssetBlueprint/RenameVariable` 使用 `VariableName` 与 `NewVariableName`，调用引擎原生成员重命名并保留变量 GUID 与引用。仅允许无子蓝图的叶子蓝图；资产注册表仍在扫描、存在已登记或已加载的子蓝图、变量带 RepNotify、成员继承或名称冲突时均提前拒绝。部分引擎的原生重命名会重置子类默认值，因此当前接口不提供带子蓝图的安全重命名。`VariableType` 支持基本类型、对象类、`ClassReference<BaseClass>`、BlueprintType 结构体名/路径，以及 `T[]`、`Array<T>`、`Set<T>`、`Map<K,V>`；不支持嵌套容器，Set 元素和 Map 键须可哈希。

`GetAssetBlueprint` 的 `Component` 使用目标蓝图 CDO 中的原生组件和目标生成类的 SCS 继承覆盖模板。`TemplatePath` 标明值来源，`bOverridesResolved=false` 表示目标覆盖无法解析、已回退到祖先模板。

关卡属性检查使用 `GetAssetLevel` 的 `ActorProperties`：先从 `Actors` 节获取精确 `ActorName`，再传 1–32 个 `PropertyPaths`，返回 `Value`、`DefaultValue`、`bDiffersFromDefault`；默认值来自 Actor 类 CDO，与 `ResetActorPropertyToDefault` 一致。读取、预览和写入均限定 AssetPath 对应的实际 ULevel，包含已加载流送子关卡，不会通过主关卡路径跨包编辑；返回 ActorPath/LevelPath。Actor 值有效而默认子对象缺失时，返回 bDefaultUnavailable 和 DefaultError，不把整次读取标成失败。无需导出整个关卡。`ExportAsset` 则按输出扩展名选导出器，并以自动化模式执行；不保证大资产导出时间。

重设普通、Widget、Anim、原生 GameplayAbility 蓝图的父类使用 `ManageAssetBlueprint` 的 `Operations=[{"Action":"Reparent","ParentClass":"Character"}]`，或传入蓝图资产/完整生成类路径。可先通过 `GetAssetEditPreview` 传同一操作获得兼容性、成员冲突、受影响继承属性和子蓝图列表；该结果不是编译证明。保留 Undo，完整编译和保存分别由顶层 `bCompile`、`bSaveToDisk` 控制；不隐式完整编译。离开当前父类继承链时必须显式设置该 operation 的 `bAllowPotentialDataLoss=true`，可能丢失原父类默认值或使引用失效。拒绝 PIE 期间操作、继承循环、专用蓝图类型不匹配、Actor/ActorComponent/普通对象跨族重设、专用蓝图基类或动画骨架不兼容、成员冲突，以及与新父类重复声明的接口。

只读路径查询使用 `GetNavigationPath`，传 `Start={X,Y,Z}`、`End={X,Y,Z}`，`World=Auto|Editor|Play`（Auto 优先 PIE/Game）。可指定普通 Actor 的 `ActorName` 作为导航 Agent 上下文，无需 AIController。接口不移动 Actor、不创建或重建导航；无现有导航时返回 `bNavigationAvailable=false`、`QueryResult=Unavailable`，不可达时 `bPathFound=false`。部分路径须显式允许 `bAllowPartialPath=true`；`MaxPoints`（1–2048，默认 256）只限制返回路径点，`Length` / `Cost` / `PointCount` 来自完整路径，`bTruncated` 标明截断。

打开资产编辑器使用 `ControlEditorAssetEditor(AssetPath=…, Action=Open)`，已打开时聚焦；`Focus` 只聚焦现有编辑器，`Status` 查询状态，`Close` 只关闭已保存且不与其他资产共享实例的编辑器。未保存时须先显式 `SaveAsset`，接口不隐式保存或丢弃修改。`GetEditorContext(Sections=["AssetEditors"])` 返回资产、编辑器名称和脏状态。`bValidateOnly=true` 不改变界面；Open/Focus/Close 要求可渲染的 GUI Editor，headless 可执行 Status 和只验证。返回 `bWasOpen`、`bIsOpen`、`bDirty`、`bCanClose`；不允许进入原子多调用批次。地图使用 `ControlEditorLevel`。


变量类型回读同时提供 `ContainerType`、`CanonicalType`、`ElementType`、Map 的 `KeyType` / `ValueType`、类型对象完整路径和 `Guid`；函数列表提供局部变量类型及 GUID。`RenameLocalVariable` 使用 `FunctionName`、`VariableName`、`NewVariableName`。成员重命名、局部重命名和 `ChangeVariableType` 均保护子蓝图，等待资产注册表扫描完成；类型变更还拒绝隐式改变 RepNotify 或 Set/Map 复制设置。先用 `GetAssetEditPreview` 预检 `ChangeVariableType`，确认后显式 `bAllowPotentialDataLoss=true`；它保留 GUID、清空旧默认值并重建本蓝图引用节点，不保证旧连接或外部依赖仍可编译。

属性预览及写后校验支持 `ManageAssetDataAsset/Set`（`PropertyName`）、`ManageAssetLevel/SetActorProperty`（精确 `ActorName` 和 `PropertyPath`）、`ManageAssetBlueprint/SetComponentProperty`（`ComponentName` 和 `PropertyPath`），均可用 `ExpectedValue` 防止过期写入。支持上述反射叶子、完整结构体/容器及目标拥有子对象路径，拒绝外部对象遍历。浮点值接受 `1e3` 等科学计数法，普通写入、预览、前值检查和写后验证保持一致，非法或非有限值不写入。关卡普通属性写入统一支持点分路径、空字符串和解析失败前置拒绝。

## 批量蓝图迁移

- `GetAssetBlueprint(Sections=["Defaults"], PropertyPaths=["Mesh.SkeletalMesh","Mesh.RelativeRotation"])` 精确读取嵌套默认值。每个请求路径返回值或错误，并用 `MissingPropertyPaths` 列出缺失项。
- `AddNode` 的 `K2Node_CustomEvent` 使用 `CustomEventName`；`SetNode` 使用同名字段重命名。名称冲突提前拒绝并返回最终名称；对自定义事件传 `FunctionName` 会明确报错。
- `Reparent` 预检返回有界的 `AffectedGraphReferences`（变量、事件覆盖及父函数调用）。Manage 结果的 `bRequiresCompile` 与 `GeneratedClassState=Missing|Stale|CompileError|Current` 表示当前生成类状态；`bChanged=true` 不表示生成类已编译。新父类字段须在显式编译后写入。
- `RemapMemberReferences` 使用 `OldMemberName`、`NewMemberName`，仅迁移本蓝图中的继承 self 属性 getter/setter。目标须是当前父类的 Blueprint-visible 属性，值 pin 类型须精确一致，setter 还要求属性可写；修改成员引用与值 pin 名，保留 pin GUID、默认值及连线。`bValidateOnly=true` 或 `GetAssetEditPreview` 可先检查目标与匹配节点。函数/事件及外部对象引用不属于此动作。
- `SetArray` 使用类型化 JSON `Values`，`[]` 清空；数值元素及嵌套数值字段必须使用有限 JSON number，整数不得包含小数或超出属性／原生转换器范围，非法数组在任何元素写入前拒绝。`ClearArray` 无需 `Value`，两者均使用 `PropertyPath`，可通过 `ComponentName` 指定组件模板。通用字符串 `Value` 沿用 Unreal import-text：容器须用括号包围或传空字符串；在 UE4，空字符串清空数组，`()` 可能生成一个默认元素；结构体未指定字段保留前值。混合的属性/数组/恢复批次可统一设置 `bVerify=true`。
- `GetAssetLevel(Sections=["ActorPropertySchema"])` 使用精确 `ActorName`，可用 `PropertyPaths` 指定结构体/所属子对象根路径；省略时从 Actor 开始。`SchemaDepth=0..3`、`MaxSchemaProperties=1..256` 限制递归与整批字段总量。返回字段名、类型、可编辑性、子字段及截断标志，不跟随外部对象；实例非空而 CDO 为空仍可发现。

`SaveAsset`、`CompileBlueprint(bSaveToDisk=true)` 和 manage 自动保存会在进入 SavePackage 前检查 UFunction super 链。临时对象、被替换类及循环引用返回 `SaveDiagnostics`（`ErrorKind=InvalidSuperFunction`、`FunctionPath`、`SuperFunctionPath`、`Reason`），不尝试保存并保留脏标记。成功编译与安全保存是两个独立结果；本检查不修改第三方插件的运行时函数绑定。

组件查询在 `GetAssetBlueprint(Sections=["Component"])` 中指定 `ComponentName`、最多 32 个 `PropertyPaths` 和 `Baseline=ClassDefault|ParentTemplate`，返回 `Value`、`DefaultValue`、`bDiffersFromDefault` 及 `BaselinePath`。不指定路径时有界列出可编辑字段。原有组件摘要保持组件类 CDO 基准；显式 ParentTemplate 比较目标父蓝图的实际模板。`ResetComponentPropertyToDefault` 使用相同基准，默认 ParentTemplate；自有组件无父模板时须明确选 ClassDefault。恢复仅限组件及其拥有的子对象，基准和目标的实际属性 Owner 均须在相应组件范围内；修改记录在实际属性 Owner 上，普通继承组件写入和恢复支持 Undo。拒绝通过复制父模板恢复实例化子对象或组件引用。

集中编译与 `CompileBlueprint` 返回 `CompilerErrorCount`、`CompilerWarningCount`、最多 128 条 `CompilerMessages`（严重级别、消息及可解析的 `GraphName` / `NodeId`）和截断标记。所有编译保存辅助路径在失败时不保存，磁盘回滚仍不属于内存 Undo 事务。

导航投影使用 `GetNavigationPoint(Point={X,Y,Z})`；导航路径可传 `bProjectStart` / `bProjectEnd`、`ProjectionExtent`（默认 100/100/250，非负轴）、`FilterClass`、正值 `AgentRadius` / `AgentHeight`，并用 `WorldName` 选择精确世界。返回导航系统/数据存在和构建状态；坐标投影失败有明确结果，不自动重建导航。`InteractRuntimeActorAI` 的 MoveTo 可显式传相同 Filter、目的地投影、部分路径策略和 `AcceptanceRadius`，并用 Stop 停止。`GetRuntimeActorAI` 返回当前移动状态与本工具捕获的最近请求结果，含请求 ID、完成标记及 Success/Blocked/OffPath/Aborted 等结果；无捕获时为 Unknown。请求接受不等于到达，捕获仅在当前进程有效。

`GetAssetExportOptions` 发现已注册导出器及格式，并可检查主输出路径存在状态。`ExportAsset/bValidateOnly=true` 不创建目录或写文件；`ConflictPolicy=Fail|Unique|Replace` 作用于主输出路径，默认 Replace 保持已有行为。导出器可能写附属文件，它们取决于具体格式和导出器选项，格式发现不宣称所有格式在所有配置下均能导出成功。

导航写入先读 `GetEditorContext(Sections=["CurrentMap"])`：`Levels` 含已加载关卡 AssetPath、当前/选中/锁定/可见及 bDirty 状态、场景 BoundsMin/BoundsMax 与导航体积。用选定关卡路径调用 `ManageAssetLevel`，operation `Action=CreateNavMeshBoundsVolume`，提供世界坐标 `BoundsMin={X,Y,Z}`、`BoundsMax={X,Y,Z}`；体积由原生 Actor Factory 与 CubeBuilder 创建实际尺寸、保持单位缩放；ResizeNavMeshBoundsVolume 接受精确 ActorName 和同一组 BoundsMin/BoundsMax，重建现有体积几何并刷新导航边界，返回实际几何边界，保留 Undo，编译/构建互不隐式触发。保存体积所在关卡后，用 `ControlEditorScene(Action=BuildNavigation)` 提交当前世界的原生导航构建，再按 OperationId 查询 `GetEditorOperation`。请求先返回任务标识；原生构建阶段可能延迟编辑器响应，等待查询完成，勿重发构建请求。检查 `CompatibilityResult.Outcome`、`bBuildComplete`、`bHasNavigationData`，再用导航点/路径查询核对可行走区域。`bSaveToDisk=true` 要求构建前所有已加载关卡均已保存，拒绝时附 DirtyLevelPaths，并在完成后保存生成的关卡包；保存粒度是整个包，构建期间继续编辑的内容也属于包内容。

旧式 Skeleton 重定向使用 `ManageAssetAnimSequence` 或 `ManageAssetAnimBlueprint`，operation 为 `Action=Retarget`、`TargetSkeleton=...`。先传 `bValidateOnly=true` 检查 `MissingBones`、`UnmappedAnimations` 与 `AffectedAssets`（不同时传顶层 bCompile/bSaveToDisk）；写入须 `bAllowPotentialDataLoss=true`。需要保留源资产时先 DuplicateAsset。ABP 以 `AnimationRemaps=[{SourceAssetPath,TargetAssetPath}]` 显式替换动画引用，目标动画必须已有目标 Skeleton、类型一致；先重定向父 ABP，当前 ABP 须无子类。序列 `bConvertSpaces` 控制参考姿势空间转换；存在引用动画时须显式 `bRetargetReferredAssets=true`，原生流程影响的附属脏资产列在 `AdditionalDirtyAssets`，随 `bSaveToDisk` 一并保存。ABP 的编译由顶层 bCompile 控制。此入口使用旧式 Skeleton 重定向，UE5 IK Retargeter 使用各自能力。

`AddVariable` 的 DefaultValue 在引擎创建变量时提供；完整编译后，只对新增字段补齐已加载子类 CDO 和实例中仍为类型初始值的内容，保留已有字段和非默认覆盖值。`NewVariableDefaultPropagations` 与 `NewVariableDefaultPackages` 回报受影响项；子蓝图或关卡包仍须按需显式保存。已有错误资产不自动重置，避免覆盖业务配置。
