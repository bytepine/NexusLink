# 既有接口的官方 Toolset 后端

Capability 名称、输入参数与返回结构保持原有契约。UE 5.8+ 宿主明确启用对应官方插件并重新编译后，以下等价操作通过 Registry 执行；旧版本或宿主未选择该官方后端时，继续使用原有引擎 API。最低支持版本仍由各 Capability 自身决定。

## 当前接入

- `GetProjectSettings` 的 Containers / Categories / Sections 使用 `ConfigSettingsToolset`；仍保留原有显示名、过滤和分页。Properties / Values 维持现有属性类型及文本值格式。
- `GetProjectPlugins` 的 Plugins 列表使用 `PluginToolset`，再补齐既有元数据、名称过滤和分页；其余分段保留现有格式实现。
- `GetProjectDataRegistry` 的 Items / Values 使用 `DataRegistryToolset`；缓存值继续转为原有行格式，未缓存条目仍逐项返回 `bCached=false`，不会 Acquire。
- `ManageGameplayTags` 的 Add / Rename / Remove 使用 `GameplayTagsToolset`；保留 INI 来源校验、子标签/资产引用保护、写入后读回及即时落盘标记。新 INI 来源仍由引擎编辑器 API 创建，并先检查原生写入权限。
- `ManageAssetPhysicsAsset` 的 AddBody / RemoveBody / SetMassScale 使用 `PhysicsToolsets`；保留骨骼、重复 Body、质量范围验证及原有返回字段。
- `ManageAssetPCGGraph` 的 RemoveNode / AddEdge / RemoveEdge 使用 `PCGToolset`；保留内置节点保护、Pin 校验、默认 Pin 选择和删除边的幂等行为。
- `ManageAssetLevelSequence.SetDisplayRate` 的有效正数帧率使用官方动画 Sequencer 工具。PlaybackRange 使用现有 tick resolution 帧单位，保持原实现。
- `ManageAssetControlRig.SetPinDefault` 使用官方 ControlRig 工具，保留空值与 Pin 定位保护。
- `ManageAssetNiagaraSystem.RemoveEmitter` 使用 `NiagaraToolsets`，并保持原有删除结果与保存提示。
- `ManageAssetViewModel.RemoveBinding` 使用 `MVVMToolset`；BindingIndex / BindingId 预检仍在现有入口完成。
- `ManageAssetStaticMesh.SetMaterialSlot` 对唯一具名材质槽使用 `EditorToolset`。重名/无名槽和无法等价映射的情况保留索引实现；`ManageAssetMaterial.Recompile` 使用官方 Material 工具。

实际使用官方分支的查询分段或写操作返回 `Backend=OfficialToolset`。仅部分操作可等价映射，不能把该字段理解为整个 Capability 的所有操作都改用了 Toolset。

## 调用与权限

同步桥接只允许固定的、已批准同步执行的方法。它通过 Registry 检查 Toolset 与工具的启用及过滤规则，捕获原生脚本错误；不等待、不泵送游戏线程，不改变为 `OperationId` 返回。长操作继续使用既有 `GetEditorOperation` 模式。

官方后端的缺失方法、禁用、过滤或执行错误直接返回失败，不自动执行兼容写入。后端选择与原生方法是否被过滤分开判断，避免绕过原生权限；对于本来不能等价映射的子操作，明确保留既有后端。

插件无需被自动启用。桥接编译门控识别已支持领域的显式启用项，也支持显式启用 `ToolsetRegistry` 或 `AllToolsets`。读取注册列表和 Schema 仍可使用 `GetProjectToolsets`。

## 保留的兼容语义

参数/UMG、网格生成、场景整理与蓝图图编辑的新增路由和低版本动作范围见[编辑器兼容扩展](editor-compatibility.md)。UMG 的 FWidgetBlueprintOperationUtils 仅用于具备该 API 的 UE 5.8；更低版本通过 WidgetTree 实现受限结构编辑。

PhysicsAsset 的形状按类型和索引定位，官方形状编辑使用名称并可能改变数组顺序；创建资产的指定目标路径也不同。因此这些操作继续使用既有实现。MVVM 的自定义 ViewModel 名称、创建策略、绑定方向及双向转换保持现有实现。UMG 结构操作已经复用与官方 Toolset 相同的 `FWidgetBlueprintOperationUtils`。

Game Features 的完成回调、Live Coding 的非阻塞编译、运行时 Game/DS 与 UnLua 等能力保持各自后端。写入后的保存、事务及批次边界仍遵循原 Capability，原生 Toolset 的存在不额外提供回滚保证。
