# NexusLink Capability 元数据规范

> 新增或修改 Capability 时，必须全部遵循本规范。注册期有自动校验兜底，但提交前请先过自检清单。

---

## 1. 描述格式（四段式）

```
[VERB] [TARGET]. [DIFFERENTIATOR]. [CONSTRAINT?]
```

| 段 | 约束 | 说明 |
|---|---|---|
| VERB + TARGET | 必填，建议 ≤30 字符 | 动作 + 受体；**禁止重复 name 中已有 token**（注册期校验） |
| DIFFERENTIATOR | 必填，建议 ≤50 字符 | 与同前缀 cap 的关键差异（batch vs single / editor vs runtime / CDO vs instance） |
| CONSTRAINT | 按需，建议 ≤30 字符 | 危险提示 / 前置依赖（`requires PIE` / `editor graph not synced` / `validated ImportText`） |
| **总长度** | **建议 ≤100 字符** | 过长会挤占 `tools/list` / SearchMode token；**注册期不再 ensure** |

### 1.1 VERB 标准词表

| Verb | 含义 | 示例 cap |
|---|---|---|
| `Search` | 按条件过滤列表（返回 ref/摘要，不含完整数据） | `search_asset` |
| `List` | 枚举运行时存在的实例 | `list_runtime_actors` |
| `Get` | 读取完整数据 / 属性 | `get_asset_blueprint`, `get_runtime_actor_property` |
| `Set` | 写入属性（单次 / 批量） | `set_runtime_actor_property` |
| `Manage` | 结构性增删改（节点/字段/行/片段） | `manage_asset_blueprint`, `manage_asset_data_table` |
| `Create` | 新建资产文件 | `create_asset_blueprint` |
| `Delete` | 删除资产文件 | `delete_asset` |
| `Rename` | 移动/重命名资产 | `rename_asset` |
| `Duplicate` | 复制资产到新路径 | `duplicate_asset` |
| `Save` | 持久化脏标记资产 | `save_asset` |
| `Spawn` | 运行时实例化 | `spawn_runtime_actor`, `spawn_runtime_widget` |
| `Destroy` | 运行时移除实例 | `destroy_runtime_actor` |
| `Diff` | 对比两个实体的属性差异 | `diff_runtime_actors` |
| `Interact` | 触发 UI 交互（click/check/set） | `interact_runtime_widget` |
| `Control` | 控制编辑器状态 | `control_pie` |
| `Exec` | 执行命令/脚本 | `exec_command`, `eval_runtime_lua`, `dofile_runtime_lua` |
| `Inspect` | 只读查看内部结构（Lua 表/堆栈/元表） | `get_runtime_lua_stack` |
| `Capture` | 截图 | `capture_viewport` |
| `Query` | 系统级只读查询 | `get_asset_refs`, `get_gameplay_tags` |
| `Hot-reload` | 热重载模块 | `hotreload_runtime_lua` |

### 1.2 反例 → 正例对照

| Verb | 反例（旧） | 正例（新） | 问题 |
|---|---|---|---|
| Get | `"Read AnimMontage asset: slots/segments and sections."` | `"Inspect montage timeline. Slots/segments/sections snapshot; read-only, no playback."` | 与 name 重叠（"montage"/"asset"），无差异化 |
| List | `"List Actors in the current World with filters."` | `"Enumerate live Actors. class/tag/name filters; PIE/Game only, returns refs not props."` | 无区分于 get_runtime_actor_property |
| Set | `"Write properties to runtime Actors (per-update entry)."` | `"Batch-write Actor properties. One entry per target actor; PIE/Game only, immediate apply."` | "per-update entry" 含义不清 |
| Manage | `"Add/remove segments and sections in an AnimMontage asset."` | `"Edit montage structure. Add/remove slots, segments, sections; save separately."` | 与 get_asset_anim_montage 描述区分不足 |
| Search | `"Search project assets by type/path/query/name."` | 保留（描述里含路由表，属例外允许超长） | — |
| Get | `"Get the widget tree of a WidgetBlueprint."` | `"Read WidgetBlueprint tree. Widget hierarchy, bindings, slot props; editor only."` | 与 manage 区分不足 |
| Control | `"Start/stop/status the PIE session."` | 当前长度 36，符合规范，可保留 | — |

---

## 2. 元数据字段速查

### 2.1 唯一钩子（所有元数据集中填写）

子类只需 override 两个纯虚方法：

```cpp
virtual void BuildDefinition(FNexusCapabilityDefinition& Out) const override;
virtual FCapabilityResult Execute(const TSharedPtr<FJsonObject>& Arguments) const override;
```

`BuildDefinition` 中按需设置以下字段：

| 字段 | 赋值方式 | 约束 |
|---|---|---|
| `Out.Name` | 必填 | snake_case，全局唯一，`verb_noun` 命名 |
| `Out.Description` | 必填 | 四段式；建议 ≤100 字符（注册期不硬拦） |
| `Out.InputSchema` | 必填 | `FNexusSchema::Object().Prop(...).Required({...}).Build()` |
| `Out.Tags` | 必填 | 至少含 1 个访问标签（`readonly`/`write`）+ 1 个分类标签 |
| `Out.ExtraSearchKeywords` | 可选 | 口语词/同义词；禁重复 name token；注册期自动剥离 |
| `Out.RelatedCapabilities` | 可选 | cap name 列表；有强关联兄弟 cap（get/manage 成对）时填 |
| `Out.SearchAssetTypes` | 可选 | **资产 get/manage cap 必填**：与 `search_asset` 返回的 `assetType` 对齐（如 `{"Blueprint"}`）；注册期写入路由索引，供 `recommendedGet`/`recommendedManage` |
| `Out.Prerequisites` | 可选 | 枚举值（见下表）；需要特定运行环境时填 |
| `Out.WhenToUse` | 可选 | ≤40 字符；同前缀 cap ≥3 个、description 难以区分时填 |

**Multi-section cap 特别说明：**
继承 `FNexusMultiSectionCapability` 时，`Out.InputSchema` 调用 `BuildSchemaWithSections()` 代替直接构建，sections 枚举字段由基类自动注入。`BuildCapabilitySchema()` 继续声明除 sections 之外的所有入参。

### 2.2 旧字段映射（废弃，仅供历史参考）

> 下列独立虚函数已在重构中合并到 `BuildDefinition`，**新 cap 禁止使用**。

| 旧 C++ 钩子（已删除） | 对应新字段 |
|---|---|
| `GetName()` | `Out.Name` |
| `GetDescription()` | `Out.Description` |
| `BuildSchema()` | `Out.InputSchema` |
| `BuildTags(OutTags)` | `Out.Tags` |
| `GetExtraSearchKeywords()` | `Out.ExtraSearchKeywords` |
| `GetRelatedCapabilities()` | `Out.RelatedCapabilities` |
| `GetPrerequisites()` | `Out.Prerequisites` |
| `GetWhenToUse()` | `Out.WhenToUse` |

### 2.3 Tags 枚举

| 标签 | 类型 | 含义 |
|---|---|---|
| `readonly` | 访问级别 | 只读操作，安全沙箱中允许 |
| `write` | 访问级别 | 会修改数据/状态 |
| `editor` | 分类 | 通用/仅编辑器 |
| `blueprint` | 分类 | Blueprint 资产 |
| `material` | 分类 | Material / MaterialInstance 资产 |
| `struct` | 分类 | UserDefinedStruct 资产 |
| `data` | 分类 | DataTable / DataAsset |
| `widget` | 分类 | UMG / WidgetBlueprint |
| `runtime` | 分类 | 运行时（PIE/Game）操作 |
| `gas` | 分类 | GameplayAbility System（`WITH_GAS=1` 时注册） |

### 2.4 Prerequisites 枚举值

| 值 | 含义 |
|---|---|
| `pie` | 需要 PIE/Game 会话运行中 |
| `unlua` | 需要 UnLua 插件启用 |
| `editor_only` | 仅编辑器模式（非 PIE）可用 |
| `ds_mode` | 仅 Dedicated Server 模式下有意义 |

---

## 3. ExtraSearchKeywords 词典

### 3.1 全局禁用词（注册期自动剥离，写了也无效）

这些词来自 Tags 或普遍命中所有 cap，无区分价值：

`asset`, `runtime`, `editor`, `blueprint`, `widget`, `material`, `struct`, `data`, `lua`, `actor`

（凡是 name token 都会被自动剥离）

### 3.2 高优先口语词（建议在对应 cap 的 keywords 里加）

| 用户可能说 | 建议加到哪个 cap |
|---|---|
| `flag`, `variable`, `var`, `field`, `property` | `manage_asset_blueprint`, `get_asset_blueprint` |
| `node`, `pin`, `wire`, `graph`, `connection` | `manage_asset_blueprint`, `manage_asset_material` |
| `tag`, `gameplay tag`, `gtag` | `get_gameplay_tags` |
| `log`, `output`, `console` | `get_output_log`, `exec_command` |
| `screenshot`, `snap`, `capture`, `photo` | `capture_viewport` |
| `spawn`, `create actor`, `instantiate` | `spawn_runtime_actor` |
| `kill`, `remove actor`, `delete actor` | `destroy_runtime_actor` |
| `montage`, `play`, `anim slot` | `get_asset_anim_montage`, `get_runtime_actor_animation`, `interact_runtime_actor_animation` |
| `texture`, `贴图`, `image`, `tga`, `png` | `get_asset_texture` |
| `static mesh`, `sm`, `mesh asset` | `get_asset_static_mesh` |
| `anim sequence`, `clip`, `frame` | `get_asset_anim_sequence` |
| `skeletal mesh`, `skmesh` | `get_asset_skeletal_mesh`, `get_asset_skeleton` |
| `sound`, `audio`, `sfx` | `get_asset_sound_wave`, `get_asset_sound_cue` |
| `niagara`, `vfx`, `fx` | `get_asset_niagara_system` |
| `level`, `map`, `umap`, `world` | `get_asset_level` |
| `state machine`, `transition`, `anim graph` | `manage_asset_anim_blueprint`, `get_asset_anim_blueprint` |
| `blackboard`, `bb key` | `get_asset_blackboard`, `get_runtime_actor_behavior_tree` |
| `bt node`, `task`, `service`, `decorator` | `get_asset_behavior_tree`, `manage_asset_behavior_tree` |
| `pie`, `play in editor`, `simulate` | `control_pie` |
| `diff`, `compare`, `delta` | `diff_runtime_actors` |
| `ref`, `dependency`, `reference`, `referencer` | `get_asset_refs` |
| `unlua`, `hot reload`, `lua bind` | `hotreload_runtime_lua`, `get_asset_lua_binding` |
| `slider`, `button`, `text`, `image`, `checkbox` | `interact_runtime_widget`, `get_runtime_widget_property` |
| `struct field`, `struct type` | `manage_asset_struct_field`, `get_asset_struct` |
| `row`, `datatable row` | `manage_asset_data_table`, `get_asset_data_table` |
| `cdo`, `default value`, `class default` | `get_asset_blueprint` (section=defaults), `manage_asset_data_asset` |

---

## 4. 提交前自检清单（6 条）

提交或 PR review 前，对每个新增/修改的 cap 逐项确认：

- [ ] **格式**：Description 符合四段式，建议总长度 ≤ 100 字符，含至少 1 个 `.`
- [ ] **无重叠**：Description 中无 name 的主要 token（注册期 overlap ≤ 30%）
- [ ] **差异化**：同前缀 cap 的 Description 在 DIFFERENTIATOR 段有明显区分词
- [ ] **Keywords**：ExtraSearchKeywords 均为口语词/同义词，无 name 已有词
- [ ] **关联**：RelatedCapabilities 列出了强关联的兄弟 cap（get/manage 通常成对）
- [ ] **SearchAssetTypes**：资产 `get_asset_*` / `manage_asset_*` 已声明对应 `assetType`（与 `search_asset` 返回值对齐）；非资产 cap 留空
- [ ] **命名**：`Out.Name` 符合 §6 决策树；非 pattern cap 已登记 InitializeInstructions 例外表

> 注册期 `FNexusCapabilityRegistry::Register()` 仅硬校验命名动词（§6）；Description 长度与 §4 格式/重叠/关键词条数由规范自检 / `Script/audit_capability_naming.py` CI 门禁负责，避免 Dev 构建启动时 ensure 闪退。

---

## 5. 新增 Capability 完整流程

```
1. 确认 name 命名遵循 verb_noun，全局唯一
2. 实现 BuildDefinition(Out)：
   a. 按四段式写 Out.Description，对照同前缀 cap 检查 DIFFERENTIATOR
   b. Out.InputSchema = FNexusSchema::Object().Prop(...).Required({...}).Build()
      （继承 FNexusMultiSectionCapability 时用 BuildSchemaWithSections()）
   c. Out.Tags 至少填 readonly/write + 分类标签
   d. 按需填 Out.ExtraSearchKeywords / Out.RelatedCapabilities / Out.WhenToUse / Out.Prerequisites
   e. 资产 get/manage：填 Out.SearchAssetTypes（如 `{"Blueprint"}`），search_asset 据此返回 recommendedGet/Manage
3. 实现 Execute(Arguments)
4. .cpp 末尾 REGISTER_MCP_CAPABILITY(YourCapClass)
5. 编译后观察 Output Log，无 ensureMsgf 警告
6. 过提交前自检清单（§4）
7. 同步更新 `CHANGELOG.md` `[Unreleased]`
8. 同步 [`InitializeInstructions.SearchMode.md`](InitializeInstructions.SearchMode.md) / [`InitializeInstructions.MultiTool.md`](InitializeInstructions.MultiTool.md)（见 §6.6）
```

---

## 6. Capability 命名规范

> AI 路由与注册期校验的权威来源；完整例外表见 [`InitializeInstructions.SearchMode.md`](InitializeInstructions.SearchMode.md)。

### 6.1 全局格式

- `snake_case`，全局唯一，仅小写字母与下划线。
- C++：`F{PascalCase}Capability`，文件 `Nexus{PascalCase}Capability.{h,cpp}`。
- Token 顺序：`{verb}_{scope}_{target}_{aspect?}`；`scope` 为 `asset` | `runtime` 时须出现在名中；Editor / 通用 / Lua 例外见 §6.4。

### 6.2 Asset 盘（编辑器资产）

- **默认**：`{get|manage|create}_asset_{type}`（`blueprint` / `material` / `user_widget` / `anim_blueprint` / …）。
- **Manage** = 磁盘资产结构性增删改（节点、行、字段、片段）；**禁止**用于 PIE 播放、GA 施放等运行时命令。
- **通用例外（完整名）**：`search_asset`、`save_asset`、`rename_asset`、`duplicate_asset`、`delete_asset`、`unload_asset`、`get_asset_refs`、`get_asset_lua_binding`、`manage_asset_struct_field`。

### 6.3 Runtime（PIE / Game）

- **默认**：`{verb}_runtime_{target}_{aspect?}`。
- **动词决策**：
  - **Get** — 只读快照 / `sections`
  - **Set** — **仅** `*_property` + `propertyPaths` 反射写
  - **Interact** — 多 `action` 命令（`interact_runtime_widget`；蒙太奇播放为 `interact_runtime_actor_animation`）
  - **List / Spawn / Destroy / Diff** — 枚举、生成、销毁、对比
- **成对**：命令写 → `get_*` + `interact_*`；属性写 → `get_*` + `set_*`。**禁止**为对称把 montage 播放命名为 `set_runtime_actor_animation`。
- **单复数**：`list_runtime_actors`、`diff_runtime_actors` 用复数 `target`；`spawn_runtime_actor`、`destroy_runtime_actor` 用单数；存量不改名。

### 6.4 例外登记表（非 Asset/Runtime pattern）

须在 Instructions「例外」段与下表 **1:1**（共 17 + 条件 GAS 11）：

| 分类 | Capability |
|------|------------|
| Editor (8) | `capture_viewport`, `compile_blueprint`, `control_pie`, `exec_command`, `get_editor_info`, `get_gameplay_tags`, `get_output_log`, `set_log_capture_filter` |
| 通用资产 (7) | 见 §6.2 例外行 |
| Lua (13, `WITH_UNLUA`) | `eval_runtime_lua`, `dofile_runtime_lua`, `gc_runtime_lua`, `hotreload_runtime_lua`, `set_runtime_lua`, `get_runtime_lua_env`, `get_runtime_lua_value`, `get_runtime_lua_loaded`, `get_runtime_lua_stack`, `get_runtime_lua_metatable`, `get_runtime_lua_object`, `get_runtime_lua_memory`, `get_asset_lua_binding` |
| Runtime 非标 | `diff_runtime_actors`, `get_runtime_slate_widget` |
| GAS (`WITH_GAS=1`, 11) | `create/get/manage_asset_gameplay_ability`, `create/get/manage_asset_gameplay_effect`, `create/get/manage_asset_attribute_set`, `get_runtime_actor_ability_system`, `interact_runtime_actor_ability_system` |
| StateTree (`WITH_STATETREE=1`, UE 5.5+, 1) | `get_asset_state_tree` |
| MVVM (`WITH_MVVM=1`, UE 5.5+, 1) | `get_asset_view_model` |

### 6.5 禁止复活与计划缺口

| 禁止 / 缺口 | 说明 |
|-------------|------|
| `manage_animation` | v1.9 已删；用 `interact_runtime_actor_animation` |
| `set_runtime_actor_animation` | Set 动词误用 |
| `get_asset_generic` 等无 scope 旧名 | 已废弃 |

### 6.6 RelatedCapabilities 与文档同步

- 兄弟：`get`↔`manage`（资产）、`get`↔`set`（property）、`get`↔`interact`（命令）。
- **禁止**指向未注册名；`audit_capability_naming.py` 在 CI / `run_e2e` 前校验。
- 变更 cap 名或路由：§6 登记表 + **SearchMode + MultiTool** + `scripts/build_tool_reference.py` + CHANGELOG。

---

*本规范为 Capability 元数据权威文档；§6 命名速查、§8 Utils 速查。*

---

## 8. Utils 模块规范

> 新增或修改任何 `Utils/` 下的工具类时，必须遵循本节（§8 速查）。

### 7.1 分层模型

Utils 按依赖方向划分为 6 层，依赖只能从高层流向低层，**禁止反向**：

| 层 | 语义 | 文件（举例） |
|---|---|---|
| **Common** | 引擎无关 JSON/字符串/宏基础 | `NexusJsonUtils`、`NexusStringMatchUtils`、`NexusResponseCompactorUtils`、`NexusVersionCompat`（纯宏） |
| **Result** | Capability 返回壳构造 | `NexusCapabilityResultBuilder` |
| **Reflection** | UObject 反射/属性读写 | `NexusPropertyUtils`、`NexusPropertyReportUtils` |
| **Asset** | 资产加载/蓝图/图结构 | `NexusAssetUtils`、`NexusBlueprintGraphUtils` |
| **Runtime** | 运行时 World/Actor/Widget | `NexusRuntimeUtils` |
| **Domain** | 领域专用（引用面窄） | `NexusLuaUtils`、`NexusMaterialUtils`、`NexusAnimGraphUtils`、`NexusBehaviorTreeInspectUtils`、`NexusPinTypeUtils` |
| **Editor** | `WITH_EDITOR` 专属 | `NexusPortUtils`、`NexusEditorCaptureUtils`、`NexusCapabilityIndexUtils` |

**依赖规则**：Common 不得依赖任何其他层；Result/Reflection 只能依赖 Common；Asset 可依赖 Reflection/Common；Runtime 可依赖 Reflection/Common；Domain/Editor 可依赖 Common，Editor 还可依赖 Asset。

### 7.2 文件命名

- **所有** Util 类必须命名为 `FNexus<Domain>Utils`，对应文件名为 `Nexus<Domain>Utils.h/.cpp`。
- **唯一例外**：纯宏文件（如 `NexusVersionCompat.h`，无类定义），保持原样。
- 无单职责豁免、无短名豁免。新增文件**必须**带 `Utils` 后缀。

### 7.3 类与 API 风格

```cpp
// Copyright byteyang. All Rights Reserved.

/** 中文类级 JavaDoc */
class NEXUSLINK_API FNexus<Domain>Utils final
{
public:
    FNexus<Domain>Utils() = delete;

    /** 中文方法 JavaDoc */
    static ReturnType MethodName(Params...);
};
```

约束：
- `final` 类，构造 `= delete`，全部 `public: static`，禁止状态成员。
- 公开类**必须** `NEXUSLINK_API`。
- 公开方法**必须**中文 JavaDoc（`/** ... */`）。
- 私有 helper 写在 `.cpp` 内 `namespace { }` 匿名块。

### 7.4 准入门槛

| 情形 | 处置 |
|---|---|
| 被 ≥2 个 Capability/Tool 引用的 helper | **强制**下沉到对应层 Utils |
| 仅 1 处引用，但函数体 >30 行 **或** 聚合 ≥3 个 helper | 可建领域 Utils（§7.4 豁免），须加注释 `// §7.4 豁免：单处引用但逻辑复杂` |
| 其余单处引用 | 保留在 Capability 文件内 `namespace { static ... }` |

### 7.5 跨版本兼容（权威详版）

> `audit_capability_naming.py` 会拒绝 **NexusLink 全模块**（除 `NexusVersionCompat.h`）出现 `NX_UE_AT_LEAST(`。

**三层分工**

| 层 | 位置 | 规则 |
|---|---|---|
| 语义宏 | `NexusVersionCompat.h` | 仅此处可写 `NX_UE_AT_LEAST(M,N)`；对外只用 `NX_UE_HAS_<语义>`，注释格式：`// 旧API → 新API` |
| 实现 | `FNexus*Utils` | 所有 `#if NX_UE_HAS_*` 多行分支；同一差异不得复制到多个 Capability |
| Capability | `Capabilities/**` | 禁止 `NX_UE_AT_LEAST`；禁止 ≥3 段版本 `#elif` 梯；只调用 Utils 或单行 `NX_UE_HAS_*` |
| Editor/Server 等 | `Private/Editor` 等 | 禁止裸 `NX_UE_AT_LEAST`（例：`NX_UE_HAS_APP_STYLE` 替代 FEditorStyle/FAppStyle） |

**新增 API 分叉（4 步）**

1. 查 `NexusVersionCompat.h` 是否已有可复用宏（含别名，如 `NX_UE_HAS_SKELETAL_MESH_ACCESSORS`）。
2. 无则新增 `NX_UE_HAS_*` + 中文注释（勿在 Capability 写裸版本号）。
3. 逻辑下沉 Utils（≥2 调用点或单点 ≥3 行 `#if`）；资产只读字段优先 `FNexusAssetUtils`。
4. `build_test`：`UE_4.26` 必过；触及引擎 API 时加 `UE_5.0`、`UE_5.6`；`audit_capability_naming.py` PASS。

**可选插件**：`WITH_GAS` / `WITH_NIAGARA` / `WITH_UNLUA` / `WITH_STATETREE` / `WITH_MVVM` 整 `.cpp` 文件守卫 + `Build.cs` 探测；文件内仍禁止裸 `NX_UE_AT_LEAST`。

**已登记宏（节选，完整列表以 `NexusVersionCompat.h` 为准）**

| 宏 | 含义 |
|---|---|
| `NX_UE_HAS_TEXTURE_*` | 贴图 PlatformData / GetSurface* |
| `NX_UE_HAS_STATIC_MESH_ACCESSORS` | StaticMesh 材质槽 / BodySetup getter |
| `NX_UE_HAS_SKELETAL_MESH_*` | SkeletalMesh 材质 / Physics / Skeleton getter |
| `NX_UE_HAS_ANIM_SEQUENCE_DATA_MODEL` | AnimSequence 帧数/帧率走 DataModel（5.6+） |
| `NX_UE_HAS_ANIM_SEQUENCE_LOOP_FIELD` | AnimSequence::bLoop（5.1+） |
| `NX_UE_HAS_SOUND_NODE_WAVE_ACCESSOR` | SoundNodeWavePlayer::GetSoundWave |
| `NX_UE_HAS_NIAGARA_*` | Niagara 发射器枚举 / ExposedParameters API |
| `NX_UE_HAS_APP_STYLE` | 编辑器 Slate：`FEditorStyle` → `FAppStyle` |
| `NX_UE_HAS_UMG_SLOT_GETTERS` | UMG PanelSlot 布局 getter（Canvas/Box/Grid，5.1+） |
| `NX_UE_HAS_SLATE_BOX_PANEL_SLOT_GETTERS` | Slate BoxPanel 子 Slot 导出（GetSlot + slot getter，5.0+） |
| `NX_UE_HAS_CONTENT_BROWSER_ITEM_PATH` | Content Browser：`GetCurrentPath()` → `FContentBrowserItemPath`（UE4 用无参 `FString`） |
| `NX_UE_HAS_ASSET_SOFT_OBJECT_PATH` | `FAssetData::GetSoftObjectPath()`（UE4 用 `ObjectPath`） |
| `NX_UE_HAS_TAG_SEARCHABLE_REFERENCERS` | AssetRegistry `GetReferencers(SearchableName)` 按 GameplayTag 查引用包 |
| `NX_UE_HAS_K2_PIN_REAL` | 蓝图 Pin `PC_Real`（UE4 仅 `PC_Float`） |

### 7.6 `#if` 守卫规则

- `WITH_EDITOR` / `WITH_UNLUA` / `NX_UE_HAS_*` 只包裹**必要分支**，禁止在头文件里把整个类体包进守卫。
- Editor 专属 Utils 文件名须体现（`NexusEditor*Utils` / `NexusPort*Utils`）。

### 7.7 依赖单向性（硬限制）

- Utils 禁止 `#include` 任何 `Capabilities/` 或 `Tools/` 目录的头。
- Common 层禁止依赖本表其他任何层。

### 7.8 通用接口约定（禁止重复）

新增 Capability 时，以下逻辑**必须**走 Utils，禁止在 Capability 里自行抄写：

| 职责 | 统一接口 |
|---|---|
| offset / limit 分页切片 | `FNexusJsonUtils::ComputeSlice(Total, Offset, Limit, OutStart, OutEnd)` |
| nameFilter / propertyPaths / sections 解析 | `FNexusJsonUtils::Parse*` 系列 |
| 反射字段报告（DataTable/DataAsset/Struct 样式） | `FNexusPropertyReportUtils::BuildFieldReport(...)` |
| Capability 返回壳 / entry 错误 | `FNexusCapabilityResultBuilder::AddEntry(...)` / `AddEntryError(...)`；适配层 `AssembleStructuredContent`：`Entries.Num()==1` 提升到顶层，`Num()>1` 写 `results[]`；响应身份字段统一 `path`，`get_`/`manage_` 可 `StripRedundantPathEcho` |
| 取运行时 World 并做错误兜底 | `FNexusRuntimeUtils::RequirePlayWorld(OutError)` |
| 新建资产 finalize（MarkDirty + AssetCreated + Save） | `FNexusAssetUtils::NotifyAndSaveCreated(...)` / `NotifyCompileAndSave(...)` |

### 7.9 Capability 实现样板约束

所有 Capability `Execute()` **必须**遵守：

- 禁止裸写 `FCapabilityResult _R` + lambda + `_R.Entries` 样板——改用 `FNexusCapabilityResultBuilder::AddEntry(...)`。
- entry 级错误禁止直接 `SetStringField("error", ...)`——改用 `AddEntryError(OutEntries, ErrCode, Msg)`。
- 运行时 World 获取禁止手写 null 检查链——改用 `FNexusRuntimeUtils::RequirePlayWorld(OutError)`。
- 新建资产 finalize 禁止手写 `MarkPackageDirty` + `AssetCreated` + Save 五件套——改用对应 `FNexusAssetUtils` 接口。

### 7.10 提交前自检（6 条）

1. 类名/文件名符合 §7.2（`FNexus<Domain>Utils`，无例外时）
2. API 全 `static`，公开类有 `NEXUSLINK_API`，公开方法有中文 JavaDoc
3. `#include` 不违反 §7.7 依赖箭头
4. 未绕过 `NX_*` 宏（§7.5）
5. 未引入新的重复 helper（应改走 §7.8 对应接口）
6. Capability 实现未出现 §7.9 列出的禁止样板

### 7.11 Manage / Create / Execute 契约（单资产多操作）

批量范围仅限**单资产多操作**；跨 `assetPaths[]` 的多资产批量不在本契约范围内（保持各 cap 现状）。

| 项 | 约定 |
|---|---|
| manage 命令列表 | Schema 只暴露 `operations: [{action, ...}]`（每项含 `action`），禁止再暴露 `ops` 或顶层裸 `action` |
| Execute 读入 | 统一 `FNexusJsonUtils::ExtractOperations(Args)`：优先 `operations` → 回退 `ops`（旧字段过渡期兼容）→ 回退顶层 `action`+其余字段合成单元素数组（旧单操作 manage 过渡期兼容，不写进 Schema） |
| 结果信封 | 禁止「一条 Entry + 内嵌 `results[]`」的双层包裹；每个 op 必须对应一条独立 `OutEntries.Add(...)`，交由适配层 `AssembleStructuredContent` 统一提升/包装 |
| `success` 字段 | 成功不写 `success`（无 `error` 即成功）；失败写 `error`，允许保留 `success:false` 辅助阅读；禁止写「成功恒为 true」或条件判断结果恒为 true 的 `success` |
| create 入参 | 统一暴露 `assetPath`；历史 `packagePath`+`assetName` 双字段 cap 短期可继续读入旧字段做兼容，但 Schema 优先 `assetPath` |
| create 响应 | 成功条目必须含 `path` 字段 |
| Execute 卫生 | 非 MultiSection 的 cap 优先 `FNexusCapabilityResultBuilder::Build`；资产定位统一 `RequireString` + `EmitError`（或对应 Fatal/`MakeArgInvalid`）；禁止裸 `SetStringField("error")` 作为唯一失败路径 |
| 有意保留（不受本节约束） | 领域数组 `keys`/`rows`/`fields`/`widgets`；runtime `interact_*` / `control_pie` 的顶层 `action`（命令式语义，非批量操作列表）；元工具 `calls[]` |
