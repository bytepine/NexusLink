# UMG、材质、项目配置与 Sequencer 扩展

[文档导航](README.md) · [前四组编辑器兼容能力](editor-compatibility.md) · [后端选择](toolset-backends.md)

可等价映射的 UE 5.8 操作复用宿主启用的官方 Toolset。原生方法缺失、过滤、禁用或执行失败时直接返回错误，不再执行兼容写入；宿主未选择原生后端时使用引擎编辑器接口。不会为一次调用自动启用官方插件。

## UMG

`ManageAssetUserWidget` 新增 Rename、SetVariable、BindEvent、AddUIComponent、RemoveUIComponent、MoveUIComponent。这些动作沿用 Operations[] 和现有编译/保存选项。

- Rename 使用 WidgetName 和唯一的 NewWidgetName。兼容路径更新 WidgetTree、蓝图变量引用、委托绑定、动画绑定和已存在的 UIComponent 目标名；不接受任意对象路径作为目标。
- SetVariable 显式传 bIsVariable。BindEvent 使用 EventName（如 OnClicked），目标必须是当前蓝图中已暴露、已编译的控件变量，事件须 BlueprintAssignable。可先单独执行 SetVariable 并传顶层 bCompile=true，再绑定事件；重复绑定复用已有节点。
- UIComponent 操作使用 ComponentClass；移动还需同一控件上已存在的 RelativeComponentClass 和 bMoveAfter。UE 5.6+ 提供相应框架，低于此版本明确不支持。`GetAssetUserWidget(Sections=["UIComponents"])` 返回控件、组件类型和组件路径，并支持原有分页。

5.8 优先使用 UMGToolSet；兼容路径通过 WidgetBlueprint、Kismet 编辑工具和 UIComponentWidgetBlueprintExtension 完成，不为操作创建 Widget 编辑器窗口。

## 材质与材质函数

`ManageAssetMaterial` 新增 Layout、DeleteUnused、RenameParameterGroup、UngroupParameters，支持 Material 和 MaterialFunction。`GetAssetMaterial(Sections=["ParameterGroups"])` 返回组名及参数数量。

参数组重命名使用 GroupName/NewGroupName，目标组不能为空；None 表示未分组参数。UngroupParameters 仅清除分组，不删除参数。清理从材质输出或函数输出追踪引用，兼容实现保留 CustomOutput 表达式；不存在的组明确报错。

5.8 的布局、材质清理与参数组操作复用 EditorToolset.MaterialTools。MaterialFunction 图的创建、节点/连线/属性编辑继续使用已有接口，函数清理使用兼容图遍历。兼容实现不引入新的图格式；保存仍沿用资产操作的保存选项。

## 项目设置

先用 `GetProjectSettings` 发现 Container/Category/Section 和 Properties，再使用 `ManageProjectSettings`。设置值查询保留原有 Value 文本，并增加 JsonValue，以便按原类型读写。

Operations[] 支持：

- SetProperties：Properties 对象使用精确的可编辑 Config 属性名和类型正确的 JSON 值，最多 64 个字段。先在副本上验证全部字段，再提交并保存。
- Save：调用注册设置节的保存接口。
- Reset：仅对声明支持重置的设置节调用 ResetDefaults。

成功返回 bSavedToDisk；bRestartRequired 根据属性重启元数据判定，Reset 保守标记需要重启。缺少重启元数据并不能证明任意设置都会立即生效。原生与兼容路径都可能在保存失败时留下已变更的内存值，失败信息会明确说明；不会重新调用另一后端。

UE 5.8 复用 ConfigSettingsToolset，旧版使用 ISettingsModule/ISettingsSection 和编辑器属性变更生命周期。SetProperties 本身会持久化，不是临时预览；该 Capability 标记为危险写操作。

## 项目插件

`ManageProjectPlugin` 以 PluginName 定位，Operations[] 支持 Create、SetEnabled、UpdateDescriptor、AddDependency、RemoveDependency，均报告 bSavedToDisk/bRestartRequired。

Create 在当前工程 Plugins 下创建唯一的纯内容插件，不创建 C++ 模块。5.8 使用官方内容模板；旧版使用 FPluginDescriptor 和工程插件管理 API。名称不能含路径，已有目录/插件不覆盖。描述符编辑仅允许当前工程内的插件；启用状态通过工程配置写入，生效时间为下次启动。

UpdateDescriptor 允许 Properties 中的字符串字段 FriendlyName、Description、Category、VersionName、CreatedBy、CreatedByURL、DocsURL、MarketplaceURL、SupportURL。其余描述符内容保留。依赖编辑要求不同的已发现插件，拒绝启用依赖环；修改通过插件的 UpdateDescriptor 同步缓存与磁盘。5.8 复用 PluginToolset。

## Sequencer 与 AnimMixer

`Get/ControlEditorAnimation` 继续以固定 Action 和 Parameters 调用，不接受任意工具类名。新增原生动作覆盖 Conditions、Outliner、自定义绑定、FBX/动画链接和 Mixer 的 Layer、Transition、Decoration 操作。名称、原生字段和可用性可通过 `GetProjectToolsets(Sections=["AnimationRoutes"])` 及对应 Schema 查询。

兼容实现包括 Section/Track 条件读写/清除，以及已有 TrackRowMetadata 的行条件；引擎未提供该条件容器时返回版本限制。普通 Sequence 文件夹使用 SequenceFolders/AddSequenceFolder/RemoveSequenceFolder，Parameters 包含 sequence 引用、写入时的 folder_name；仅移除空根文件夹。

ExportFbx/ImportFbx 使用 world、sequence、bindings、fbx_file_path 及可选原生设置引用；兼容路径调用该版本真实的 SequencerTools 接口。导出拒绝覆盖已有 .fbx，导入要求文件存在，操作不自动重试。

Outliner 视图模型操作和自定义绑定转换依赖该引擎提供的编辑器脚本 API，通常还要求已打开相应 Sequence；旧版无法等价映射时明确返回不支持。Sequence 文件夹与普通绑定操作仍可用。

AnimMixer 5.6/5.7 的框架不等于 5.8 的 Layer/Transition 对象模型。5.8 优先复用 SequencerAnimMixerToolset；没有相应脚本扩展的引擎不伪造 Layer 对象。旧版 Mixer 轨道的通用 Section/关键帧读写继续使用已有 Sequencer 兼容接口。原生 Mixer 同样要求宿主启用该插件，不仅依赖本机目录存在。
