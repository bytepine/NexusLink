# PCG 参数、PhysicsAsset、GameplayTags 与 DataRegistry

[文档导航](README.md) · [参数参考](tool-reference.zh.md)

## PCG 参数闭环（UE 5.8+）

启用宿主的 PCGToolset 并重新编译后，`GetEditorPCG` / `ControlEditorPCG` 沿用官方 Registry 的启用及过滤规则，结果位于 `Result.OfficialResult`。启动一次后按 OperationId 查询 GetEditorOperation；Completed 是调用结束，还需检查 bApplied、Error 等业务字段。写入不提供事务回滚，避免在断连后自动重放。

先通过资产搜索与 GetAssetPCGGraph / GraphStructure 取得 object path 和节点 refPath，再调用：

- `GetEditorPCG(Action=NodeInfo, NodePath=...)`：读取节点设置；`ControlEditorPCG(Action=UpdateNode, NodePath=..., Parameters={...})` 写入原生设置属性。可选 NodeTitle，Parameters 不接受空对象。属性名称和值结构以 NativeNodeSchema / NodeInfo 为准。
- `SetGraphParameters`：AssetPath 和 ParameterDefinitions（1–64 项）。每项 Name、Type 必填，可选 Description、ContainerType=None|Array、DefaultValueJson；后者为 JSON 编码文本，例如数值 `1.5`、布尔 `true`、字符串 `"value"`。仅支持 Schema 枚举中的 PCG 值类型。使用 GetEditorPCG GraphSchema 读回。
- `RemoveGraphParameters`：AssetPath、ParameterNames（1–64 个已发现名字）。
- `SetInstanceParameters`：ActorPath、非空 Parameters；`ResetInstanceParameters`：ActorPath、ParameterNames。使用 `GetEditorPCG(Action=InstanceParameters)` 读回，Execute 后再读取 NodeData 验证生成结果。
- `GetEditorPCG(Action=AvailableSubgraphs)` 发现子图；`ControlEditorPCG(Action=AddSubgraph)` 指定 AssetPath、SubgraphPath、唯一 NodeName，可选 Parameters / NodeTitle。拒绝自身、递归子图和已有节点名。

Spawn 也接受可选 Parameters 作为新实例初值。图资产写后显式 SaveAsset；实例变化属于编辑器关卡状态，按项目流程保存关卡。UE 5.4–5.7 保留基础 ManageAssetPCGGraph 节点/连线操作，新增官方参数命令只在 5.8 对应插件可用时注册。

## PhysicsAsset 精细编辑

`CreateAssetPhysicsAsset(AssetPath, SkeletalMeshPath, bAssignToMesh=false)` 使用引擎自动碰撞生成并保存新资产，拒绝覆盖已有资产。可选赋给并保存源 Mesh，会真实改变该 Mesh；默认不赋值。

先 GetAssetPhysicsAsset 取得 Bodies、Shapes 和 Constraints，再用 ManageAssetPhysicsAsset 的 Operations：

- AddBody / RemoveBody：BoneName。删除 Body 同时移除相关约束及维护引擎的 Body/碰撞索引；带预览 Mesh 时，新增 Body 必须属于其骨骼。
- UpdateShape / RemoveShape：BoneName、ShapeType=Sphere|Capsule|Box、ShapeIndex（该类型数组内索引）。修改可选 Radius、HalfHeight、ExtentX/Y/Z、CenterX/Y/Z、RotationPitch/Yaw/Roll；旋转适用于 Box / Capsule。尺寸必须为正且有限；HalfHeight 沿用既有胶囊圆柱段半长定义，Box Extent 为半尺寸。
- SetMassScale：BoneName、正数 MassScale。
- SetConstraintLimits：JointName；按需指定 Swing1Motion、Swing2Motion、TwistMotion（Free/Limited/Locked）和对应 Limit（0–180 度）。未指定的轴保留原值。

读回包含 Shapes 的类型、索引、局部尺寸/变换，以及 MassScale 和各约束运动方式/角度。移除后索引可能变化，应重新查询再编辑。已有 AddSphere / AddCapsule / AddBox / SetPhysicsType / AddConstraint / RemoveConstraint 继续可用。写后读回并按需 SaveAsset。

## GameplayTags 配置管理

启用 GameplayTagsEditor。`GetGameplayTags(Sections=["Info"], Tag=...)` 查询显式状态、来源与注释；Sources 列出普通 INI TagList 来源，可传 NameFilter。

`ManageGameplayTags(Tag=..., Operations=[...])` 操作单个目标标签：

- Add：必须提供简单的 TagSource `.ini` 文件名，可选 Comment；没有对应来源时由引擎创建。不能传目录路径。
- Rename：NewTag 必填，使用引擎重命名和 Redirect 机制；同次调用后续操作跟随新名。目标不能已存在或位于旧标签子树。
- Remove：仅普通 INI 来源中的显式叶标签，存在子标签或资产引用时拒绝；先通过 Referencers 检查并处理引用。

这是危险配置写入能力，沿用 Confirm / Custom 权限。引擎会立即写 Config，不受 bSaveToDisk 或 SaveAsset 控制，不支持 CallCapability.calls[] 或事务回滚；前面成功的操作不会因后续失败而撤销。原生代码标签和受限来源不接受写入。修改后查询 Info / Hierarchy 验证。

## DataRegistry 查询（UE 5.0+）

宿主须启用 DataRegistry 并重新编译。`GetProjectDataRegistry` 默认为 Registries，只查询已经注册的类型；不会为发现而加载或激活额外 Registry。未启用时不注册能力。

传 RegistryName 后可以查询 Info、Schema、Items、DataSources、RuntimeSources。发现列表支持 NameFilter、Offset、Limit（最多 500）；Info 返回资产路径、描述、条目 Struct、数量与可用状态，Schema 返回字段名称和类型。

Values 要求显式 ItemNames（1–100）。每项返回 Name 和 bCached；命中时 Value 是真实 Struct 的 JSON，未命中时给出 NotCached 原因。查询不 Acquire、不等待加载、不返回占位默认行；需要先由使用该 Registry 的游戏系统完成获取，再读取缓存。数据源条目包含 ClassPath、来源 AssetPath、Description、初始化/临时状态与 Availability，配置源和运行时源分开查询。
