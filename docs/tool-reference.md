# NexusLink 工具参考手册

本文档列出 NexusLink暴露的全部 MCP 工具与 Capability 的详细参数说明。

> **Tool 与 Capability 的区别**
> - **MCP Tool（3 个）**：`search_capabilities`、`call_capability`、`submit_feedback`。由 AI 直接通过 `tools/call` 调用。
> - **Capability（其余所有）**：通过 `call_capability` 调用：单条 `capability` + `arguments`；或批量 `calls=[{capability,arguments?},...]`。本文档中除元工具节外的所有 `###` 小节均为 Capability，其标题即 `capability` 字段值。
>
> 示例（单条）：`call_capability(capability="list_runtime_actors", arguments={"classFilter":"BP_Enemy"})`

> **`search_capabilities` 失败区分**（看 `errorKind`，勿混读单一 `error` 文案）
> - `not_found`：注册表中无此 Capability 名
> - `disabled`：`capabilityName` / 与 cap 名相同的 `query` 精确查询时，该 cap 已在设置中禁用
> - `disabled_only`：模糊 `query` 无已启用命中，但存在名称匹配的**已禁用** cap（见 `disabledCapabilities[]`）

> **通用约定**
> - 所有 `assetPath` 均为 UE 内容路径，如 `/Game/Blueprints/BP_Player`
> - 属性路径（`propertyPath`）支持 `A.B.C` 点号钻取 + 容器下标 `[i]` / `["key"]`：
>     - 数组：`Items[0]`、`Matrix[0][1]`
>     - Map：`Users["alice"].Score`（Key 与 `ExportText` 结果等值比较；字符串键剥引号）
>     - Set：`Tags["Player.Ally"]`
>     - 组合：`MyComp.RelativeLocation.X`、`Users["alice"].Modules[2].Name`
>     - 路径分段按 `.` 切，Map 的字符串键**不能包含 `.`**
> - 过滤参数支持四种匹配模式：子串（`Player`）、前缀（`^BP_`）、后缀（`Actor$`）、正则（`/^BP_.+$/`）
> - 列表工具均支持 `offset`（默认 0）和 `limit`（默认 100，上限 500）分页
> - 标记 ★ 的参数为必填
> - **批量参数单/复数容错**：所有暴露复数参数（`assetPaths` / `actorNames` / `widgetNames` / `rowNames` / `propertyPaths` / `variables` / `components` / `fields` / `widgets` / `nodes` / `wires` / `operations` / `rows` / `updates` / `spawns` 等）的工具，运行时允许调用方传对应的单数键（`assetPath` / `actorName` / `variable` / `update` ...），会被自动包装成单元素数组继续执行。Schema 仍只宣传复数形态，仅作运行时容错兜底，不要依赖单数形态做新功能设计
> - **响应默认值压缩（全工具默认启用）**：`NexusMcpDispatcher` 在每次工具执行后、序列化前对 `structuredContent` 做一次**递归自动扫描**——遍历所有嵌套层级里的"对象数组"字段 `K`，对其中每个标量字段做分布统计，主流值满足三阈值（`MinCount=3` / `MinMatchRatio=0.7` / `MinNetSaveBytes=30`）时抽到同级 `<K>_defaults`，条目里等于该默认值的同名字段随即省略。内置身份字段排除集（`name` / `path` / `assetPath` / `nodeId` / `tag` / `message` / `timestamp` / `frame` / `id` / `label` / `title` / `text` / `error`）永远不会进入 defaults。
>
>   **消费侧合并规则**：遍历条目时先拿 `<K>_defaults` 作为底，再用条目自身字段覆写（`{**defaults, **entry}`）；条目缺少某字段时视为等于 defaults 值。
>
>   **跳过规则**：同级已出现 `<K>_defaults` 时不二次处理；字段名以 `_defaults` 结尾或等于 `content` 时不参与（协议字段语义冲突）。
>
>   可通过 编辑器 → 编辑器首选项 → 插件 → NexusLink → `响应默认值压缩` 关闭，关闭后工具返回未压缩的原始条目。

---

<!-- 自动生成，由 build_tool_reference.py 产出；以下内容请勿手工修改 -->
<!-- 共 175 个 Capability + 3 个元工具 -->

## 目录

- [元工具（Meta）](#元工具-meta)
- [编辑器工具（Editor）](#编辑器工具-editor)
- [通用资产工具](#通用资产工具)
- [蓝图工具](#蓝图工具)
- [动画资产工具](#动画资产工具)
- [材质工具（Material）](#材质工具-material)
- [结构体工具（Struct）](#结构体工具-struct)
- [数据资产工具（DataAsset / DataTable）](#数据资产工具-dataasset-datatable)
- [控件蓝图工具（Widget）](#控件蓝图工具-widget)
- [Lua 运行时工具](#lua-运行时工具)
- [运行时工具（Runtime）](#运行时工具-runtime)
- [AI 工具](#ai-工具)

---

## 元工具（Meta）

### `call_capability`

执行 Capability（在 search_asset / get_asset_* 之后）。失败看 errorKind：unknown/disabled/arg_invalid；disabled 勿重试。旧名（如 create_blackboard）自动映射规范名。批量 calls[] 与单条不可混用。单条成功时字段在顶层（无 `results[{...}]`）；多条仍为 `results[]`。响应身份字段为 `path`（入参仍 `assetPath`）；`get_`/`manage_` 等价回显可省略。

---

### `search_capabilities`

**首要入口** — 回答任何蓝图/Widget/材质/资产问题前应先调用。已知名称优先 `capabilityName=<精确名>`；`query` 用 1-2 词 AND 匹配。失败看 `errorKind`：`not_found`（不存在）/ `disabled`（设置已禁用）/ `disabled_only`（仅禁用 cap 命中，见 `disabledCapabilities[]`）；`query=get_asset` 零命中时 `hint` 会指向 `get_asset_<类型>` 路由。匹配 ≤2 返回完整 `parameters[]`。

---

### `submit_feedback`

上报 Capability/工具的使用摩擦，帮助改进搜索和 Schema。触发时机：重试 ≥2 次仍无进展、找不到合适的 Capability、Schema 字段含义需要猜测、被迫串行调用 ≥3 次。`category` 可取：`wrong_tool` / `misuse` / `schema_guess` / `search_zero` / `search_overflow` / `other`。优先填结构化字段（`attemptedArgs`、`actualError`、`expectedField`），少写长 `note`。

---

## 编辑器工具（Editor）

### `capture_viewport`

截图编辑器面板、PIE 视口或指定的 Actor/UMG Widget。`validateOnly=true` 不写图片，仅验通路。

**适用场景**：截图编辑器/PIE/Actor/Widget；非 editor_desktop 勿滥用

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `target` | `string` |  | editor|editor_desktop|viewport|pie|<panel>|list |
| `format` | `string (enum)` |  | 图片格式 枚举值：`png` / `jpg` / `png` |
| `maxSize` | `integer` |  | 最大边长像素（0=原生） |
| `actorName` | `string` |  | Actor 名/标签；裁剪到屏幕包围盒 |
| `widgetName` | `string` |  | 运行时 UMG Widget；target=pie |
| `ownerClass` | `string` |  | UserWidget 类过滤 |
| `viewAngle` | `string (enum)` |  | Actor 裁剪相机角度 枚举值：`front` / `back` / `left` / `right` / `top` / `bottom` / `front` |
| `windowIndex` | `integer` |  | 顶层窗口索引（0=主窗口） |

**相关 Capability**：`list_runtime_widgets`、`list_runtime_actors`

---

### `control_pie`

启动、停止或查询 PIE 状态。action 可取：`start` / `stop` / `status`；mode 可取：`viewport` / `simulate`。

**适用场景**：启动/停止/查询 PIE；action=start|stop|status

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | PIE 操作 枚举值：`start` / `stop` / `status` |
| `mode` | `string (enum)` |  | 播放模式（仅 start） 枚举值：`viewport` / `simulate` / `viewport` |

**相关 Capability**：`exec_command`

---

### `exec_command`

执行 UE 控制台命令并捕获输出。`silent=true` 可跳过捕获；支持所有 World。

**适用场景**：执行 UE 控制台命令并返回 output；LogEngine 走 GLog，其余走 LogConsole

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `command` | `string` | ★ | 要执行的控制台命令 |

**相关 Capability**：`get_output_log`

---

### `get_editor_context`

只读编辑器上下文：选中 Actor/资产、Content Browser 路径；`sections` 可选 selection_actors/selection_assets/content_browser_path；editor World ≠ PIE。

**前置条件**：`editor_only`

**适用场景**：读编辑器选中与 Content Browser 路径

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`selection_actors` / `selection_assets` / `content_browser_path` |
| `limit` | `integer` |  | 列表最大条数（selection 段） |

**相关 Capability**：`get_editor_info`、`capture_viewport`、`search_asset`

---

### `get_editor_info`

返回 UE 版本、项目名、平台和构建配置。无参数；响应快，随时可用。

**适用场景**：读 UE 版本、项目名、平台与构建配置；无参数

**相关 Capability**：`get_output_log`

---

### `get_gameplay_tags`

检查 GAS 标签树或指定 Actor/资产的标签容器。sections 可选：`hierarchy`（标签树）/ `actor`（Actor 标签）/ `asset`（资产标签）。

**适用场景**：查 Tag 树/Actor/资产/referencers；sections 含 referencers

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`hierarchy` / `actor` / `asset` / `referencers` |
| `parentTag` | `string` |  | 层级子树的根标签 |
| `actorName` | `string` |  | 运行时 Actor 名（actor 段） |
| `assetPath` | `string` |  | 资产路径（asset 段） |
| `tag` | `string` |  | referencers 段：GameplayTag 全名 |
| `nameFilter` | `string` |  | 标签名过滤 |
| `offset` | `integer` |  | referencers 段分页偏移 |
| `limit` | `integer` |  | 最大条数 |

**相关 Capability**：`get_runtime_actor_property`

---

### `get_output_log`

读取 UE 控制台缓冲区。支持按 `category` / `verbosity` / `text` 过滤；`offset`+`limit` 分页。

**适用场景**：读 UE 输出日志；非 LogConsole（exec_command 负责）；category/verbosity/text 过滤

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |
| `categoryFilter` | `string` |  | 日志分类子串（不区分大小写） |
| `verbosity` | `string (enum)` |  | 最低详细级别 枚举值：`error` / `warning` / `display` / `log` / `verbose` / `veryverbose` / `all` / `log` |
| `textFilter` | `string` |  | 单文本子串过滤 |
| `textFilters` | `string` |  | 文本过滤（OR）；覆盖 textFilter |

**相关 Capability**：`set_log_capture_filter`、`exec_command`

---

### `search_console_variables`

按子串搜索控制台变量名（只读，含当前值）；不修改 CVar。

**适用场景**：搜索控制台变量名（只读）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `query` | `string` | ★ | 变量名子串（必填） |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |

**相关 Capability**：`exec_command`

---

### `set_log_capture_filter`

配置哪些日志分类写入缓冲区。传空数组表示全部；影响 `get_output_log` 的查询范围。

**适用场景**：设置写入缓冲的日志类别；空=全部；影响 get_output_log

**相关 Capability**：`get_output_log`

---

## 通用资产工具

### `create_asset_attribute_set`

创建 AttributeSet BP；默认值用 `manage_asset_attribute_set`，属性变量用 `manage_asset_blueprint`。

**适用场景**：创建空白 AttributeSet BP

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新 AS Blueprint 包路径，如 '/Game/GAS/AS_Hero' |
| `parentClass` | `string` |  | 父类名（默认 AttributeSet） |

**相关 Capability**：`get_asset_attribute_set`、`manage_asset_attribute_set`、`manage_asset_blueprint`

---

### `create_asset_control_rig`

创建空白 ControlRig Blueprint；用 manage 添加骨骼/控件。

**适用场景**：创建空白 ControlRig Blueprint；UE5.0+ 专用

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产路径（包路径） |

**相关 Capability**：`get_asset_control_rig`、`manage_asset_control_rig`

---

### `create_asset_curve`

创建曲线资产：CurveFloat / CurveVector / CurveLinearColor / CurveTable。

**适用场景**：创建空白曲线资产；用 manage 写入关键帧

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径 |
| `curveType` | `string` |  | float（默认）/ vector / linear_color / curve_table |

**相关 Capability**：`get_asset_curve`、`manage_asset_curve`

---

### `create_asset_data_layer`

创建 DataLayer 资产（UDataLayerAsset，≥UE5.1）。type: Runtime 或 Editor。读用 get_asset_data_layer。

**适用场景**：新建 World Partition DataLayer 资产（≥UE5.1），设置 Runtime/Editor 类型及调试颜色

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `packagePath` | `string` | ★ | 资产包路径，如 /Game/WorldData |
| `assetName` | `string` | ★ | 资产名称 |
| `type` | `string` |  | Runtime 或 Editor（默认 Runtime） |
| `debugColor` | `string` |  | 调试颜色（十六进制 #RRGGBB 或颜色名，可选） |

**相关 Capability**：`get_asset_data_layer`、`manage_asset_data_layer`、`search_asset`

---

### `create_asset_enum`

创建 UserDefinedEnum（蓝图枚举）资产；用 manage 增删枚举项。

**适用场景**：创建新的蓝图枚举资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 枚举资产包路径 |

**相关 Capability**：`get_asset_enum`、`manage_asset_enum`

---

### `create_asset_gameplay_ability`

创建 GameplayAbility BP；语义字段用 `manage_asset_gameplay_ability`，Graph 用 `manage_asset_blueprint`。

**适用场景**：创建空白 GA BP

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新 GA Blueprint 包路径，如 '/Game/GAS/GA_Jump' |
| `parentClass` | `string` |  | 父类名（默认 GameplayAbility） |

**相关 Capability**：`get_asset_gameplay_ability`、`manage_asset_gameplay_ability`、`manage_asset_blueprint`

---

### `create_asset_gameplay_effect`

创建 GameplayEffect BP；修改 Duration/Modifier/Tag 用 `manage_asset_gameplay_effect`。

**适用场景**：创建空白 GE BP

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新 GE Blueprint 包路径，如 '/Game/GAS/GE_Damage' |
| `parentClass` | `string` |  | 父类名（默认 GameplayEffect） |

**相关 Capability**：`get_asset_gameplay_effect`、`manage_asset_gameplay_effect`

---

### `create_asset_ik_rig`

创建空白 IKRig 资产；可选关联预览 SkeletalMesh。

**适用场景**：创建空白 IKRig 定义；UE5.0+ 专用

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产路径（包路径） |
| `meshPath` | `string` |  | 可选：预览 SkeletalMesh 路径 |

**相关 Capability**：`get_asset_ik_rig`、`manage_asset_ik_rig`、`get_asset_ik_retargeter`

---

### `create_asset_input_action`

创建空白 UInputAction。指定 valueType 后可用 manage 添加 Trigger/Modifier。

**适用场景**：新建 InputAction 资产；之后用 manage 配置 Trigger/Modifier

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径（/Game/…/IA_Jump） |

**相关 Capability**：`get_asset_input_action`、`manage_asset_input_action`、`create_asset_input_mapping_context`

---

### `create_asset_input_mapping_context`

创建空白 UInputMappingContext。用 manage 绑定 Action 与按键。

**适用场景**：新建 InputMappingContext；之后用 manage 添加 Action-Key 绑定

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径（/Game/…/IMC_Default） |

**相关 Capability**：`get_asset_input_mapping_context`、`manage_asset_input_mapping_context`、`create_asset_input_action`

---

### `create_asset_meta_sound`

创建 MetaSound Source 资产。读用 get_asset_meta_sound。

**适用场景**：新建 MetaSound Source 资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `packagePath` | `string` | ★ | 资产所在包路径，如 /Game/Audio |
| `assetName` | `string` | ★ | 资产名称 |

**相关 Capability**：`get_asset_meta_sound`、`manage_asset_meta_sound`、`search_asset`

---

### `create_asset_meta_sound_patch`

创建 MetaSound Patch 资产（可复用子图，≥UE5.1）。读用 get_asset_meta_sound。

**适用场景**：新建可复用 MetaSound Patch 子图资产（≥UE5.1）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `packagePath` | `string` | ★ | 资产所在包路径，如 /Game/Audio |
| `assetName` | `string` | ★ | 资产名称 |

**相关 Capability**：`get_asset_meta_sound`、`manage_asset_meta_sound`、`search_asset`

---

### `create_asset_pcg_graph`

创建 PCG Graph 资产。读用 get_asset_pcg_graph。

**适用场景**：新建 PCG Graph 资产（UE 5.4+）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `packagePath` | `string` | ★ | 资产所在包路径，如 /Game/PCG |
| `assetName` | `string` | ★ | 资产名称 |

**相关 Capability**：`get_asset_pcg_graph`、`manage_asset_pcg_graph`、`search_asset`

---

### `create_asset_render_target`

创建 TextureRenderTarget2D 资产；用 manage 修改尺寸/格式。

**适用场景**：创建渲染目标纹理资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径 |
| `sizeX` | `integer` |  | 宽度（默认256） |
| `sizeY` | `integer` |  | 高度（默认256） |

**相关 Capability**：`get_asset_render_target`、`manage_asset_render_target`

---

### `create_asset_sound_attenuation`

创建 SoundAttenuation 资产（声音衰减曲线/形状设置）。

**适用场景**：创建声音衰减设置资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundAttenuation 包路径 |

**相关 Capability**：`get_asset_sound_attenuation`、`manage_asset_sound_attenuation`

---

### `create_asset_sound_class`

创建 SoundClass 资产（音量/音高层级管理节点）。

**适用场景**：创建 SoundClass 层级节点

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundClass 包路径 |

**相关 Capability**：`get_asset_sound_class`、`manage_asset_sound_class`

---

### `create_asset_sound_concurrency`

创建 SoundConcurrency 资产（最大并发实例数限制）。

**适用场景**：创建声音并发限制资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundConcurrency 包路径 |
| `maxCount` | `integer` |  | 最大并发实例数（默认16） |

**相关 Capability**：`get_asset_sound_concurrency`、`manage_asset_sound_concurrency`

---

### `delete_asset`

永久删除单个资产包。尽力清理重定向器；仅限编辑器，操作不可逆。

**适用场景**：永久删除编辑器资产；不可逆，慎用特定包内批量

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产路径，如 '/Game/BP/BP_MyActor' |

**相关 Capability**：`save_asset`、`rename_asset`

---

### `duplicate_asset`

将编辑器资产复制到新路径。支持任意资产类型；源资产保持不变。

**适用场景**：复制编辑器资产到新路径；源资产不变

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 源资产路径 |
| `newPath` | `string` | ★ | 新完整资产路径（包路径 + 资产名） |

**相关 Capability**：`rename_asset`、`delete_asset`

---

### `export_asset`

将编辑器资产导出到磁盘文件（Fbx/Stl 等，依资产类型与 UE 导出器）。

**前置条件**：`editor_only`

**适用场景**：导出资产到磁盘文件

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产路径 |
| `outputPath` | `string` |  | 导出目标文件路径（含扩展名；留空自动生成到 Saved/Exported/） |

**相关 Capability**：`search_asset`、`duplicate_asset`

---

### `get_asset_attribute_set`

读 AttributeSet CDO 全部 `FGameplayAttributeData` 默认值；只读。

**适用场景**：读 AttributeSet CDO 默认值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AttributeSet Blueprint 路径 |

**相关 Capability**：`manage_asset_attribute_set`、`create_asset_attribute_set`

---

### `get_asset_control_rig`

读取 ControlRig Blueprint 层级（骨骼/控件/Null）与 RigVM 图（节点/引脚/连线）。写用 manage_asset_control_rig。

**适用场景**：读取 ControlRig 层级元素与 RigVM 图节点/连线；写用 manage_asset_control_rig

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | ControlRig Blueprint 资产路径 |

**相关 Capability**：`manage_asset_control_rig`、`create_asset_control_rig`、`get_asset_skeleton`

---

### `get_asset_curve`

读取曲线资产（CurveFloat/Vector/LinearColor/CurveTable）的通道与关键帧。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径 |

**相关 Capability**：`create_asset_curve`、`manage_asset_curve`

---

### `get_asset_data_layer`

读取 DataLayer 资产属性：类型（Runtime/Editor）、调试颜色（≥UE5.1）。写用 manage_asset_data_layer。

**适用场景**：读取 DataLayerAsset 的类型与调试颜色（≥UE5.1）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | DataLayerAsset 路径 |
| `assetPaths` | `string` |  | 批量路径 |

**相关 Capability**：`manage_asset_data_layer`、`create_asset_data_layer`、`search_asset`

---

### `get_asset_enum`

读取 UserDefinedEnum 的枚举项（name/displayName/value）。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 枚举资产包路径 |

**相关 Capability**：`create_asset_enum`、`manage_asset_enum`

---

### `get_asset_gameplay_ability`

读 GA Blueprint CDO：`sections=metadata|tags|costs|graphOverview`；Graph 详情用 `get_asset_blueprint`。

**适用场景**：读 GA CDO 元数据/Tag/消耗

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`metadata` / `tags` / `costs` / `graphOverview` |
| `assetPath` | `string` | ★ | GameplayAbility Blueprint 路径 |

**相关 Capability**：`manage_asset_gameplay_ability`、`manage_asset_blueprint`、`create_asset_gameplay_ability`

---

### `get_asset_gameplay_effect`

读 GE Blueprint CDO：`sections=policy|modifiers|tags|cues`；只读。

**适用场景**：读 GE CDO 策略/Modifier/Tag

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`policy` / `modifiers` / `tags` / `cues` |
| `assetPath` | `string` | ★ | GameplayEffect Blueprint 路径 |

**相关 Capability**：`manage_asset_gameplay_effect`、`create_asset_gameplay_effect`

---

### `get_asset_ik_retargeter`

读取 IKRetargeter：源/目标 IKRig、Chain Mapping 列表。写用 manage_asset_ik_retargeter。

**适用场景**：读取 IKRetargeter 配置；写用 manage_asset_ik_retargeter

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | IKRetargeter 资产路径 |

**相关 Capability**：`manage_asset_ik_retargeter`、`get_asset_ik_rig`

---

### `get_asset_ik_rig`

读取 IKRig 资产：预览网格/Solver 列表/BoneChain 列表。写用 manage_asset_ik_rig。

**适用场景**：读取 IKRig 结构概览；写用 manage_asset_ik_rig

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | IKRig 资产路径 |

**相关 Capability**：`manage_asset_ik_rig`、`create_asset_ik_rig`、`get_asset_ik_retargeter`

---

### `get_asset_input_action`

读取 InputAction 配置：ValueType/Trigger/Modifier/标志位。UE5+。

**适用场景**：读 InputAction 的 ValueType、Trigger/Modifier 类名列表、标志位

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | InputAction 资产路径 |

**相关 Capability**：`manage_asset_input_action`、`create_asset_input_action`、`get_asset_input_mapping_context`

---

### `get_asset_input_mapping_context`

列举 InputMappingContext 全部 Action-Key 绑定及其 Trigger/Modifier 数量。UE5+。

**适用场景**：读 IMC 的全部 Action-Key 绑定列表

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | InputMappingContext 资产路径 |

**相关 Capability**：`manage_asset_input_mapping_context`、`create_asset_input_mapping_context`、`get_asset_input_action`

---

### `get_asset_level`

检查磁盘关卡（UWorld 包）Actor 列表与 WorldSettings；`editor_only`。

**前置条件**：`editor_only`

**适用场景**：读磁盘关卡布局；PIE 用 list_runtime_actors

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`actors` / `settings` |
| `assetPath` | `string` | ★ | 关卡资产路径（/Game/.../*.umap 包路径） |
| `classFilter` | `string` |  | Actor 类名过滤（可选） |
| `nameFilter` | `string` |  | Actor 名/标签过滤（可选） |
| `tagFilter` | `string` |  | Actor Tag 精确匹配（可选） |
| `offset` | `integer` |  | actors 段分页偏移 |
| `limit` | `integer` |  | actors 段每页条数 |

**相关 Capability**：`manage_asset_level`、`search_asset`、`list_runtime_actors`、`get_asset_refs`

---

### `get_asset_level_sequence`

读取 LevelSequence 的时长/帧率、Binding 列表与 Track 类型概览。

**适用场景**：读 LevelSequence 的 Binding/Track 列表、时长、帧率

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | LevelSequence 资产路径 |

**相关 Capability**：`manage_asset_level_sequence`、`search_asset`、`save_asset`

---

### `get_asset_meta_sound`

读取 MetaSound Source / MetaSound Patch：inputs/outputs/节点摘要（≥5.1 支持 Patch）。写用 manage_asset_meta_sound。

**适用场景**：读取 MetaSound Source 或 Patch 的 inputs/outputs/节点；写用 manage_asset_meta_sound

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | MetaSound Source 或 Patch 资产路径 |
| `assetPaths` | `string` |  | 多个路径（批量） |

**相关 Capability**：`manage_asset_meta_sound`、`create_asset_meta_sound`、`create_asset_meta_sound_patch`、`search_asset`

---

### `get_asset_niagara_system`

检查 NiagaraSystem 发射器与用户参数；只读（需 `WITH_NIAGARA`）。

**适用场景**：读 Niagara 系统元数据；不编辑节点图

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | NiagaraSystem 资产路径 |
| `assetPaths` | `string` |  | 多个 NiagaraSystem 路径（批量） |

**相关 Capability**：`manage_asset_niagara_system`、`search_asset`、`get_asset_refs`、`save_asset`

---

### `get_asset_pcg_graph`

读取 PCG Graph 节点列表及 pin 概览。写用 manage_asset_pcg_graph。

**适用场景**：读取 PCG Graph 节点结构；写用 manage_asset_pcg_graph

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PCG Graph 资产路径 |
| `assetPaths` | `string` |  | 多个路径（批量） |

**相关 Capability**：`manage_asset_pcg_graph`、`create_asset_pcg_graph`、`search_asset`

---

### `get_asset_physical_material`

读取 PhysicalMaterial：摩擦/弹性/密度/表面类型。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PhysicalMaterial 资产路径 |

**相关 Capability**：`manage_asset_physical_material`

---

### `get_asset_physics_asset`

列举 PhysicsAsset 的 Body（骨骼/碰撞形状）和 Constraint（约束关节）概览。

**适用场景**：读 PhysicsAsset 的 Body 骨骼名/碰撞形状数量与 Constraint 列表

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PhysicsAsset 资产路径 |

**相关 Capability**：`manage_asset_physics_asset`、`get_asset_skeletal_mesh`、`save_asset`

---

### `get_asset_pose_search`

读取 PoseSearchDatabase 或 Schema 概览。写用 manage_asset_pose_search。

**适用场景**：读取 PoseSearch 数据库 schema 及动画资产数量；写用 manage_asset_pose_search

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PoseSearchDatabase 或 Schema 资产路径 |
| `assetPaths` | `string` |  | 多个路径（批量） |

**相关 Capability**：`manage_asset_pose_search`、`search_asset`

---

### `get_asset_refs`

查找包的依赖项或引用方。`direction` 可取：`dependencies`（依赖项）/ `referencers`（被引用方）；可选递归查找。

**适用场景**：查资产依赖/被引用；direction=dependencies|referencers，可选递归

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 要查询的资产路径 |
| `direction` | `string (enum)` |  | 查询方向（默认 dependencies） 枚举值：`dependencies` / `referencers` |
| `nameFilter` | `string` |  | 路径子串过滤 |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |

**相关 Capability**：`search_asset`

---

### `get_asset_render_target`

读取 TextureRenderTarget2D：尺寸/格式/清除色/生成Mips。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | RenderTarget 资产路径 |

**相关 Capability**：`create_asset_render_target`、`manage_asset_render_target`

---

### `get_asset_skeletal_mesh`

检查 SkeletalMesh LOD、材质槽、骨骼与 PhysicsAsset 摘要；只读。

**适用场景**：读 SK 资产；骨骼树用 get_asset_skeleton

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SkeletalMesh 资产路径 |
| `assetPaths` | `string` |  | 多个 SkeletalMesh 路径（批量） |

**相关 Capability**：`manage_asset_skeletal_mesh`、`search_asset`、`get_asset_skeleton`、`get_asset_refs`、`save_asset`

---

### `get_asset_sound_attenuation`

读取 SoundAttenuation：shape/innerRadius/falloffDistance/bAttenuate/bSpatialize。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundAttenuation 资产路径 |

**相关 Capability**：`create_asset_sound_attenuation`、`manage_asset_sound_attenuation`

---

### `get_asset_sound_class`

读取 SoundClass：volume/pitch/lowPassFilter/parentClass/childClasses。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundClass 资产路径 |

**相关 Capability**：`create_asset_sound_class`、`manage_asset_sound_class`

---

### `get_asset_sound_concurrency`

读取 SoundConcurrency：maxCount/resolutionRule/retriggerTime。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundConcurrency 资产路径 |

**相关 Capability**：`create_asset_sound_concurrency`、`manage_asset_sound_concurrency`

---

### `get_asset_sound_cue`

检查 SoundCue 时长与 SoundNode 图摘要；只读。

**适用场景**：读 Cue 图摘要；波形用 get_asset_sound_wave

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundCue 资产路径 |
| `assetPaths` | `string` |  | 多个 SoundCue 路径（批量） |

**相关 Capability**：`manage_asset_sound_cue`、`search_asset`、`get_asset_sound_wave`、`get_asset_refs`

---

### `get_asset_sound_submix`

读取 SoundSubmix：outputVolume/wetLevel/dryLevel/effectChainCount/parentSubmix。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundSubmix 资产路径 |

**相关 Capability**：`manage_asset_sound_submix`、`search_asset`

---

### `get_asset_sound_wave`

检查 SoundWave 时长、采样率与声道；只读。

**适用场景**：读波形元数据；Cue 节点树用 get_asset_sound_cue

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundWave 资产路径 |
| `assetPaths` | `string` |  | 多个 SoundWave 路径（批量） |

**相关 Capability**：`manage_asset_sound_wave`、`search_asset`、`get_asset_sound_cue`、`get_asset_refs`

---

### `get_asset_state_tree`

检查 StateTree 结构快照。Schema/States 树/Evaluators/参数；只读。UE 5.5+。

**适用场景**：读 StateTree 结构：Schema/States/Evaluators/迁移/条件/参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | StateTree 资产路径（/Game/…/ST_Foo） |

**相关 Capability**：`search_asset`、`get_asset_behavior_tree`、`get_asset_refs`、`save_asset`

---

### `get_asset_static_mesh`

检查 StaticMesh LOD、包围盒、材质槽与碰撞摘要；只读。

**适用场景**：读 StaticMesh LOD/材质槽/碰撞；不含编辑

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | StaticMesh 资产路径 |
| `assetPaths` | `string` |  | 多个 StaticMesh 路径（批量） |

**相关 Capability**：`manage_asset_static_mesh`、`search_asset`、`get_asset_refs`、`save_asset`

---

### `get_asset_texture`

检查 Texture2D 尺寸、像素格式、压缩、sRGB、LOD；只读。

**适用场景**：读 Texture2D 元数据；引用用 get_asset_refs

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Texture2D 资产路径 |
| `assetPaths` | `string` |  | 多个 Texture2D 路径（批量） |

**相关 Capability**：`manage_asset_texture`、`search_asset`、`get_asset_refs`、`save_asset`

---

### `get_asset_view_model`

检查 WBP 上的 MVVM ViewModel 列表与 Binding 快照。只读。UE 5.5+。

**适用场景**：读 Widget 蓝图上挂载的 MVVM ViewModel 列表、属性绑定（源↔目标/方向/转换）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Widget 蓝图资产路径（/Game/…/WBP_Foo） |

**相关 Capability**：`get_asset_user_widget`、`manage_asset_user_widget`、`search_asset`、`get_asset_blueprint`

---

### `manage_asset_attribute_set`

批量 `set`/`reset` AttributeSet CDO 的 `FGameplayAttributeData` 默认值。

**适用场景**：写 AttributeSet CDO 默认值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AttributeSet Blueprint 路径 |
| `ops` | `string` | ★ | 操作数组；每项含 action(set/reset) + attributeName + 可选 baseValue |

**相关 Capability**：`get_asset_attribute_set`、`save_asset`、`create_asset_attribute_set`

---

### `manage_asset_control_rig`

编辑 ControlRig：层级（rename_element/set_control_color/add_null/remove_element）与 RigVM 图连线（add_rig_link/break_rig_link/add_rig_node）。

**适用场景**：修改 ControlRig：层级元素增删/改色；RigVM 图节点增删与引脚连线（add_rig_link/break_rig_link）；需 save_asset 落盘

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | ControlRig Blueprint 资产路径 |

**相关 Capability**：`get_asset_control_rig`、`create_asset_control_rig`

---

### `manage_asset_curve`

修改曲线资产关键帧。operations[].action: add_key / set_key / remove_key / set_interp（CurveTable 用 rowName 代替 channel）。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径 |

**相关 Capability**：`create_asset_curve`、`get_asset_curve`

---

### `manage_asset_data_layer`

修改 DataLayer 资产属性（≥UE5.1，需要编辑器）：set_type（Runtime/Editor）、set_debug_color（#RRGGBB）。

**适用场景**：修改 DataLayerAsset 的类型（Runtime/Editor）或调试颜色（≥UE5.1）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | DataLayerAsset 路径 |

**相关 Capability**：`get_asset_data_layer`、`create_asset_data_layer`

---

### `manage_asset_enum`

修改 UserDefinedEnum 枚举项。operations[].action: add_entry / remove_entry / set_display_name。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 枚举资产包路径 |

**相关 Capability**：`create_asset_enum`、`get_asset_enum`

---

### `manage_asset_gameplay_ability`

修改 GA CDO：`set_tags` / `set_policy` / `set_cost_cooldown`；Graph 编辑用 `manage_asset_blueprint`。

**适用场景**：写 GA CDO 策略/Tag/Cost

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | GameplayAbility Blueprint 路径 |
| `action` | `string (enum)` | ★ | 操作类型 枚举值：`set_tags` / `set_policy` / `set_cost_cooldown` |
| `tagContainer` | `string (enum)` |  | Tag 容器名 枚举值：`abilityTags` / `activationOwnedTags` / `activationRequiredTags` / `activationBlockedTags` / `cancelAbilitiesWithTag` / `blockAbilitiesWithTag` |
| `tags` | `string` |  | Tag 字符串数组 |
| `mode` | `string (enum)` |  | set/add/remove 枚举值：`set` / `add` / `remove` |
| `instancingPolicy` | `string (enum)` |  | 实例化策略 枚举值：`NonInstanced` / `InstancedPerActor` / `InstancedPerExecution` |
| `netExecutionPolicy` | `string (enum)` |  | 网络执行策略 枚举值：`LocalPredicted` / `LocalOnly` / `ServerInitiated` / `ServerOnly` |
| `costGE` | `string` |  | Cost GE 资产路径（传空字符串清空） |
| `cooldownGE` | `string` |  | Cooldown GE 资产路径（传空字符串清空） |

**相关 Capability**：`get_asset_gameplay_ability`、`save_asset`、`manage_asset_blueprint`

---

### `manage_asset_gameplay_effect`

批量修改 GE CDO：`set_policy` / `set_tags` / `add_modifier` / `remove_modifier` / `set_modifier`。

**适用场景**：写 GE Modifier/Duration/Tag

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | GameplayEffect Blueprint 路径 |
| `ops` | `string` | ★ | 操作数组；每项为含 action 字段的 JSON 对象 |

**相关 Capability**：`get_asset_gameplay_effect`、`save_asset`、`create_asset_gameplay_effect`

---

### `manage_asset_ik_retargeter`

编辑 IKRetargeter：set_source_rig / set_target_rig / set_chain_source。

**适用场景**：修改 IKRetargeter 绑定；修改后需 save_asset 落盘

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | IKRetargeter 资产路径 |

**相关 Capability**：`get_asset_ik_retargeter`、`get_asset_ik_rig`

---

### `manage_asset_ik_rig`

编辑 IKRig：set_preview_mesh / set_solver_enabled。

**适用场景**：修改 IKRig 属性；修改后需 save_asset 落盘

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | IKRig 资产路径 |

**相关 Capability**：`get_asset_ik_rig`、`create_asset_ik_rig`

---

### `manage_asset_input_action`

编辑 InputAction：set_value_type/add_trigger/remove_trigger/add_modifier/remove_modifier/set_flags。

**适用场景**：改 InputAction 的 ValueType/Trigger/Modifier/标志位

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | InputAction 资产路径 |

**相关 Capability**：`get_asset_input_action`、`create_asset_input_action`

---

### `manage_asset_input_mapping_context`

编辑 IMC：add_mapping/remove_mapping/clear_mappings。

**适用场景**：往 IMC 里添加或移除 Action-Key 绑定

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | InputMappingContext 资产路径 |

**相关 Capability**：`get_asset_input_mapping_context`、`create_asset_input_mapping_context`

---

### `manage_asset_level`

编辑关卡 WorldSettings 与磁盘 Actor：`set_property` / `spawn_actor` / `remove_actor` / `set_actor_property`；`editor_only`。

**前置条件**：`editor_only`

**适用场景**：写操作：改 WorldSettings 或关卡磁盘 Actor

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 关卡资产路径（如 /Game/Maps/MyLevel） |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_property` / `spawn_actor` / `remove_actor` / `set_actor_property` |
| `propertyPath` | `string` |  | WorldSettings 属性路径（set_property） |
| `value` | `string` |  | 属性新值字符串 |
| `classPath` | `string` |  | Actor 类名（spawn_actor） |
| `blueprintPath` | `string` |  | Blueprint 路径（spawn_actor） |
| `location` | `string` |  | 生成位置 x,y,z（spawn_actor） |
| `rotation` | `string` |  | 生成旋转 pitch,yaw,roll（spawn_actor，可选） |
| `actorName` | `string` |  | Actor 名或 Label（remove/set_actor_property） |

**相关 Capability**：`get_asset_level`、`search_asset`

---

### `manage_asset_level_sequence`

编辑 LevelSequence：set_display_rate/set_range/remove_binding/add_master_track/remove_master_track。

**适用场景**：改 LevelSequence 的帧率/播放范围/Binding/MasterTrack

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | LevelSequence 资产路径 |

**相关 Capability**：`get_asset_level_sequence`、`save_asset`

---

### `manage_asset_meta_sound`

修改 MetaSound Source / Patch（≥5.1）：add_input/remove_input/add_output/remove_output/add_node/remove_node/add_edge/remove_edge。

**适用场景**：修改 MetaSound Source 或 Patch 的接口/图；add_edge 用 fromNodeID/fromPin/toNodeID/toPin（节点 ID 从 get_asset_meta_sound 获取）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | MetaSound Source 或 Patch 资产路径 |

**相关 Capability**：`get_asset_meta_sound`、`create_asset_meta_sound`、`create_asset_meta_sound_patch`

---

### `manage_asset_niagara_system`

编辑 Niagara 系统：`set_property` / `set_user_parameter`；不编辑 Emitter 节点图（需 `WITH_NIAGARA`）。

**前置条件**：`editor_only`

**适用场景**：写操作：改 Niagara 属性或用户参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | NiagaraSystem 资产路径 |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_property` / `set_user_parameter` |
| `propertyPath` | `string` |  | 属性路径（set_property） |
| `parameterName` | `string` |  | 用户参数名（set_user_parameter） |
| `value` | `string` |  | 新值字符串 |

**相关 Capability**：`get_asset_niagara_system`、`search_asset`

---

### `manage_asset_pcg_graph`

管理 PCG Graph：add_node/remove_node/add_edge（UE 5.4+）。

**适用场景**：向 PCG Graph 添加/删除节点或连接 pin

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PCG Graph 资产路径 |

**相关 Capability**：`get_asset_pcg_graph`、`create_asset_pcg_graph`

---

### `manage_asset_physical_material`

设置 PhysicalMaterial 属性：friction / restitution / density / surfaceType / raiseMassToPower。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PhysicalMaterial 资产路径 |
| `surfaceType` | `integer` |  | 表面类型枚举值（EPhysicalSurface int） |

**相关 Capability**：`get_asset_physical_material`

---

### `manage_asset_physics_asset`

编辑 PhysicsAsset：set_physics_type/add_sphere/add_capsule/add_box/clear_shapes/add_constraint/remove_constraint。

**适用场景**：给 PhysicsAsset 的骨骼添加碰撞形状、设置 PhysicsType、添加/移除关节约束

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PhysicsAsset 资产路径 |

**相关 Capability**：`get_asset_physics_asset`、`get_asset_skeletal_mesh`

---

### `manage_asset_pose_search`

管理 PoseSearchDatabase：set_schema/add_tag/remove_tag（UE 5.4+）。

**适用场景**：设置 PoseSearch Database 的 Schema 或修改 Tags

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | PoseSearchDatabase 资产路径 |

**相关 Capability**：`get_asset_pose_search`、`search_asset`

---

### `manage_asset_render_target`

修改 TextureRenderTarget2D：sizeX/sizeY/formatValue/clearColor(r,g,b,a)。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | RenderTarget 资产路径 |
| `sizeX` | `integer` |  | 宽度（≥1） |
| `sizeY` | `integer` |  | 高度（≥1） |
| `formatValue` | `integer` |  | ETextureRenderTargetFormat 枚举值：0=RGBA8,1=RGBA16f… |

**相关 Capability**：`create_asset_render_target`、`get_asset_render_target`

---

### `manage_asset_skeletal_mesh`

编辑 SkeletalMesh：`set_material_slot` / `set_property`；改后须 `save_asset`。

**前置条件**：`editor_only`

**适用场景**：写操作：改 SkeletalMesh 材质槽/属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SkeletalMesh 资产路径 |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_material_slot` / `set_property` |
| `slotIndex` | `integer` |  | 材质槽索引（set_material_slot） |
| `materialPath` | `string` |  | 材质资产路径（set_material_slot） |
| `propertyPath` | `string` |  | 属性路径（set_property） |
| `value` | `string` |  | 属性新值（set_property） |

**相关 Capability**：`get_asset_skeletal_mesh`、`get_asset_skeleton`

---

### `manage_asset_sound_attenuation`

设置 SoundAttenuation：innerRadius/falloffDistance/shapeValue/bAttenuate/bSpatialize。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundAttenuation 资产路径 |
| `shapeValue` | `integer` |  | 形状枚举值：0=Sphere,1=Capsule,2=Box,3=Cone |

**相关 Capability**：`get_asset_sound_attenuation`、`create_asset_sound_attenuation`

---

### `manage_asset_sound_class`

设置 SoundClass 的 volume/pitch/lowPassFilter/attenuationScale。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundClass 资产路径 |

**相关 Capability**：`get_asset_sound_class`、`create_asset_sound_class`

---

### `manage_asset_sound_concurrency`

设置 SoundConcurrency：maxCount/resolutionRuleValue/retriggerTime/limitToOwner。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundConcurrency 资产路径 |
| `maxCount` | `integer` |  | 最大并发实例数（≥1） |
| `resolutionRuleValue` | `integer` |  | EMaxConcurrentResolutionRule int 值：0=PreventNew,1=StopOldest… |

**相关 Capability**：`get_asset_sound_concurrency`、`create_asset_sound_concurrency`

---

### `manage_asset_sound_cue`

编辑 SoundCue：`set_property` / `add_node` / `remove_node` / `connect_nodes`；索引与 `get_asset_sound_cue` 一致。

**前置条件**：`editor_only`

**适用场景**：写操作：改 Cue 属性或节点图

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundCue 资产路径 |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_property` / `add_node` / `remove_node` / `connect_nodes` |
| `propertyPath` | `string` |  | 属性路径（set_property） |
| `value` | `string` |  | 属性新值字符串 |
| `nodeClass` | `string` |  | SoundNode 类名（add_node，如 SoundNodeWavePlayer） |
| `soundWavePath` | `string` |  | SoundWave 路径（WavePlayer 可选） |
| `parentNodeIndex` | `integer` |  | 父节点索引（add_node/connect_nodes） |
| `childSlot` | `integer` |  | 父节点子槽（add_node/connect_nodes） |
| `nodeIndex` | `integer` |  | 节点索引（remove_node） |
| `childIndex` | `integer` |  | 子节点索引（connect_nodes） |

**相关 Capability**：`get_asset_sound_cue`、`get_asset_sound_wave`

---

### `manage_asset_sound_submix`

设置 SoundSubmix 音量。UE4/5.0：outputVolume/wetLevel/dryLevel[0,1]；UE5.1+：outputVolumeDB/wetLevelDB/dryLevelDB(dB)。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundSubmix 资产路径 |

**相关 Capability**：`get_asset_sound_submix`

---

### `manage_asset_sound_wave`

编辑 SoundWave 属性：`action=set_property`（音量/循环/衰减等）。

**前置条件**：`editor_only`

**适用场景**：写操作：改 SoundWave 音量/循环/衰减

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | SoundWave 资产路径 |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_property` |
| `propertyPath` | `string` |  | 属性路径（如 Volume/Looping） |
| `value` | `string` |  | 属性新值字符串 |

**相关 Capability**：`get_asset_sound_wave`、`get_asset_sound_cue`

---

### `manage_asset_state_tree`

编辑 StateTree：add_state/remove_state/rename_state/recompile。UE 5.5+。

**适用场景**：增删改 StateTree 的 State 节点，或触发重编译

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | StateTree 资产路径 |

**相关 Capability**：`get_asset_state_tree`、`save_asset`

---

### `manage_asset_static_mesh`

编辑 StaticMesh：`set_material_slot` / `set_property`；改后须 `save_asset`。

**前置条件**：`editor_only`

**适用场景**：写操作：改 StaticMesh 材质槽/属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | StaticMesh 资产路径 |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_material_slot` / `set_property` |
| `slotIndex` | `integer` |  | 材质槽索引（set_material_slot） |
| `materialPath` | `string` |  | 材质资产路径（set_material_slot） |
| `propertyPath` | `string` |  | 属性路径（set_property） |
| `value` | `string` |  | 属性新值（set_property） |

**相关 Capability**：`get_asset_static_mesh`、`search_asset`

---

### `manage_asset_texture`

编辑 Texture2D 属性：`action=set_property`（压缩/sRGB/LODGroup 等）；改后须 `save_asset`。

**前置条件**：`editor_only`

**适用场景**：写操作：改 Texture 压缩/sRGB/LODGroup

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Texture 资产路径 |
| `action` | `string (enum)` | ★ | 操作 枚举值：`set_property` |
| `propertyPath` | `string` |  | 属性路径（如 CompressionSettings/sRGB/LODGroup） |
| `value` | `string` |  | 属性新值字符串 |

**相关 Capability**：`get_asset_texture`、`search_asset`

---

### `reimport_asset`

从源文件重新导入资产，刷新已修改的外部资源。

**前置条件**：`editor_only`

**适用场景**：从源文件重新导入资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产路径 |
| `assetPaths` | `string` |  | 多个资产路径（批量） |

**相关 Capability**：`search_asset`、`export_asset`

---

### `rename_asset`

将编辑器资产移动或重命名到新路径。引擎自动生成重定向器以修复断开的引用。

**适用场景**：移动或重命名资产；自动更新软引用与路径

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 当前资产路径 |
| `newPath` | `string` | ★ | 新完整资产路径 |

**相关 Capability**：`save_asset`、`delete_asset`

---

### `save_asset`

将一个资产包持久化到磁盘。经 `SaveDirtyPackage` 先 `MarkPackageDirty` 再落盘；Live Coding 开启时仅标脏并返回 `deferred=true`。

**适用场景**：落盘脏包；Live Coding 时可能 deferred

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` |  | 单个资产路径 |
| `assetPaths` | `string` |  | 多个资产路径（批量） |

**相关 Capability**：`rename_asset`、`delete_asset`

---

### `search_asset`

查找资产路径。**必须先调用**；须指定 `assetType` 和功能级 `pathFilter`；禁止猜测 `/Game/...` 路径。返回顶层 `assets`/`totalCount`；指定具体 `assetType` 时顶层附 `recommendedGet`/`recommendedManage`（`all` 时推荐在每条上）。

**适用场景**：先 search 再 get/manage；禁止猜路径

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetType` | `string` |  | Blueprint/Widget/Material/AnimSequence/SkeletalMesh/Skeleton/… 或 UClass；大项目避免 all |
| `pathFilter` | `string` |  | 功能级路径前缀（大项目勿用裸 /Game/） |
| `query` | `string` |  | 分词 AND 匹配；匹配名称/路径/标签 |
| `nameFilter` | `string` |  | 资产名称过滤 |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |

**相关 Capability**：`get_asset_blueprint`、`get_asset_refs`

---

## 蓝图工具

### `compile_blueprint`

显式编译 Blueprint/ABP/WBP；可选 saveToDisk 落盘。

**适用场景**：manage 改图后显式编译；落盘用 saveToDisk 或 save_asset

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 蓝图资产路径 |
| `assetPaths` | `string` |  | 多个蓝图路径（批量） |

**相关 Capability**：`save_asset`、`manage_asset_blueprint`、`get_asset_blueprint`

---

### `create_asset_blueprint`

以 UObject 子类为父类创建新 BP 资产，自动编译；用 manage 添加变量/节点/连线。

**适用场景**：创建空白 BP；不用于编辑现有 BP

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新蓝图包路径，如 '/Game/Blueprints/BP_NewActor' |
| `parentClass` | `string` | ★ | 父类名（任意 UObject 子类），如 Actor、Pawn、Character |

**相关 Capability**：`manage_asset_blueprint`、`get_asset_blueprint`

---

### `get_asset_blueprint`

从编辑器读取 BP 结构。**回答蓝图问题前必须先调用**；禁止从源码推断。sections 可选 variable/function/component/graph 等。

**适用场景**：用户问蓝图变量/Graph/函数 — 必须先调用，勿 grep 源码

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`variable` / `function` / `component` / `graph` / `graphOverview` / `defaults` |
| `assetPath` | `string` | ★ | 蓝图资产路径 |
| `nameFilter` | `string` |  | 项名称过滤（/regex/ ^前缀 后缀$） |
| `propertyPaths` | `string` |  | defaults 段精确属性名过滤，如 [\"bUseBuffClass\",\"Scale\"] |
| `graphName` | `string` |  | 图名（仅 graph 段） |
| `graphType` | `string (enum)` |  | 图类型过滤 枚举值：`event` / `function` / `macro` / `animgraph` / `statemachine` / `state` / `transition` / `conduit` / `all` |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |

**相关 Capability**：`manage_asset_blueprint`、`create_asset_blueprint`

---

### `manage_asset_blueprint`

编辑 BP：图/变量/节点/连线、SCS 组件树、CDO 默认值。SCS/defaults 仅限 Actor BP。操作后记得保存。

**适用场景**：写操作：增删变量、图节点、连线

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 蓝图资产路径 |
| `action` | `string (enum)` | ★ | 操作类型 枚举值：`add_variable` / `remove_variable` / `add_node` / `remove_node` / `set_node` / `connect` / `disconnect` / `disconnect_all` / `add_component` / `remove_component` / `set_component_property` / `set_defaults` |
| `graphName` | `string` |  | 图名（节点/连线操作） |
| `variableName` | `string` |  | 变量或节点变量名 |
| `variableType` | `string` |  | 基本或对象类型（add_variable） |
| `defaultValue` | `string` |  | 默认值（add_variable） |
| `category` | `string` |  | 编辑器分类（add_variable） |
| `nodeId` | `string` |  | 节点 GUID（remove/set_node） |
| `nodeClass` | `string` |  | K2Node 类（add_node） |
| `functionName` | `string` |  | CallFunction：函数名 |
| `functionClass` | `string` |  | CallFunction：所属类 |
| `comment` | `string` |  | 节点注释（set_node） |
| `pinName` | `string` |  | 要设默认值的引脚（set_node） |
| `pinDefaultValue` | `string` |  | 引脚新默认值 |
| `sourceNodeId` | `string` |  | 源节点 GUID（连线操作） |
| `sourcePinName` | `string` |  | 源引脚名 |
| `targetNodeId` | `string` |  | 目标节点 GUID（connect/disconnect） |
| `targetPinName` | `string` |  | 目标引脚名 |
| `componentClass` | `string` |  | 组件类名（add_component），如 StaticMeshComponent |
| `componentName` | `string` |  | SCS 变量名（add/remove/set_component_property） |
| `attachTo` | `string` |  | 父组件变量名（add_component）；省略则用默认场景根 |
| `propertyPath` | `string` |  | 属性路径，点分记法如 RelativeLocation.X（set_component_property/set_defaults） |
| `value` | `string` |  | 字符串值，如 (X=100,Y=0,Z=50) 或 true（set_component_property/set_defaults） |

**相关 Capability**：`get_asset_blueprint`、`create_asset_blueprint`、`save_asset`

---

## 动画资产工具

### `create_asset_anim_blueprint`

为指定骨骼创建新 ABP 文件，自动关联骨骼；使用 `manage_asset_anim_blueprint` 填充状态机。

**适用场景**：创建空白 ABP；需要 skeletonPath

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 动画蓝图包路径 |
| `skeletonPath` | `string` | ★ | 骨骼资产路径 |

**相关 Capability**：`manage_asset_anim_blueprint`、`get_asset_anim_blueprint`

---

### `create_asset_anim_composite`

创建 AnimComposite（动画合成）资产；用 manage 添加片段。

**适用场景**：创建空白 AnimComposite；需要 skeletonPath 时绑定骨骼

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AnimComposite 包路径 |
| `skeletonPath` | `string` |  | 骨骼资产路径（可选） |

**相关 Capability**：`get_asset_anim_composite`、`manage_asset_anim_composite`

---

### `create_asset_anim_montage`

为指定骨骼创建新 Montage 文件；使用 `manage_asset_anim_montage` 添加片段填充内容。

**适用场景**：创建空白 Montage；需要 skeletonPath

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Montage 包路径 |
| `skeletonPath` | `string` | ★ | 骨骼资产路径 |

**相关 Capability**：`manage_asset_anim_montage`、`get_asset_anim_montage`

---

### `create_asset_blend_space`

创建 BlendSpace（2D）或 BlendSpace1D 资产；用 manage 配置轴参数与样本。

**适用场景**：新建 BlendSpace；需要 skeletonPath；创建后用 manage 配置轴与样本

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产路径（包路径） |
| `skeletonPath` | `string` | ★ | 关联骨骼路径 |
| `blendSpaceType` | `string (enum)` |  | 类型：blend_space（2D，默认）或 blend_space_1d 枚举值：`blend_space` / `blend_space_1d` |

**相关 Capability**：`get_asset_blend_space`、`manage_asset_blend_space`

---

### `get_asset_anim_blueprint`

检查 ABP 结构。sections=variables|statemachines|defaults|graphOverview；仅限编辑器使用。

**适用场景**：读取 ABP 变量、状态机、默认值；不含写操作

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`variables` / `statemachines` / `defaults` / `graphOverview` |
| `assetPath` | `string` |  | 动画蓝图资产路径 |
| `assetPaths` | `string` |  | 多个动画蓝图路径（批量） |
| `nameFilter` | `string` |  | 变量/默认值名称过滤 |

**相关 Capability**：`manage_asset_anim_blueprint`、`create_asset_anim_blueprint`

---

### `get_asset_anim_composite`

读取 AnimComposite 合成轨道中的片段列表（animReference/startPos/duration/playRate）。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AnimComposite 资产路径 |

**相关 Capability**：`create_asset_anim_composite`、`manage_asset_anim_composite`

---

### `get_asset_anim_montage`

检查 Montage 时间轴快照（槽位/片段/分段）；只读，不触发运行时播放。

**适用场景**：读取 Montage 结构；运行时播放状态请使用 get_runtime_actor_animation

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 动画 Montage 资产路径 |

**相关 Capability**：`manage_asset_anim_montage`、`create_asset_anim_montage`、`get_runtime_actor_animation`

---

### `get_asset_anim_sequence`

检查 AnimSequence 时长、帧率、帧数、骨骼引用与 `notifies[]` 列表；只读。

**适用场景**：读序列元数据与 notifies；Montage 用 get_asset_anim_montage

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AnimSequence 资产路径 |
| `assetPaths` | `string` |  | 多个 AnimSequence 路径（批量） |

**相关 Capability**：`manage_asset_anim_sequence`、`search_asset`、`get_asset_skeleton`、`get_asset_anim_montage`、`get_asset_refs`

---

### `get_asset_blend_space`

读取 BlendSpace 快照：轴参数 + 样本列表。写用 manage_asset_blend_space。

**适用场景**：读取 BlendSpace 轴定义与样本动画；写用 manage_asset_blend_space

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | BlendSpace 资产路径 |
| `assetPaths` | `string` |  | 多个 BlendSpace 路径（批量） |

**相关 Capability**：`manage_asset_blend_space`、`create_asset_blend_space`、`search_asset`

---

### `get_asset_skeleton`

检查 Skeleton 骨骼树（分页）与 Socket 摘要；只读。

**适用场景**：读骨骼树/Socket；绑定网格用 get_asset_skeletal_mesh

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Skeleton 资产路径 |
| `assetPaths` | `string` |  | 多个 Skeleton 路径（批量） |
| `offset` | `integer` |  | 骨骼列表分页偏移 |
| `limit` | `integer` |  | 骨骼列表每页条数 |

**相关 Capability**：`manage_asset_skeleton`、`search_asset`、`get_asset_anim_blueprint`、`get_asset_skeletal_mesh`、`get_asset_refs`

---

### `get_runtime_actor_animation`

从运行中的骨骼网格体获取 AnimInstance 数据。支持 `state` / `slots` / `variables` 段；支持批量 Actor 查询。

**前置条件**：`pie`

**适用场景**：从运行中 Pawn 读 AnimInstance；sections=state|slots|variables

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`state` / `slots` / `variables` |
| `actorName` | `string` |  | Actor 名 |
| `actorNames` | `string` |  | 多个 Actor 名（批量） |
| `nameFilter` | `string` |  | 变量/槽位名过滤 |

**相关 Capability**：`interact_runtime_actor_animation`、`get_asset_anim_montage`

---

### `interact_runtime_actor_animation`

命令式驱动运行时动画：`play_montage` / `stop_montage` / `stop_all` / `set_anim_variable`。

**前置条件**：`pie`

**适用场景**：PIE 播放/停止蒙太奇或写 Anim 变量

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 动画命令 枚举值：`play_montage` / `stop_montage` / `stop_all` / `set_anim_variable` |
| `actorName` | `string` |  | Actor 名 |
| `actorNames` | `string` |  | 多个 Actor 名（批量） |
| `montagePath` | `string` |  | 蒙太奇资产路径（play/stop） |
| `startSection` | `string` |  | 起始 Section 名（play_montage） |
| `variableName` | `string` |  | AnimInstance 变量名（set_anim_variable） |
| `value` | `string` |  | 变量新值字符串（set_anim_variable） |

**相关 Capability**：`get_runtime_actor_animation`、`get_asset_anim_montage`

---

### `manage_asset_anim_blueprint`

编辑 ABP 状态机结构，支持增删 `state_machine` / `state` / `transition` 节点；操作后须保存。

**适用场景**：写操作：增删状态机、状态、过渡

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 动画蓝图资产路径 |
| `action` | `string (enum)` | ★ | 操作类型 枚举值：`add_state_machine` / `remove_state_machine` / `add_state` / `remove_state` / `add_transition` / `remove_transition` |
| `graphName` | `string` |  | 所属 AnimGraph 名（默认 AnimGraph） |
| `stateMachineName` | `string` |  | 状态机名（boundgraph 名） |
| `stateName` | `string` |  | 状态名（add/remove_state、过渡源） |
| `targetStateName` | `string` |  | 过渡目标状态名 |

**相关 Capability**：`get_asset_anim_blueprint`、`create_asset_anim_blueprint`、`save_asset`

---

### `manage_asset_anim_composite`

编辑 AnimComposite 合成轨道片段。operations[].action: add_segment / remove_segment。

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AnimComposite 资产路径 |

**相关 Capability**：`create_asset_anim_composite`、`get_asset_anim_composite`

---

### `manage_asset_anim_montage`

编辑 Montage 结构，支持增删槽位、片段和分段；需单独调用 `save_asset` 保存。

**适用场景**：写操作：增删 Montage 片段、分段、槽位

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 动画 Montage 资产路径 |
| `action` | `string (enum)` | ★ | 操作类型 枚举值：`add_segment` / `remove_segment` / `add_section` / `remove_section` |
| `animSequencePath` | `string` |  | AnimSequence 路径（add_segment） |
| `slotName` | `string` |  | 槽位名 |
| `segmentIndex` | `integer` |  | 要删除的片段索引（remove_segment） |
| `sectionName` | `string` |  | 分段名（add_section / remove_section） |
| `nextSectionName` | `string` |  | 循环下一分段（add_section，可选） |

**相关 Capability**：`get_asset_anim_montage`、`create_asset_anim_montage`、`save_asset`

---

### `manage_asset_anim_sequence`

编辑 AnimSequence：`add_notify` / `remove_notify` / `set_frame_rate` / `set_root_motion`；改后须 `save_asset`。

**前置条件**：`editor_only`

**适用场景**：写操作：增删 AnimNotify、改帧率/根运动

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | AnimSequence 资产路径 |
| `action` | `string (enum)` | ★ | 编辑操作 枚举值：`add_notify` / `remove_notify` / `set_frame_rate` / `set_root_motion` / `add_float_curve` / `set_curve_key` / `remove_curve` |
| `notifyName` | `string` |  | Notify 名（add/remove） |
| `notifyClass` | `string` |  | Notify 类路径（add；默认 AnimNotify） |
| `notifyIndex` | `integer` |  | Notify 索引（remove） |
| `rootMotion` | `string` |  | 根运动模式：RootMotionFromEverything|RootMotionFromMontagesOnly|NoRootMotionExtraction |
| `curveName` | `string` |  | 曲线名（add_float_curve / set_curve_key / remove_curve） |

**相关 Capability**：`get_asset_anim_sequence`、`get_asset_anim_montage`

---

### `manage_asset_blend_space`

编辑 BlendSpace：set_axis / add_sample / remove_sample。

**适用场景**：配置 BlendSpace 轴参数或添加/删除动画样本；修改后需 save_asset 落盘

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | BlendSpace 资产路径 |

**相关 Capability**：`get_asset_blend_space`、`create_asset_blend_space`

---

### `manage_asset_skeleton`

编辑 Skeleton Socket：`add_socket` / `remove_socket` / `modify_socket`。

**前置条件**：`editor_only`

**适用场景**：写操作：增删改 Skeleton Socket

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Skeleton 资产路径 |
| `action` | `string (enum)` | ★ | Socket 操作 枚举值：`add_socket` / `remove_socket` / `modify_socket` |
| `socketName` | `string` |  | Socket 名 |
| `boneName` | `string` |  | 挂载骨骼名（add/modify） |
| `location` | `string` |  | 位置 X,Y,Z（add/modify） |
| `rotation` | `string` |  | 旋转 P,Y,R（add/modify） |
| `scale` | `string` |  | 缩放 X,Y,Z（add/modify） |

**相关 Capability**：`get_asset_skeleton`、`get_asset_skeletal_mesh`

---

## 材质工具（Material）

### `create_asset_material`

创建新的材质或材质实例文件。材质实例需传 `parentMaterial` 路径；`materialDomain` 与 `type` 须保持一致。

**适用场景**：创建空白 Material 或 MaterialInstance 资产

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新资产完整包路径（如 /Game/Mats/M1.M1） |
| `type` | `string (enum)` |  | 资产种类 枚举值：`Material` / `MaterialInstance` / `Material` |
| `parentMaterial` | `string` |  | 父材质路径（type 为 MaterialInstance 时必填） |
| `materialDomain` | `string (enum)` |  | 材质域（仅 Material） 枚举值：`surface` / `deferredDecal` / `lightFunction` / `volume` / `postProcess` / `ui` / `runtimeVirtualTexture` |

**相关 Capability**：`manage_asset_material`、`get_asset_material`

---

### `create_asset_material_function`

创建空白 UMaterialFunction。可设 description 和 bExposeToLibrary。

**适用场景**：新建 MaterialFunction；之后用 manage_asset_material 添加节点

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径（/Game/…/MF_MyFunc） |
| `description` | `string` |  | 函数描述（可选） |

**相关 Capability**：`get_asset_material`、`manage_asset_material`、`create_asset_material`

---

### `create_asset_material_parameter_collection`

创建空白 MaterialParameterCollection。用 manage 添加参数。

**适用场景**：新建 MaterialParameterCollection；之后用 manage 添加标量/向量参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径（/Game/…/MPC_Global） |

**相关 Capability**：`get_asset_material_parameter_collection`、`manage_asset_material_parameter_collection`

---

### `get_asset_material`

检查 Mat/MI/MF 节点图和参数。支持 `overview` / `params` / `graph` 段；可按名称过滤并分页。

**适用场景**：读取节点图、参数、连线；不含编辑操作

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`overview` / `params` / `graph` |
| `assetPath` | `string` |  | Material/MI/MaterialFunction 资产路径 |
| `assetPaths` | `string` |  | 多个材质资产路径（批量） |
| `nameFilter` | `string` |  | 参数/节点名过滤 |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |

**相关 Capability**：`manage_asset_material`、`create_asset_material`

---

### `get_asset_material_parameter_collection`

列举 MaterialParameterCollection 的标量/向量参数及其默认值。

**适用场景**：读 MPC 的全部标量/向量参数名与默认值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | MPC 资产路径（/Game/…/MPC_Foo） |

**相关 Capability**：`manage_asset_material_parameter_collection`、`get_asset_material`、`manage_asset_material`

---

### `manage_asset_material`

批量编辑材质/材质实例，支持 `set_param` / `add_node` / `connect` / `recompile` 操作；需先获取节点 ID 再操作。

**适用场景**：写操作：设置参数、增删节点、连接连线、重新编译

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 编辑操作 枚举值：`set_param` / `add_node` / `remove_node` / `set_node` / `recompile` / `connect` / `disconnect` / `disconnect_all` |
| `paramName` | `string` |  | 参数名（set_param） |
| `paramType` | `string (enum)` |  | 参数类型 枚举值：`scalar` / `vector` / `texture` |
| `value` | `string` |  | float / R,G,B,A / 纹理路径 |
| `expressionClass` | `string` |  | 表达式类短名（add_node） |
| `parameterName` | `string` |  | Parameter/TextureSampleParam 名 |
| `defaultValue` | `string` |  | float / R,G,B,A / 纹理路径 |
| `nodeId` | `string` |  | 表达式节点 id（remove/set） |
| `sourceNodeId` | `string` |  | 源节点 id（connect/disconnect_all） |
| `sourceOutputName` | `string` |  | 源输出引脚名（默认第一个） |
| `targetNodeId` | `string` |  | 目标节点 id 或 Material（connect/disconnect） |
| `targetInputName` | `string` |  | 目标输入引脚或材质属性名 |

**相关 Capability**：`get_asset_material`、`create_asset_material`、`save_asset`

---

### `manage_asset_material_parameter_collection`

增删改 MPC 的标量/向量参数（add_scalar/add_vector/remove/set_default）。

**适用场景**：往 MPC 里增删改标量/向量参数

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | MPC 资产路径 |

**相关 Capability**：`get_asset_material_parameter_collection`、`manage_asset_material`

---

## 结构体工具（Struct）

### `create_asset_struct`

创建新的 UserDefinedStruct 文件，自动编译；使用 `manage_asset_struct_field` 添加字段。

**适用场景**：创建空白 UserDefinedStruct；用 manage 添加字段

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新结构体包路径 |

**相关 Capability**：`manage_asset_struct_field`、`get_asset_struct`

---

### `get_asset_struct`

检查 UDS 字段定义。每个字段含 `name` / `type` / `subType` / `defaultValue`；支持 `propertyPaths` 过滤。

**适用场景**：读取 UDS 字段定义；不含编辑操作

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | UserDefinedStruct 资产路径 |
| `propertyPaths` | `string` |  | 字段名过滤（首段） |

**相关 Capability**：`manage_asset_struct_field`、`create_asset_struct`

---

### `manage_asset_struct_field`

批量编辑 UDS 字段，支持 `add` / `remove` / `modify`，携带类型、名称和默认值；修改后自动重新编译。

**适用场景**：写操作：增删/修改 UDS 字段

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 字段操作 枚举值：`add` / `remove` / `set` |
| `fieldName` | `string` | ★ | 字段显示名 |
| `fieldType` | `string` |  | 字段类型（add） |
| `defaultValue` | `string` |  | 默认值（add/set） |
| `newName` | `string` |  | 新显示名（set） |
| `newType` | `string` |  | 新字段类型（set） |

**相关 Capability**：`get_asset_struct`、`create_asset_struct`、`save_asset`

---

## 数据资产工具（DataAsset / DataTable）

### `create_asset_data_asset`

创建新的类型化数据对象文件。需指定子类名；仅支持非抽象类。

**适用场景**：创建 DataAsset；parentClass 默认为 PrimaryDataAsset

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新 DataAsset 包路径 |
| `parentClass` | `string` |  | 非抽象父类名 |

**相关 Capability**：`manage_asset_data_asset`、`get_asset_data_asset`

---

### `create_asset_data_table`

创建带行结构体的新数据表文件；使用 `manage_asset_data_table` 填充行数据。

**适用场景**：创建空白数据表；需要 rowStructName

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新 DataTable 包路径 |
| `rowStructName` | `string` | ★ | 行结构体类名（须已存在） |

**相关 Capability**：`manage_asset_data_table`、`get_asset_data_table`

---

### `get_asset_data_asset`

读取自定义数据对象的属性，可编辑字段含类型/当前值/是否继承等信息；支持路径过滤。

**适用场景**：读取 DataAsset 属性；不含编辑操作

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | DataAsset 资产路径 |
| `nameFilter` | `string` |  | 属性名过滤（/regex/ ^前缀 后缀$） |
| `propertyPaths` | `string` |  | 精确属性名过滤（首段路径），如 [\"Health\",\"Damage\"] |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大条数 |

**相关 Capability**：`manage_asset_data_asset`、`create_asset_data_asset`

---

### `get_asset_data_table`

检查数据表行或 Schema。`mode=schema` 返回列定义，`mode=rows` 返回行值；支持 `propertyPaths` 过滤。

**适用场景**：读取数据表 Schema 或行值；不含编辑操作

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | DataTable 资产路径 |
| `mode` | `string (enum)` |  | auto：rowNames 非空则 rows 否则 schema；schema：忽略 rowNames；rows：要求 rowNames 非空 枚举值：`auto` / `schema` / `rows` / `auto` |
| `rowNames` | `string` |  | 行名（rows 模式或 auto 且非空时） |
| `nameFilter` | `string` |  | 行名过滤（/regex/ ^前缀 后缀$） |
| `propertyPaths` | `string` |  | 列/字段名过滤（首段路径），schema 列表与行字段导出 |
| `offset` | `integer` |  | 分页偏移 |
| `limit` | `integer` |  | 每页最大行数 |

**相关 Capability**：`manage_asset_data_table`、`create_asset_data_table`

---

### `manage_asset_data_asset`

批量编辑自定义数据对象属性。`set` 使用 ImportText 验证，`reset` 恢复为 CDO 默认值；`ops[]` 不能为空。

**适用场景**：写操作：设置或重置 DataAsset 属性为 CDO 默认值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` |  | 属性操作 枚举值：`set` / `reset` / `set` |
| `propertyName` | `string` | ★ | 可编辑属性名 |
| `value` | `string` |  | 新值字符串（仅 set） |

**相关 Capability**：`get_asset_data_asset`、`create_asset_data_asset`、`save_asset`

---

### `manage_asset_data_table`

批量编辑数据表行，支持 `add` / `remove` / `set` rows[]；ImportText 验证；仅真实变更时才标记为已修改。

**适用场景**：写操作：增删/设置数据表行值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 行操作 枚举值：`add` / `remove` / `set` / `add` |
| `rowName` | `string` | ★ | 行名 |
| `fieldName` | `string` |  | 字段名（仅 set） |
| `value` | `string` |  | 新值字符串（仅 set） |

**相关 Capability**：`get_asset_data_table`、`create_asset_data_table`、`save_asset`

---

## 控件蓝图工具（Widget）

### `create_asset_user_widget`

创建新的 WBP 文件。`parentClass` 设置 UI 基类；使用 `manage_asset_user_widget` 填充控件树。

**适用场景**：创建空白 WBP；parentClass 可选（默认为 UserWidget）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 新 WidgetBlueprint 包路径 |
| `parentClass` | `string` |  | 父类名（默认 UserWidget） |

**相关 Capability**：`manage_asset_user_widget`、`get_asset_user_widget`

---

### `get_asset_user_widget`

从编辑器读取 WBP 控件树与 UMG 动画。**回答 Widget/UMG 问题前必须先调用**；禁止从源码推断。sections 可选 widgets/animations。

**适用场景**：用户问控件树/UMG 动画 — 必须先调用，勿 grep 源码

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`widgets` / `animations` |
| `assetPath` | `string` | ★ | Widget 蓝图资产路径 |
| `nameFilter` | `string` |  | Widget/动画名称子串匹配（可选） |
| `typeFilter` | `string` |  | Widget 类子串匹配（仅 widgets 段） |
| `offset` | `integer` |  | Widget 分页偏移（默认 0） |
| `limit` | `integer` |  | 每页最大 Widget 数 1~500（默认 100） |

**相关 Capability**：`manage_asset_user_widget`、`create_asset_user_widget`

---

### `manage_asset_user_widget`

批量编辑 WBP 层级：`add` / `remove` / `set_slot` / `set_property`；操作后须 `save_asset`。

**适用场景**：写操作：增删控件、改 Slot/属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | Widget 操作 枚举值：`add` / `remove` / `set_slot` / `set_property` |
| `widgetClass` | `string` |  | Widget 类短名（add） |
| `widgetName` | `string` |  | Widget 名；remove/set_* 时必填 |
| `parentWidget` | `string` |  | 父面板 Widget 名（add） |
| `propertyPath` | `string` |  | 属性路径（set_property） |
| `value` | `string` |  | 属性值（set_property） |

**相关 Capability**：`get_asset_user_widget`、`create_asset_user_widget`、`save_asset`

---

## Lua 运行时工具

### `dofile_runtime_lua`

从 Content/Script/ 根目录加载并执行 .lua 文件，使用相对路径。需要 UnLua + PIE 运行中。

**前置条件**：`unlua` / `pie`

**适用场景**：从 Content/Script/ 加载执行 .lua；需路径与 UnLua+PIE

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `filePath` | `string` | ★ | Lua 文件路径（相对 Content/Script/） |

**相关 Capability**：`eval_runtime_lua`、`hotreload_runtime_lua`

---

### `eval_runtime_lua`

在 PIE/Game 中执行任意 Lua 代码片段，返回压栈值；尽力完成 UE 环境初始化。

**前置条件**：`unlua` / `pie`

**适用场景**：在 PIE/Game 执行 Lua 片段；返回压栈值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `code` | `string` | ★ | Lua 表达式或代码块 |

**相关 Capability**：`set_runtime_lua`、`get_runtime_lua_value`

---

### `gc_runtime_lua`

控制 PIE 中 Lua 的 GC 周期。模式可取：`collect`（默认）/ `stop` / `restart` / `count`。

**前置条件**：`unlua` / `pie`

**适用场景**：控制 PIE 内 Lua GC；mode=collect|stop|restart|count

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `mode` | `string (enum)` |  | GC 模式 枚举值：`collect` / `stop` / `restart` / `count` / `collect` |

**相关 Capability**：`get_runtime_lua_memory`

---

### `get_asset_lua_binding`

解析蓝图绑定的 UnLua 模块，返回 `bound`（已绑定）和 `fileExists`（文件存在）标志。需要 UnLua 插件。

**前置条件**：`unlua` / `editor_only`

**适用场景**：读取/编辑前先找到 Lua 文件路径

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 蓝图资产路径 |

**相关 Capability**：`get_runtime_lua_object`、`get_runtime_lua_env`

---

### `get_runtime_lua_env`

枚举 `_G` 或指定路径的 Lua 嵌套表中的所有键，支持名称过滤和数量限制。

**前置条件**：`unlua` / `pie`

**适用场景**：浏览所有键（用 env）或读取单个键值（用 value）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `path` | `string` |  | 点分表路径；省略则为 _G |
| `nameFilter` | `string` |  | 键名过滤；支持 /regex/、^前缀、后缀$ |
| `limit` | `integer` |  | 最大返回条数 |

**相关 Capability**：`get_runtime_lua_value`、`get_runtime_lua_object`

---

### `get_runtime_lua_loaded`

枚举 `package.loaded` 缓存中已加载的 Lua 模块列表，支持名称模式过滤。

**前置条件**：`unlua` / `pie`

**适用场景**：枚举 package.loaded 已加载模块；支持名称过滤

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `nameFilter` | `string` |  | 键名过滤；支持 /regex/、^前缀、后缀$ |
| `limit` | `integer` |  | 最大返回条数 |

**相关 Capability**：`hotreload_runtime_lua`、`dofile_runtime_lua`

---

### `get_runtime_lua_memory`

报告 Lua VM 堆内存使用量（KB 和字节），无需参数；配合 `gc_runtime_lua` 诊断内存泄漏。

**前置条件**：`unlua` / `pie`

**适用场景**：在 gc_collect 前后检查堆内存大小

**相关 Capability**：`gc_runtime_lua`

---

### `get_runtime_lua_metatable`

沿 `__index` 链遍历并转储指定点路径的 OOP 类表，用于 UnLua 继承链调试。

**前置条件**：`unlua` / `pie`

**适用场景**：查 __index 链追溯 OOP 类；查 UnLua 继承与属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `path` | `string` | ★ | Lua 点分路径 |
| `nameFilter` | `string` |  | 键名过滤；支持 /regex/、^前缀、后缀$ |
| `limit` | `integer` |  | 最大返回条数 |

**相关 Capability**：`get_runtime_lua_object`、`get_runtime_lua_env`

---

### `get_runtime_lua_object`

读取 UnLua 绑定的 Actor/UObject 实例级 Lua 表，通过指针在注册表中定位。

**前置条件**：`unlua` / `pie`

**适用场景**：查 UnLua 绑 Actor/UObject 的实例 Lua 表

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `actorName` | `string` | ★ | 运行时 Actor 名 |
| `path` | `string` |  | Lua 表内点分子路径 |
| `nameFilter` | `string` |  | 键名过滤；支持 /regex/、^前缀、后缀$ |
| `limit` | `integer` |  | 最大返回键数 |

**相关 Capability**：`get_runtime_lua_env`、`get_asset_lua_binding`

---

### `get_runtime_lua_stack`

转储 Lua 调用栈帧及局部变量/上值。按帧索引向下钻取；`detail` 可取：`locals` / `upvalues` / `all`。

**前置条件**：`unlua` / `pie`

**适用场景**：转储 Lua 调用栈；局部/上值；detail=locals|upvalues|all

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `detail` | `string (enum)` |  | 栈帧详情 枚举值：`summary` / `locals` / `upvalues` / `all` / `summary` |
| `frameIndex` | `integer` |  | 要钻取的单个栈帧 |
| `sourceFilter` | `string` |  | 栈帧源路径过滤 |
| `maxDepth` | `integer` |  | 最大栈帧数 |

**相关 Capability**：`eval_runtime_lua`

---

### `get_runtime_lua_value`

按点路径读取单个 Lua 全局变量或嵌套字段，返回当前类型和值。

**前置条件**：`unlua` / `pie`

**适用场景**：读取单个键值（用此工具）vs 浏览所有键（用 env）

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `path` | `string` | ★ | Lua 点分路径 |

**相关 Capability**：`set_runtime_lua`、`get_runtime_lua_env`

---

### `hotreload_runtime_lua`

热重载 UnLua 模块（2.x）。UnLua 1.x 不执行，返回 error。

**前置条件**：`unlua` / `pie`

**适用场景**：热重载 UnLua 模块（2.x）；需运行中 PIE；UnLua 1.x 不执行热重载会 error

**相关 Capability**：`dofile_runtime_lua`、`eval_runtime_lua`

---

### `set_runtime_lua`

为 Lua 全局变量或嵌套表字段赋值，使用点路径记法；支持 `string` / `number` / `bool` / `null` 类型。

**前置条件**：`unlua` / `pie`

**适用场景**：为 Lua 全局或嵌套字段赋值；路径含 string/number/bool/null

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `path` | `string` | ★ | set 的点路径目标 |

**相关 Capability**：`get_runtime_lua_value`、`eval_runtime_lua`

---

## 运行时工具（Runtime）

### `destroy_runtime_actor`

从 PIE/Game 中移除指定的运行时场景实体，并将 Level 包标记为已修改。

**前置条件**：`pie`

**适用场景**：从 PIE/Game 移除运行时 Actor；非 Level 盘修改

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `actorName` | `string` | ★ | 要销毁的 Actor 名或标签 |

**相关 Capability**：`spawn_runtime_actor`、`list_runtime_actors`

---

### `destroy_runtime_widget`

从视口移除并销毁运行时 UMG 面板；按 `widgetName` 定位。

**前置条件**：`pie`

**适用场景**：销毁运行时 UMG 面板

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `widgetName` | `string` | ★ | 要销毁的 UserWidget 实例名 |
| `ownerWidget` | `string` |  | Owner UserWidget 类/名过滤（可选） |

**相关 Capability**：`spawn_runtime_widget`、`list_runtime_widgets`

---

### `diff_runtime_actors`

对比两个运行时 Actor 的属性差异。支持指定属性路径过滤或全量扫描；最多返回 50 条差异。

**前置条件**：`pie`

**适用场景**：对比两个运行时 Actor 属性差异；最多 50 项；propertyPaths 过滤

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `actorNameA` | `string` | ★ | 第一个 Actor 名 |
| `actorNameB` | `string` | ★ | 第二个 Actor 名 |
| `propertyPaths` | `string` |  | 点分路径；省略=全部可编辑属性 |

**相关 Capability**：`get_runtime_actor_property`、`list_runtime_actors`

---

### `get_runtime_actor_ability_system`

PIE 读 Actor ASC 快照。`sections=abilities|effects|attributes`；写用 `interact_runtime_actor_ability_system`。

**前置条件**：`pie`

**适用场景**：PIE 读 ASC 快照

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `sections` | `string[]` |  | 查询段（可多选）：`abilities` / `effects` / `attributes` |
| `actorName` | `string` |  | Actor 名称（可选；省略则取 World 中首个带 ASC 的 Pawn/Actor） |

**相关 Capability**：`interact_runtime_actor_ability_system`、`get_gameplay_tags`、`get_runtime_actor_property`

---

### `get_runtime_actor_property`

查询运行时场景对象的字段值，支持诊断预设、批量属性路径和组件树遍历。

**前置条件**：`pie`

**适用场景**：只读字段，不做修改

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `target` | `string (enum)` |  | 分发目标（自动推断） 枚举值：`actor` / `widget` / `asset` |
| `actorName` | `string` | ★ | Actor 名/标签（先 list_runtime_actors） |
| `propertyPath` | `string` |  | 点分路径（单个） |
| `propertyPaths` | `string` |  | 点分路径（批量） |
| `view` | `string (enum)` |  | Actor 树视图 枚举值：`components` / `attach_hierarchy` / `all` |
| `diagnose` | `string (enum)` |  | Actor 诊断预设 枚举值：`visibility` / `transform` / `world_transform` / `rotation_chain` / `defaults` |

**相关 Capability**：`set_runtime_actor_property`、`list_runtime_actors`

---

### `get_runtime_slate_widget`

按十六进制地址检查原生 SWidget（地址来自 UE Widget Reflector），返回类型/可见性/子控件信息。

**前置条件**：`pie`

**适用场景**：持有来自 Widget Reflector 的十六进制地址时使用

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `address` | `string` | ★ | Widget Reflector 提供的十六进制地址 |

**相关 Capability**：`list_runtime_widgets`

---

### `get_runtime_widget_property`

从指定名称的运行时 UMG 元素获取字段值，使用 `widgetName`+`ownerClass` 定位；支持批量属性路径或子控件查询。

**前置条件**：`pie`

**适用场景**：只读 UMG 字段，不做修改

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `widgetName` | `string` |  | 运行时 Widget 名 |
| `ownerClass` | `string` |  | UserWidget 过滤 |
| `propertyPath` | `string` |  | 点分路径（单个） |
| `propertyPaths` | `string` |  | 点分路径（批量） |

**相关 Capability**：`set_runtime_widget_property`、`list_runtime_widgets`

---

### `interact_runtime_actor_ability_system`

运行时写 ASC：`activate_ability` / `cancel_ability` / `apply_effect` / `remove_effect` / `set_attribute`。

**前置条件**：`pie`

**适用场景**：PIE 施放技能/Apply GE/改属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 写操作 枚举值：`activate_ability` / `cancel_ability` / `apply_effect` / `remove_effect` / `set_attribute` |
| `actorName` | `string` |  | Actor 名称（可选；省略取首个带 ASC 的 Pawn/Actor） |
| `abilityPath` | `string` |  | GameplayAbility 资产路径（activate/cancel） |
| `effectPath` | `string` |  | GameplayEffect 资产路径（apply/remove） |
| `attributeName` | `string` |  | 属性名，格式 AttributeSetName.AttributeName（set_attribute） |

**相关 Capability**：`get_runtime_actor_ability_system`、`get_gameplay_tags`

---

### `interact_runtime_widget`

在运行时 UMG 元素上触发 UI 事件。`action` 可取：`click` / `check` / `toggle` / `set` / `read`；支持 Button / CheckBox / Slider / TextBlock / EditableText / ProgressBar。

**前置条件**：`pie`

**适用场景**：触发运行时 UMG 事件；action=click|check|toggle|set|read

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `widgetName` | `string` | ★ | 子 Widget 名 |
| `action` | `string (enum)` | ★ | 交互操作 枚举值：`click` / `check` / `uncheck` / `toggle` / `set` / `read` |
| `value` | `string` |  | action=set 时的新值 |
| `ownerWidget` | `string` |  | Owner UserWidget 类/名过滤 |

**相关 Capability**：`list_runtime_widgets`、`get_runtime_widget_property`

---

### `list_runtime_actors`

枚举 PIE/Game 世界中的 Actor，支持按类/标签/名称过滤；返回 Actor 引用列表，不含具体属性值。

**前置条件**：`pie`

**适用场景**：枚举 PIE/Game 中 Actor；类/标签/名称过滤；不含属性值

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `classFilter` | `string` |  | Actor 类名子串匹配（可选） |
| `nameFilter` | `string` |  | Actor 名或标签子串匹配（可选） |
| `tagFilter` | `string` |  | 仅含此标签的 Actor（可选） |
| `offset` | `integer` |  | 分页偏移（默认 0） |
| `limit` | `integer` |  | 最大条数 1~500（默认 100） |
| `detail` | `string (enum)` |  | 响应详细度：minimal/standard/full 枚举值：`minimal` / `standard` / `full` / `standard` |

**相关 Capability**：`get_runtime_actor_property`、`diff_runtime_actors`

---

### `list_runtime_widgets`

枚举 PIE/Game 视口中的 UMG UserWidget 实例，支持按类型/名称/显示文本过滤。

**前置条件**：`pie`

**适用场景**：枚举 PIE/Game 视口 UMG 实例；类/名/displayText 过滤

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `classFilter` | `string` |  | UserWidget 类名过滤 |
| `nameFilter` | `string` |  | 子 Widget 名过滤 |
| `textFilter` | `string` |  | 可见显示文本子串过滤 |
| `offset` | `integer` |  | 分页偏移（默认 0） |
| `limit` | `integer` |  | 最大条数 1~500（默认 100） |

**相关 Capability**：`get_runtime_widget_property`、`interact_runtime_widget`

---

### `set_runtime_actor_property`

批量修改运行时场景对象的可编辑字段，`updates` 数组中每项对应一个结果。

**前置条件**：`pie`

**适用场景**：运行时修改 Actor 的实时字段

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `target` | `string (enum)` |  | 分发目标（自动推断） 枚举值：`actor` / `widget` / `asset` |

**相关 Capability**：`get_runtime_actor_property`

---

### `set_runtime_widget_property`

批量修改运行时 UMG 元素的字段；`updates[]` 每项含控件名、属性路径和目标值。

**前置条件**：`pie`

**适用场景**：运行时修改 UMG 元素的实时字段

**相关 Capability**：`get_runtime_widget_property`

---

### `spawn_runtime_actor`

在 PIE 世界中实例化场景实体，接受 `blueprintPath` 或 `className`，位置/旋转可指定；自动处理碰撞偏移。

**前置条件**：`pie`

**适用场景**：在 PIE 实例化 Actor；blueprintPath 或 className；可设位置/旋转

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `blueprintPath` | `string` |  | 蓝图路径（与 className 二选一） |
| `className` | `string` |  | 原生类名（与 blueprintPath 二选一） |

**相关 Capability**：`destroy_runtime_actor`、`list_runtime_actors`

---

### `spawn_runtime_widget`

在 PIE/Game 视口中创建并显示 UMG 面板，接受 WidgetBlueprint 的资产路径和 `zOrder`。

**前置条件**：`pie`

**适用场景**：在 PIE/Game 视口创建显示 UMG 面板；assetPath+zOrder

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | Widget 蓝图资产路径 |
| `zOrder` | `integer` |  | AddToViewport 的 Z 序（默认 0） |

**相关 Capability**：`list_runtime_widgets`、`interact_runtime_widget`

---

## AI 工具

### `create_asset_behavior_tree`

创建新的行为树文件，初始为空。通过 `manage_asset_behavior_tree` 的 `set_blackboard` 关联黑板，之后再填充节点。

**适用场景**：创建空白行为树；无节点，尚未关联黑板

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 行为树包路径 |

**相关 Capability**：`manage_asset_behavior_tree`、`create_asset_blackboard`

---

### `create_asset_blackboard`

创建无键的空黑板文件，通过 `manage_asset_behavior_tree` 的 `set_blackboard` 动作关联到行为树。

**适用场景**：创建空白黑板；用 manage_asset_blackboard 添加键

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | BlackboardData 包路径，如 '/Game/AI/BB_Enemy' |

**相关 Capability**：`manage_asset_blackboard`、`get_asset_blackboard`、`create_asset_behavior_tree`

---

### `create_asset_eqs`

创建空白 UEnvQuery（EQS 环境查询）。用 manage 添加 Generator/Test。

**适用场景**：新建 EQS 环境查询资产；之后用 manage_asset_eqs 添加 Generator/Test

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 资产包路径（/Game/…/EQ_FindCover） |

**相关 Capability**：`get_asset_eqs`、`manage_asset_eqs`、`create_asset_behavior_tree`

---

### `get_asset_behavior_tree`

检查行为树结构快照（含路径索引与节点/装饰器/服务属性）；只读，不做修改。

**适用场景**：读取 BT 路径索引、装饰器参数与节点属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` |  | 行为树资产路径 |

**相关 Capability**：`manage_asset_behavior_tree`、`create_asset_behavior_tree`、`get_asset_blackboard`

---

### `get_asset_blackboard`

检查黑板键定义，返回所有键的名称和类型快照；只读。

**适用场景**：读取黑板键列表；运行时黑板值请使用 get_runtime_actor_behavior_tree

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` |  | BlackboardData 资产路径 |
| `nameFilter` | `string` |  | 黑板键名过滤 |

**相关 Capability**：`manage_asset_blackboard`、`get_asset_behavior_tree`

---

### `get_asset_eqs`

读取 EQS 的 Options/Generator/Test 概览。UE5+。

**适用场景**：读 EQS 的 Option/Generator/Test 列表及测试类名

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | EnvQuery 资产路径 |

**相关 Capability**：`manage_asset_eqs`、`create_asset_eqs`、`get_asset_behavior_tree`

---

### `get_runtime_actor_behavior_tree`

查询运行中 AI 的当前活动行为树节点和黑板键值。目标为 AIController 或其控制的 Pawn。

**前置条件**：`pie`

**适用场景**：读运行中 AI 的 BT 节点与 BB 值；写黑板/重启用 interact

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `actorName` | `string` |  | Controller 或 Pawn 名（可选；省略则用首个 AIController） |
| `nameFilter` | `string` |  | 黑板键名过滤 |

**相关 Capability**：`interact_runtime_actor_behavior_tree`、`get_asset_behavior_tree`

---

### `interact_runtime_actor_behavior_tree`

运行时写 BT：`set_blackboard` / `restart_tree` / `stop_tree`；按 AIController 定位。

**前置条件**：`pie`

**适用场景**：PIE 写黑板/重启或停止 BT

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 写操作 枚举值：`set_blackboard` / `restart_tree` / `stop_tree` |
| `actorName` | `string` |  | Controller 或 Pawn 名（可选；省略取首个 AIController） |
| `keyName` | `string` |  | 黑板键名（set_blackboard） |
| `value` | `string` |  | 键值字符串（set_blackboard） |
| `treePath` | `string` |  | BT 资产路径（restart_tree 可选；省略则重启当前树） |

**相关 Capability**：`get_runtime_actor_behavior_tree`、`get_asset_behavior_tree`

---

### `manage_asset_behavior_tree`

批量编辑 BT 节点/装饰器/服务：`move_node` 与 `set_property` 等；改后刷新编辑器 BT 图。

**适用场景**：写操作：增删/移动节点、装饰器、服务，设置属性

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | 行为树资产路径 |
| `action` | `string (enum)` | ★ | 操作类型 枚举值：`set_root` / `add_node` / `remove_node` / `move_node` / `add_decorator` / `remove_decorator` / `add_service` / `remove_service` / `set_blackboard` / `set_property` |
| `nodeClass` | `string` |  | 节点类名（set_root/add_node/add_decorator/add_service） |
| `nodeName` | `string` |  | 显示名覆盖（可选） |
| `parentPath` | `string` |  | 从根起的点分子节点索引，如 '' 或 '0.1' |
| `childIndex` | `integer` |  | 子槽索引（add_node/move_node/装饰器/服务） |
| `targetPath` | `string` |  | 目标节点点分路径（remove_node/move_node/set_property） |
| `targetIndex` | `integer` |  | decorators[]/services[] 中要删改的索引 |
| `blackboardPath` | `string` |  | BlackboardData 资产路径（set_blackboard） |
| `targetType` | `string (enum)` |  | set_property 的目标类型 枚举值：`node` / `decorator` / `service` |
| `propertyName` | `string` |  | 要设置的 UPROPERTY 名（set_property） |
| `propertyValue` | `string` |  | 文本值，ImportText 格式（set_property） |

**相关 Capability**：`get_asset_behavior_tree`、`manage_asset_blackboard`、`save_asset`

---

### `manage_asset_blackboard`

批量编辑黑板键，支持增删/重命名键或修改父黑板；需单独调用 `save_asset` 保存。

**适用场景**：写操作：增删/重命名黑板键，修改父黑板

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `action` | `string (enum)` | ★ | 键操作 枚举值：`add` / `remove` / `rename` / `set_parent` |
| `keyName` | `string` |  | 键名（set_parent 不需要） |
| `keyType` | `string (enum)` |  | 键类型（仅 add） 枚举值：`bool` / `float` / `int` / `string` / `name` / `vector` / `rotator` / `object` / `class` / `enum` |
| `newName` | `string` |  | 新键名（仅 rename） |
| `parentPath` | `string` |  | 父 BlackboardData 路径（仅 set_parent，空则清除） |

**相关 Capability**：`get_asset_blackboard`、`create_asset_blackboard`、`save_asset`

---

### `manage_asset_eqs`

编辑 EQS：add_option/remove_option/set_generator/add_test/remove_test。

**适用场景**：往 EQS 里添加/删除 Option、设置 Generator、添加/删除 Test

| 参数 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `assetPath` | `string` | ★ | EnvQuery 资产路径 |

**相关 Capability**：`get_asset_eqs`、`create_asset_eqs`

---
