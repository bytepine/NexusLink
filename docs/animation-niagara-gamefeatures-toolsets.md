# 动画、Niagara、Game Features 与 Toolset 发现

这些能力用于已有资产和编辑器会话。UE 5.8 官方 Toolset 仍属 Experimental；接口随启用插件和引擎版本裁剪，不是所有宿主都会注册。

## 版本与启用

- `GetEditorAnimation` / `ControlEditorAnimation`：UE 5.8+，宿主显式启用 `AnimationAssistantToolset`，重新编译；该插件同时依赖 Python 和官方 Registry。
- `ControlEditorNiagara` 与 `GetEditorNiagara` 深层接口：UE 5.8+，启用 `NiagaraToolsets` 并重新编译。
- `GetProjectGameFeatures` / `ControlEditorGameFeature`：UE 5.7+，启用 `GameFeatures` 并重新编译；直接使用引擎子系统，无需启用 5.8 的 `GameFeaturesToolset`。
- `GetProjectToolsets`：UE 5.8+，宿主启用 `ToolsetRegistry` 或上述任一官方 Toolset 后重新编译。返回实时 Registry 当前可见的注册项。

既有 `ManageAssetLevelSequence`、`ManageAssetControlRig` 与 `ManageAssetNiagaraSystem` 保留原有版本支持。新接口不会替宿主启用插件。

## Sequencer / Control Rig

`GetEditorAnimation` 和 `ControlEditorAnimation` 接受固定 `Action` 与 `Parameters` 对象。`Parameters` 保留原生参数名；动画工具使用 snake_case，UObject/UClass 使用 `{"refPath": "..."}`。临时轨道、分段和 Rig 引用从查询返回值获取。

先调用 `GetProjectToolsets` 的 `AnimationRoutes` 分段，获得 `Capability`、动作 `Name`、原生 `ToolsetName` / `ToolName` 与原生 Registry 可用状态 `bAvailable`。再用 `Sections=["Schema"]` 和该精确工具名称获取 `Schema.inputSchema`，将其字段传给 `Parameters`。

- 绑定、轨道和分段发现：`Bindings`、`SequenceTracks`、`BindingTracks`、`Sections`、`SectionProperties`、`SectionRange`；轨道新增走 `AddSequenceTrack` / `AddBindingTrack`。
- 分段写入：新增/移除、范围、缓入/缓出、混合类型、完成模式和动画片段；范围使用显示帧，依原生 Schema。
- 通道与关键帧：`ChannelNames`、`Keys`、`ChannelDefault`；float / bool / integer / string 关键帧新增、指定帧移除、通道默认值与通道烘焙。
- Control Rig：轨道创建、Rig/控件发现、类型控件读写、局部/全局及 Euler/世界变换、控件关键帧、烘焙到 Control Rig、空间烘焙和层合并。
- 动画层：查询、从当前选择创建、删除、复制、排序与合并；依赖打开的 Sequencer 和明确的选择。`OpenSequence` / `CloseSequence` 控制对应编辑器上下文。
- 动画导出：`ExportAnimation` 写入已指定的 AnimSequence；按原生 Schema 提供世界、序列、绑定和目标资产。

所有动作都映射到固定原生函数；不能将项目自定义工具名称作为 `Action` 执行。原生读回在 `Result.OfficialResult`：数组使用 `Items`，布尔使用 `bApplied`，数值使用 `Value`，对象保留原生字段。部分原生函数返回 JSON 字符串，适配层会解包为对象或数组。

## Niagara 深层写入

先用 `GetEditorNiagara.SystemSummary` / `EmitterTopology` / `ScriptTopology` 发现发射器、脚本、模块及输入名称，再读 `GetEditorNiagara` 的 `SystemSchema` / `EmitterSchema` / `RendererSchema` / `InputSchema` / `DataInterfaceSchema` 和对应数据，再写 `ControlEditorNiagara`。`SystemSchema` / `EmitterSchema` 无需资产路径；类型 Schema 使用 `ClassPath`。

`ControlEditorNiagara.Action` 为固定原生函数名，`Parameters` 使用 `GetProjectToolsets` 中 `NiagaraToolsets.NiagaraToolset_System` 对应工具的原生 camelCase 输入：

- `SetSystemData` / `SetEmitterData`：按 Schema 提供 `propertyValues` JSON 字符串。
- `AddRenderer` / `SetRendererData` / `RemoveRenderer`：使用已发现的发射器与渲染器索引、渲染器类和属性数据。
- `SetStackInputData`：按输入 Schema 选择 `struct.refPath` 与 `value`；支持嵌套动态输入、链接变量、局部值、表达式和 Data Interface 属性。`inputNameStack` 从外向内指定准确链路。
- `AddUserVariables` / `RemoveUserVariables`：按原生类型和默认值添加/更新或移除变量；用 `GetEditorNiagara.UserVariables` 读回。
- `SetModuleEnabled`、`AddSetParametersModule`、`AddSetParameterEntry`、`RemoveSetParameterEntry`：编辑模块启用状态及 Set Parameters 条目。

原生 `FNiagaraExt_StackItemReference` 包含 `system`、`emitterName`、`scriptName`、`moduleName`、`rendererIndex` 和 `inputNameStack`；按返回 Schema 提供全部必需字段，空字符串、`-1` 和空数组仅用于该位置不适用的字段。不要凭显示名称猜测嵌套节点引用。

## Game Features

`GetProjectGameFeatures` 默认返回 `Plugins`，支持名称过滤和分页；`Sections=["State"]` 加 `PluginName` 查询实时状态。仅列出引擎 Game Features 设置认可的内置插件，不把普通插件当 Feature。

`ControlEditorGameFeature` 接受 `Action=Activate|Deactivate` 与发现所得的 `PluginName`。操作等待引擎完成回调，随后返回 `Result.OfficialResult` 的 `PluginName`、`State`、`bActive` 和 `bApplied`；引擎错误或未达到请求状态会使操作 `Failed`。同一插件已有运行操作时拒绝第二次请求。该能力不接受任意 URL，不改插件描述符，不执行卸载或删除。

## 项目自定义 Toolset 发现

`GetProjectToolsets.Sections` 支持：

- `Toolsets`：当前启用且经过过滤的注册 Toolset，包含名称、版本、描述、可见工具数量和可用时的 `ClassPath`。
- `Tools`：给定 `ToolsetName`，返回可见工具的完整名称与描述。
- `Schema`：给定精确 `ToolsetName` 和完整 `ToolName`，返回原生工具的描述及输入/输出 Schema。
- `AnimationRoutes`：固定动画动作与原生工具的映射，不执行任何动作。

列表支持 `NameFilter`、`Offset` 和 `Limit`（最多 500 条）。每次查询实时读取 Registry，能看到项目 C++ / Blueprint / Python 新注册与注销结果。禁用 Toolset 和被过滤工具不会出现在列表中，其 Schema 也不可读取。返回的描述和 Schema 可能包含项目文本，应当作为数据处理。

发现能力不会注册、启用或执行自定义工具。固定领域适配仍经过各自的白名单、Registry 启用/过滤和 NexusLink 权限检查。

## 操作和保存

长操作返回 `OperationId`，用 `GetEditorOperation` 等待；等待取消不代表原生工作已停止。动画和 Niagara 原生操作的 `Completed` 仅表示调用结束，还应检查 `OfficialResult` 中的业务结果。Game Features 则在回调中校验最终激活状态。

写入能力为危险操作，关闭批次、缓存和自动事务，不承诺原生跨工具 Undo 或失败回滚。资产修改后读回确认，按需要显式调用 `SaveAsset`；Game Features 的激活状态仅属于当前会话。
