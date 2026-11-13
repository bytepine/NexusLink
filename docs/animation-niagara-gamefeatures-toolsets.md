# 动画、Niagara、Game Features 与自有动作发现

中文 · [English](animation-niagara-gamefeatures-toolsets.en.md)

[文档导航](README.md) · [独立后端](toolset-backends.md)

这些能力用于已有资产和编辑器会话。NexusLink 自身直接调用引擎公开 API，不直接链接或调用官方 Toolset、ToolsetRegistry 或官方 Python toolset 包。具体动作由引擎版本及相应领域插件支持范围决定，标准模块的[传递依赖遵循引擎要求](toolset-backends.md)。

## 版本与启用

- Sequencer / Control Rig 使用引擎 MovieScene、Sequencer、ControlRig 与相关编辑器脚本 API。高级功能按实际接口裁剪，无需 AnimationAssistantToolset 或 SequencerAnimMixerToolset。
- AnimMixer 动作要求 UE 5.8+，并在宿主显式启用引擎领域插件 `MovieSceneAnimMixer` 后重新编译、重启。它提供 Mixer 轨道、层与过渡的数据模型；NexusLink 不通过 Toolset 派发该动作。未启用时，动作发现返回 `bSupported=false`，并列出 `PluginEnabled:MovieSceneAnimMixer` 前提。
- Niagara 使用 Niagara / NiagaraEditor 数据、图及 Stack API，无需 NiagaraToolsets。
- Game Features 需启用 GameFeatures；其能力使用引擎子系统及异步完成回调，无需 GameFeaturesToolset。
- `GetProjectToolsets` 查询 NexusLink 自有注册元数据，不通过外部 Registry 发现或派发。

领域插件须在宿主中启用并重新编译；本工具不为一次请求自动启用插件。

## Sequencer / Control Rig

`GetEditorAnimation` 与 `ControlEditorAnimation` 接受固定 Action 和 Parameters。先用 `GetProjectToolsets(Sections=["Actions"], Capability=..., Action=...)` 查询 InputSchema、MinimumVersion、Prerequisites、bSupported 和 Reason，再按该动作的参数名构造 Parameters。对象、轨道、分段和 Rig 引用来自实际查询结果。

FBX 导出要求序列具有非空且上下界有效的播放范围，UE 5.9 起引擎会显式校验；先配置目标序列的播放范围，导出动作不会代为修改资产范围。导出使用新的 `.fbx` 文件路径，导入使用已经存在的 `.fbx` 文件与明确的导入设置。

- 绑定、轨道和分段：Bindings、SequenceTracks、BindingTracks、Sections、SectionProperties、SectionRange，以及新增/删除和范围配置。
- 通道与关键帧：ChannelNames、Keys、ChannelDefault；Float / Bool / Integer / String 关键帧写入、删除、默认值和烘焙。
- Control Rig：Rig/控件发现与值读写、变换、关键帧、轨道及当前引擎支持的烘焙和层操作。
- Sequencer 扩展：条件、Outliner、绑定转换、FBX/动画导入导出、序列文件夹及 AnimMixer；要求底层 API 存在，必要时先打开目标 Sequence 并建立明确选择。

动作只执行 NexusLink 预定义实现，不接受外部工具类名或任意 Python 路径。动画读写使用 Backend=Independent；既有 Result.CompatibilityResult / Result.OfficialResult 只作为历史数据容器，具体字段按输出 Schema 读取。

## Niagara 深层写入

先用 SystemSummary / EmitterTopology / ScriptTopology 发现发射器、脚本、模块与输入名称，再读对应 Schema 和数据，最后使用 ControlEditorNiagara。

- System/Emitter 数据按属性 Schema 读写；UserVariables 使用明确的类型和默认值。
- Renderer 使用已发现的发射器、渲染器索引、类型和属性数据。
- 模块、Stack Input、动态输入链、链接变量与 Set Parameters 操作按独立动作 Schema 提供字段，不猜测嵌套节点引用。
- StackIssues 查询与 ExecNiagaraStackFix 使用同一系统返回的明确 IssueId / FixId，不自动选择修复。

输入结构和类型范围取决于当前引擎实际 API。查询、验证或写入失败时返回具体原因，不把不支持的输入静默转换为其他类型。

## Game Features

GetProjectGameFeatures 默认返回 Plugins，支持名称过滤和分页；Sections=["State"] 加 PluginName 查询实时状态。仅列出引擎 Game Features 设置认可的内置插件。

ControlEditorGameFeature 接受 Action=Activate|Deactivate 与发现所得的 PluginName。等待引擎完成回调，随后核对最终状态；结果包含 PluginName、State、bActive 和 bApplied，历史容器 Result.OfficialResult 不表示调用官方 Toolset。同一插件已有运行操作时拒绝第二次请求，不接受任意 URL，不改描述符，也不卸载或删除插件。

## NexusLink 自有发现

GetProjectToolsets 保留历史能力名及 Sections 参数，其内容现为 NexusLink Capability 与动作元数据：

- Actions：默认分段，支持 Capability / Action 筛选，返回动作可用性与 Parameters 的 InputSchema。
- AnimationRoutes：动画领域的动作元数据，不再返回外部工具映射；按各条目的 Capability / Name 查询和调用。
- Toolsets：按自有注册来源分组，返回组名、描述和可见 ToolCount。
- Tools：传 Toolsets 返回的 ToolsetName，返回组内可见的 Capability 名称和描述。
- Schema：传精确 ToolsetName 与 Tools 返回的 ToolName，返回该 Capability 的 Name、Description、InputSchema 及可用的 OutputSchema。

列表支持 NameFilter、Offset 和 Limit（最多 500 条），遵守 NexusLink 的能力禁用与宿主可见性规则。不读取或执行外部 C++ / Blueprint / Python Toolset，旧外部 ToolsetName/ToolName 需要迁移到这里返回的自有标识。项目描述和 Schema 均按数据处理。

## 操作和保存

长操作返回 OperationId，用 GetEditorOperation 等待；等待取消不代表底层工作已停止。Completed 表示执行结束，还须核对业务结果。Game Features 在回调中验证激活状态。

大多数编辑器动作直接改变内存或编辑器状态。资产更改后显式编译/保存；长操作不提供跨工具事务回滚。结果可能包含临时对象引用，编辑器重启或图重建后须重新发现。
