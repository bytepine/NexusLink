# 独立引擎后端与版本适配

[文档导航](README.md) · [编辑器能力范围](editor-compatibility.md)

NexusLink 自行实现编辑器操作、动作发现和参数转换，直接调用 Unreal Engine 及相应领域插件的 API。本插件不直接链接或调用官方 Toolset 插件、ToolsetCore 或 ToolsetRegistry，也不加载官方 Python toolset 包。启用或禁用官方 Toolset 插件不会改变 NexusLink 的执行路径，也不需要启动官方 MCP 服务。

UE 5.9 的标准编辑器模块（如 UMGEditor）在引擎内部依赖 ToolsetCore；标准 PCG 插件在 Editor 目标启用 ToolsetRegistry，PCGEditor 模块也依赖 ToolsetCore、ToolsetRegistry 和 ToolsetRegistryEditor。因此保留标准 PCG 领域功能时，UE 5.9 编辑器宿主须保留引擎要求的 Registry 依赖，可分离的官方 Toolset 插件仍可关闭。这些传递依赖无法由 NexusLink 从插件侧移除；上述独立实现约束指 NexusLink 自身的直接依赖和调用，不表示物理传递依赖为零。

## 领域插件与自有实现

领域插件提供实际的数据类型及引擎功能，仍须按需启用并重新编译。例如 Niagara、PCG、ControlRig、GameFeatures、DataRegistry、ModelViewViewModel 和 SemanticSearch。可选领域插件不等于官方 Toolset 依赖。

- 设置和插件发现直接读取 ISettingsModule / IPluginManager；持久化使用对应引擎编辑器 API。
- GameplayTags、PhysicsAsset、PCG、Niagara、材质、网格、MVVM、UMG、Sequencer 和 Control Rig 操作使用各自的引擎 API，并保留 NexusLink 的参数校验、版本限制、编译及保存流程。
- Slate 使用自有会话 Ref 缓存与可见控件树；Automation 使用本地 AutomationController；Live Coding 使用非等待底层入口。
- `GetProjectToolsets` 保留历史名称与分段参数，但现查询 NexusLink 自有 Capability 分组及动作 Schema，不读取外部 Toolset 注册表。
- `GetProjectSkills` 扫描项目、启用插件及引擎 AgentTools 目录下的 SKILL.md；Details 只读 List 返回的精确文件路径。
- `SearchAssetsSemantic` 直接使用 SemanticSearch 领域插件的索引和 Embedding Provider，保留真正的混合向量/BM25 与相似资产检索；模型或索引缺失时明确报错。

## 返回值与长操作

编辑器操作报告 `Backend=Independent`。通过 `OperationId` 查询 `GetEditorOperation`；`Completed` 只表示执行结束，仍须检查测试、编译和领域数据中的业务结果。

既有 `Result.CompatibilityResult` / `Result.OfficialResult` 键可作为兼容的数据容器保留，名字不再表示执行后端。UI、编译等接口也可直接在 `Result` 中返回结构化数据，以各能力的输出 Schema 为准。

保存、权限、事务和批次边界沿用各 Capability 的约定；断连或等待取消不能触发自动重放写入。版本适配根据底层引擎实际 API 裁剪，不把不存在的类型或功能伪造成可用操作。

## 从官方 Toolset 后端迁移

1. 保留实际使用的领域插件及引擎要求的依赖后重新编译 NexusLink；UE 5.9 标准 PCG 须保留 Registry。NexusLink 自身无需额外启用可分离的官方 Toolset 插件或官方 MCP 服务。
2. 使用 `GetProjectToolsets(Sections=["Actions"])` 获取动作级 InputSchema、版本要求及支持状态。`Toolsets/Tools/Schema` 现返回 NexusLink 自有分组和 Capability Schema；旧外部 Toolset 名称不再可查询。
3. `GraphDSL` / `WriteGraphDSL` 现使用 `UnrealGraphText` 原生节点文本。先读取本工具返回的 Code；旧官方 S-expression 文档不能直接作为 Code 传入。结构化 `WriteGraphPlan` 保持其节点计划契约。
4. 项目技能改用 SKILL.md 文件；先 List，再按返回的 SkillPath 读取，旧 AgentSkill 类路径不再是 Details 输入。

具体调用范围见[编辑器工具](editor-toolsets.md)、[蓝图与场景操作](editor-compatibility.md)及[动画与领域动作](animation-niagara-gamefeatures-toolsets.md)。
