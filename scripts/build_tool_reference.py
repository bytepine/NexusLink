#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright byteyang. All Rights Reserved.
"""
build_tool_reference.py — 从 Capabilities 源码和元工具源码半自动生成 docs/tool-reference.md。

用法：
  # NexusLink 独立仓（推荐）
  py scripts/build_tool_reference.py [--live URL]

  # NexusWork 工作区
  py nexus-unreal/Script/build_tool_reference.py [--live URL]

  --repo-root  可选；含 NexusLink.uplugin 的目录（默认从脚本位置自动探测）
  --live URL   （可选）对运行中的 UE MCP 端点调 search_capabilities 补全 schema
               示例：--live http://127.0.0.1:45000/stream

输出：
  docs/tool-reference.md = tool-reference.header.md + AUTO-GENERATED 段

  AUTO-GENERATED 段由脚本生成，禁止手工编辑；
  手工维护内容在 docs/tool-reference.header.md（通用约定、引言等）。

规则：
  - 改 Capability schema 后须重跑本脚本，不要手工改生成段
  - 从 `FNexusSchema::Object()…Build()` 链解析参数；多 section cap 勿用 `.Required({})` 处截断
  - 首次 --live 补全后 schema 才完整（CI 无 UE 时跳过，已尽力从源码提取）
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

# ── 目录分类映射（Capabilities 子目录 → 文档分类名） ────────────────────────────

DIR_CATEGORY: dict[str, str] = {
    "AI":        "AI 工具",
    "Animation": "动画资产工具",
    "Blueprint": "蓝图工具",
    "DataAsset": "数据资产工具（DataAsset / DataTable）",
    "Editor":    "编辑器工具（Editor）",
    "Lua":       "Lua 运行时工具",
    "Material":  "材质工具（Material）",
    "Runtime":   "运行时工具（Runtime）",
    "Struct":    "结构体工具（Struct）",
    "UMG":       "控件蓝图工具（Widget）",
    "Asset":     "通用资产工具",
}

# 文档章节展示顺序
CATEGORY_ORDER = [
    "元工具（Meta）",
    "编辑器工具（Editor）",
    "通用资产工具",
    "蓝图工具",
    "动画资产工具",
    "材质工具（Material）",
    "结构体工具（Struct）",
    "数据资产工具（DataAsset / DataTable）",
    "控件蓝图工具（Widget）",
    "Lua 运行时工具",
    "运行时工具（Runtime）",
    "AI 工具",
]

# ── 中文描述覆写表（key = capability/tool 名称） ──────────────────────────────
# C++ Out.Description / Out.WhenToUse / Schema 参数描述均为中文，直接供 MCP 与文档使用；
# ZH_DESCRIPTIONS / ZH_WHEN_TO_USE 可选提供更详细的中文文档覆写。

ZH_DESCRIPTIONS: dict[str, str] = {
    # Meta
    "call_capability":    "执行 Capability（在 search_asset / get_asset_* 之后）。失败看 errorKind：unknown/disabled/arg_invalid；disabled 勿重试。旧名（如 create_blackboard）自动映射规范名。批量 calls[] 与单条不可混用。",
    "search_capabilities": "**首要入口** — 回答任何蓝图/Widget/材质/资产问题前应先调用。已知名称优先 `capabilityName=<精确名>`；`query` 用 1-2 词 AND 匹配。失败看 `errorKind`：`not_found`（不存在）/ `disabled`（设置已禁用）/ `disabled_only`（仅禁用 cap 命中，见 `disabledCapabilities[]`）；`query=get_asset` 零命中时 `hint` 会指向 `get_asset_<类型>` 路由。匹配 ≤2 返回完整 `parameters[]`。",
    "submit_feedback":    "上报 Capability/工具的使用摩擦，帮助改进搜索和 Schema。触发时机：重试 ≥2 次仍无进展、找不到合适的 Capability、Schema 字段含义需要猜测、被迫串行调用 ≥3 次。`category` 可取：`wrong_tool` / `misuse` / `schema_guess` / `search_zero` / `search_overflow` / `other`。优先填结构化字段（`attemptedArgs`、`actualError`、`expectedField`），少写长 `note`。",
    # Editor
    "capture_viewport":   "截图编辑器面板、PIE 视口或指定的 Actor/UMG Widget。`validateOnly=true` 不写图片，仅验通路。",
    "control_pie":        "启动、停止或查询 PIE 状态。action 可取：`start` / `stop` / `status`；mode 可取：`viewport` / `simulate`。",
    "exec_command":       "执行 UE 控制台命令并捕获输出。`silent=true` 可跳过捕获；支持所有 World。",
    "get_editor_info":    "返回 UE 版本、项目名、平台和构建配置。无参数；响应快，随时可用。",
    "get_editor_context":     "只读编辑器上下文：选中 Actor/资产、Content Browser 路径；`sections` 可选 selection_actors/selection_assets/content_browser_path；editor World ≠ PIE。",
    "search_console_variables": "按子串搜索控制台变量名（只读，含当前值）；不修改 CVar。",
    "get_gameplay_tags":  "检查 GAS 标签树或指定 Actor/资产的标签容器。sections 可选：`hierarchy`（标签树）/ `actor`（Actor 标签）/ `asset`（资产标签）。",
    "get_output_log":     "读取 UE 控制台缓冲区。支持按 `category` / `verbosity` / `text` 过滤；`offset`+`limit` 分页。",
    "set_log_capture_filter": "配置哪些日志分类写入缓冲区。传空数组表示全部；影响 `get_output_log` 的查询范围。",
    # Asset 通用
    "delete_asset":       "永久删除单个资产包。尽力清理重定向器；仅限编辑器，操作不可逆。",
    "duplicate_asset":    "将编辑器资产复制到新路径。支持任意资产类型；源资产保持不变。",
    "get_asset_refs":     "查找包的依赖项或引用方。`direction` 可取：`dependencies`（依赖项）/ `referencers`（被引用方）；可选递归查找。",
    "rename_asset":       "将编辑器资产移动或重命名到新路径。引擎自动生成重定向器以修复断开的引用。",
    "export_asset":       "将编辑器资产导出到磁盘文件（Fbx/Stl 等，依资产类型与 UE 导出器）。",
    "reimport_asset":     "从源文件重新导入资产，刷新已修改的外部资源。",
    "save_asset":         "将一个资产包持久化到磁盘。经 `SaveDirtyPackage` 先 `MarkPackageDirty` 再落盘；Live Coding 开启时仅标脏并返回 `deferred=true`。",
    "search_asset":       "查找资产路径。**必须先调用**；须指定 `assetType` 和功能级 `pathFilter`；禁止猜测 `/Game/...` 路径。`assetType` 支持别名归一化（如 `Blueprints`→`blueprint`、`Widgets`→`widget`、`ga`/`ge`→GAS 类型）。返回名称+路径列表。",
    "get_asset_blueprint":    "从编辑器读取 BP 结构。**回答蓝图问题前必须先调用**；禁止从源码推断。sections 可选 variable/function/component/graph 等。",
    # Blueprint
    "create_asset_blueprint": "以 UObject 子类为父类创建新 BP 资产，自动编译；用 manage 添加变量/节点/连线。",
    "manage_asset_blueprint": "编辑 BP：图/变量/节点/连线、SCS 组件树、CDO 默认值。SCS/defaults 仅限 Actor BP。操作后记得保存。",
    # Animation
    "create_asset_anim_blueprint": "为指定骨骼创建新 ABP 文件，自动关联骨骼；使用 `manage_asset_anim_blueprint` 填充状态机。",
    "create_asset_anim_montage":   "为指定骨骼创建新 Montage 文件；使用 `manage_asset_anim_montage` 添加片段填充内容。",
    "get_asset_anim_blueprint":    "检查 ABP 结构。sections=variables|statemachines|defaults|graphOverview；仅限编辑器使用。",
    "get_asset_texture":           "检查 Texture2D 尺寸、像素格式、压缩、sRGB、LOD；只读。",
    "get_asset_static_mesh":       "检查 StaticMesh LOD、包围盒、材质槽与碰撞摘要；只读。",
    "compile_blueprint":           "显式编译 Blueprint/ABP/WBP；可选 saveToDisk 落盘。",
    "get_asset_anim_montage":      "检查 Montage 时间轴快照（槽位/片段/分段）；只读，不触发运行时播放。",
    "get_asset_anim_sequence":     "检查 AnimSequence 时长、帧率、帧数、骨骼引用与 `notifies[]` 列表；只读。",
    "get_asset_skeletal_mesh":     "检查 SkeletalMesh LOD、材质槽、骨骼与 PhysicsAsset 摘要；只读。",
    "get_asset_skeleton":          "检查 Skeleton 骨骼树（分页）与 Socket 摘要；只读。",
    "get_asset_sound_wave":        "检查 SoundWave 时长、采样率与声道；只读。",
    "get_asset_sound_cue":         "检查 SoundCue 时长与 SoundNode 图摘要；只读。",
    "get_asset_niagara_system":    "检查 NiagaraSystem 发射器与用户参数；只读（需 `WITH_NIAGARA`）。",
    "get_asset_level":             "检查磁盘关卡（UWorld 包）Actor 列表与 WorldSettings；`editor_only`。",
    "manage_asset_level":          "编辑关卡 WorldSettings 与磁盘 Actor：`set_property` / `spawn_actor` / `remove_actor` / `set_actor_property`；`editor_only`。",
    "manage_asset_texture":        "编辑 Texture2D 属性：`action=set_property`（压缩/sRGB/LODGroup 等）；改后须 `save_asset`。",
    "manage_asset_static_mesh":    "编辑 StaticMesh：`set_material_slot` / `set_property`；改后须 `save_asset`。",
    "manage_asset_skeletal_mesh":  "编辑 SkeletalMesh：`set_material_slot` / `set_property`；改后须 `save_asset`。",
    "manage_asset_skeleton":       "编辑 Skeleton Socket：`add_socket` / `remove_socket` / `modify_socket`。",
    "manage_asset_sound_wave":     "编辑 SoundWave 属性：`action=set_property`（音量/循环/衰减等）。",
    "manage_asset_sound_cue":      "编辑 SoundCue：`set_property` / `add_node` / `remove_node` / `connect_nodes`；索引与 `get_asset_sound_cue` 一致。",
    "manage_asset_niagara_system": "编辑 Niagara 系统：`set_property` / `set_user_parameter`；不编辑 Emitter 节点图（需 `WITH_NIAGARA`）。",
    "get_runtime_actor_animation": "从运行中的骨骼网格体获取 AnimInstance 数据。支持 `state` / `slots` / `variables` 段；支持批量 Actor 查询。",
    "get_runtime_actor_ability_system": "PIE 读 Actor ASC 快照。`sections=abilities|effects|attributes`；写用 `interact_runtime_actor_ability_system`。",
    "interact_runtime_actor_ability_system": "运行时写 ASC：`activate_ability` / `cancel_ability` / `apply_effect` / `remove_effect` / `set_attribute`。",
    "interact_runtime_actor_animation": "命令式驱动运行时动画：`play_montage` / `stop_montage` / `stop_all` / `set_anim_variable`。",
    "interact_runtime_actor_behavior_tree": "运行时写 BT：`set_blackboard` / `restart_tree` / `stop_tree`；按 AIController 定位。",
    "manage_asset_anim_blueprint": "编辑 ABP 状态机结构，支持增删 `state_machine` / `state` / `transition` 节点；操作后须保存。",
    "manage_asset_anim_montage":   "编辑 Montage 结构，支持增删槽位、片段和分段；需单独调用 `save_asset` 保存。",
    "manage_asset_anim_sequence":  "编辑 AnimSequence：`add_notify` / `remove_notify` / `set_frame_rate` / `set_root_motion`；改后须 `save_asset`。",
    # Material
    "create_asset_material": "创建新的材质或材质实例文件。材质实例需传 `parentMaterial` 路径；`materialDomain` 与 `type` 须保持一致。",
    "get_asset_material":    "检查 Mat/MI/MF 节点图和参数。支持 `overview` / `params` / `graph` 段；可按名称过滤并分页。",
    "manage_asset_material": "批量编辑材质/材质实例，支持 `set_param` / `add_node` / `connect` / `recompile` 操作；需先获取节点 ID 再操作。",
    # Struct
    "create_asset_struct":       "创建新的 UserDefinedStruct 文件，自动编译；使用 `manage_asset_struct_field` 添加字段。",
    "get_asset_struct":          "检查 UDS 字段定义。每个字段含 `name` / `type` / `subType` / `defaultValue`；支持 `propertyPaths` 过滤。",
    "manage_asset_struct_field": "批量编辑 UDS 字段，支持 `add` / `remove` / `modify`，携带类型、名称和默认值；修改后自动重新编译。",
    # DataAsset / DataTable
    "create_asset_data_asset": "创建新的类型化数据对象文件。需指定子类名；仅支持非抽象类。",
    "create_asset_data_table": "创建带行结构体的新数据表文件；使用 `manage_asset_data_table` 填充行数据。",
    "get_asset_data_asset":    "读取自定义数据对象的属性，可编辑字段含类型/当前值/是否继承等信息；支持路径过滤。",
    "get_asset_data_table":    "检查数据表行或 Schema。`mode=schema` 返回列定义，`mode=rows` 返回行值；支持 `propertyPaths` 过滤。",
    "manage_asset_data_asset": "批量编辑自定义数据对象属性。`set` 使用 ImportText 验证，`reset` 恢复为 CDO 默认值；`ops[]` 不能为空。",
    "manage_asset_data_table": "批量编辑数据表行，支持 `add` / `remove` / `set` rows[]；ImportText 验证；仅真实变更时才标记为已修改。",
    # Widget
    "create_asset_user_widget": "创建新的 WBP 文件。`parentClass` 设置 UI 基类；使用 `manage_asset_user_widget` 填充控件树。",
    "get_asset_user_widget":    "从编辑器读取 WBP 控件树与 UMG 动画。**回答 Widget/UMG 问题前必须先调用**；禁止从源码推断。sections 可选 widgets/animations。",
    "manage_asset_user_widget": "批量编辑 WBP 层级：`add` / `remove` / `set_slot` / `set_property`；操作后须 `save_asset`。",
    # Lua
    "dofile_runtime_lua":       "从 Content/Script/ 根目录加载并执行 .lua 文件，使用相对路径。需要 UnLua + PIE 运行中。",
    "eval_runtime_lua":         "在 PIE/Game 中执行任意 Lua 代码片段，返回压栈值；尽力完成 UE 环境初始化。",
    "gc_runtime_lua":           "控制 PIE 中 Lua 的 GC 周期。模式可取：`collect`（默认）/ `stop` / `restart` / `count`。",
    "get_asset_lua_binding":    "解析蓝图绑定的 UnLua 模块，返回 `bound`（已绑定）和 `fileExists`（文件存在）标志。需要 UnLua 插件。",
    "get_runtime_lua_env":      "枚举 `_G` 或指定路径的 Lua 嵌套表中的所有键，支持名称过滤和数量限制。",
    "get_runtime_lua_loaded":   "枚举 `package.loaded` 缓存中已加载的 Lua 模块列表，支持名称模式过滤。",
    "get_runtime_lua_memory":   "报告 Lua VM 堆内存使用量（KB 和字节），无需参数；配合 `gc_runtime_lua` 诊断内存泄漏。",
    "get_runtime_lua_metatable":"沿 `__index` 链遍历并转储指定点路径的 OOP 类表，用于 UnLua 继承链调试。",
    "get_runtime_lua_object":   "读取 UnLua 绑定的 Actor/UObject 实例级 Lua 表，通过指针在注册表中定位。",
    "get_runtime_lua_stack":    "转储 Lua 调用栈帧及局部变量/上值。按帧索引向下钻取；`detail` 可取：`locals` / `upvalues` / `all`。",
    "get_runtime_lua_value":    "按点路径读取单个 Lua 全局变量或嵌套字段，返回当前类型和值。",
    "hotreload_runtime_lua":    "热重载 UnLua 模块（2.x）。UnLua 1.x 不执行，返回 error。",
    "set_runtime_lua":          "为 Lua 全局变量或嵌套表字段赋值，使用点路径记法；支持 `string` / `number` / `bool` / `null` 类型。",
    # Runtime
    "destroy_runtime_widget":          "从视口移除并销毁运行时 UMG 面板；按 `widgetName` 定位。",
    "destroy_runtime_actor":         "从 PIE/Game 中移除指定的运行时场景实体，并将 Level 包标记为已修改。",
    "diff_runtime_actors":           "对比两个运行时 Actor 的属性差异。支持指定属性路径过滤或全量扫描；最多返回 50 条差异。",
    "get_runtime_actor_property":    "查询运行时场景对象的字段值，支持诊断预设、批量属性路径和组件树遍历。",
    "get_runtime_slate_widget":      "按十六进制地址检查原生 SWidget（地址来自 UE Widget Reflector），返回类型/可见性/子控件信息。",
    "get_runtime_widget_property":   "从指定名称的运行时 UMG 元素获取字段值，使用 `widgetName`+`ownerClass` 定位；支持批量属性路径或子控件查询。",
    "interact_runtime_widget":       "在运行时 UMG 元素上触发 UI 事件。`action` 可取：`click` / `check` / `toggle` / `set` / `read`；支持 Button / CheckBox / Slider / TextBlock / EditableText / ProgressBar。",
    "list_runtime_actors":           "枚举 PIE/Game 世界中的 Actor，支持按类/标签/名称过滤；返回 Actor 引用列表，不含具体属性值。",
    "list_runtime_widgets":          "枚举 PIE/Game 视口中的 UMG UserWidget 实例，支持按类型/名称/显示文本过滤。",
    "set_runtime_actor_property":    "批量修改运行时场景对象的可编辑字段，`updates` 数组中每项对应一个结果。",
    "set_runtime_widget_property":   "批量修改运行时 UMG 元素的字段；`updates[]` 每项含控件名、属性路径和目标值。",
    "spawn_runtime_actor":           "在 PIE 世界中实例化场景实体，接受 `blueprintPath` 或 `className`，位置/旋转可指定；自动处理碰撞偏移。",
    "spawn_runtime_widget":          "在 PIE/Game 视口中创建并显示 UMG 面板，接受 WidgetBlueprint 的资产路径和 `zOrder`。",
    # AI
    "create_asset_behavior_tree":       "创建新的行为树文件，初始为空。通过 `manage_asset_behavior_tree` 的 `set_blackboard` 关联黑板，之后再填充节点。",
    "create_asset_blackboard":          "创建无键的空黑板文件，通过 `manage_asset_behavior_tree` 的 `set_blackboard` 动作关联到行为树。",
    "get_asset_behavior_tree":          "检查行为树结构快照（含路径索引与节点/装饰器/服务属性）；只读，不做修改。",
    "get_asset_blackboard":             "检查黑板键定义，返回所有键的名称和类型快照；只读。",
    "get_runtime_actor_behavior_tree":  "查询运行中 AI 的当前活动行为树节点和黑板键值。目标为 AIController 或其控制的 Pawn。",
    "manage_asset_behavior_tree":       "批量编辑 BT 节点/装饰器/服务：`move_node` 与 `set_property` 等；改后刷新编辑器 BT 图。",
    "manage_asset_blackboard":          "批量编辑黑板键，支持增删/重命名键或修改父黑板；需单独调用 `save_asset` 保存。",
    # GAS（WITH_GAS=1）
    "create_asset_gameplay_ability":  "创建 GameplayAbility BP；语义字段用 `manage_asset_gameplay_ability`，Graph 用 `manage_asset_blueprint`。",
    "create_asset_gameplay_effect":   "创建 GameplayEffect BP；修改 Duration/Modifier/Tag 用 `manage_asset_gameplay_effect`。",
    "create_asset_attribute_set":     "创建 AttributeSet BP；默认值用 `manage_asset_attribute_set`，属性变量用 `manage_asset_blueprint`。",
    "get_asset_gameplay_ability":     "读 GA Blueprint CDO：`sections=metadata|tags|costs|graphOverview`；Graph 详情用 `get_asset_blueprint`。",
    "get_asset_gameplay_effect":      "读 GE Blueprint CDO：`sections=policy|modifiers|tags|cues`；只读。",
    "get_asset_attribute_set":        "读 AttributeSet CDO 全部 `FGameplayAttributeData` 默认值；只读。",
    "manage_asset_gameplay_ability":  "修改 GA CDO：`set_tags` / `set_policy` / `set_cost_cooldown`；Graph 编辑用 `manage_asset_blueprint`。",
    "manage_asset_gameplay_effect":   "批量修改 GE CDO：`set_policy` / `set_tags` / `add_modifier` / `remove_modifier` / `set_modifier`。",
    "manage_asset_attribute_set":     "批量 `set`/`reset` AttributeSet CDO 的 `FGameplayAttributeData` 默认值。",
}

ZH_WHEN_TO_USE: dict[str, str] = {
    # Editor
    "capture_viewport":          "截图编辑器/PIE/Actor/Widget；非 editor_desktop 勿滥用",
    "control_pie":               "启动/停止/查询 PIE；action=start|stop|status",
    "delete_asset":              "永久删除编辑器资产；不可逆，慎用特定包内批量",
    "duplicate_asset":           "复制编辑器资产到新路径；源资产不变",
    "exec_command":              "执行 UE 控制台命令并返回 output；LogEngine 走 GLog，其余走 LogConsole",
    "get_editor_info":           "读 UE 版本、项目名、平台与构建配置；无参数",
    "get_output_log":            "读 UE 输出日志；非 LogConsole（exec_command 负责）；category/verbosity/text 过滤",
    "rename_asset":              "移动或重命名资产；自动更新软引用与路径",
    "set_log_capture_filter":    "设置写入缓冲的日志类别；空=全部；影响 get_output_log",
    # Blueprint
    "create_asset_blueprint": "创建空白 BP；不用于编辑现有 BP",
    "get_asset_blueprint":    "用户问蓝图变量/Graph/函数 — 必须先调用，勿 grep 源码",
    "manage_asset_blueprint": "写操作：增删变量、图节点、连线",
    # Animation
    "create_asset_anim_blueprint": "创建空白 ABP；需要 skeletonPath",
    "create_asset_anim_montage":   "创建空白 Montage；需要 skeletonPath",
    "get_asset_anim_blueprint":    "读取 ABP 变量、状态机、默认值；不含写操作",
    "get_asset_texture":           "读 Texture2D 元数据；引用用 get_asset_refs",
    "get_asset_refs":              "查资产依赖/被引用；direction=dependencies|referencers，可选递归",
    "get_gameplay_tags":           "查 Tag 树/Actor/资产/referencers；sections 含 referencers",
    "get_asset_static_mesh":       "读 StaticMesh LOD/材质槽/碰撞；不含编辑",
    "compile_blueprint":           "manage 改图后显式编译；落盘用 saveToDisk 或 save_asset",
    "get_asset_anim_montage":      "读取 Montage 结构；运行时播放状态请使用 get_runtime_actor_animation",
    "get_asset_anim_sequence":     "读序列元数据与 notifies；Montage 用 get_asset_anim_montage",
    "get_asset_skeletal_mesh":     "读 SK 资产；骨骼树用 get_asset_skeleton",
    "get_asset_skeleton":          "读骨骼树/Socket；绑定网格用 get_asset_skeletal_mesh",
    "get_asset_sound_wave":        "读波形元数据；Cue 节点树用 get_asset_sound_cue",
    "get_asset_sound_cue":         "读 Cue 图摘要；波形用 get_asset_sound_wave",
    "get_asset_niagara_system":    "读 Niagara 系统元数据；不编辑节点图",
    "get_asset_level":             "读磁盘关卡布局；PIE 用 list_runtime_actors",
    "manage_asset_anim_blueprint": "写操作：增删状态机、状态、过渡",
    "manage_asset_anim_montage":   "写操作：增删 Montage 片段、分段、槽位",
    "manage_asset_anim_sequence":  "写操作：增删 AnimNotify、改帧率/根运动",
    "manage_asset_level":          "写操作：改 WorldSettings 或关卡磁盘 Actor",
    "manage_asset_texture":        "写操作：改 Texture 压缩/sRGB/LODGroup",
    "manage_asset_static_mesh":    "写操作：改 StaticMesh 材质槽/属性",
    "manage_asset_skeletal_mesh":  "写操作：改 SkeletalMesh 材质槽/属性",
    "manage_asset_skeleton":       "写操作：增删改 Skeleton Socket",
    "manage_asset_sound_wave":     "写操作：改 SoundWave 音量/循环/衰减",
    "manage_asset_sound_cue":      "写操作：改 Cue 属性或节点图",
    "manage_asset_niagara_system": "写操作：改 Niagara 属性或用户参数",
    # Material
    "create_asset_material": "创建空白 Material 或 MaterialInstance 资产",
    "get_asset_material":    "读取节点图、参数、连线；不含编辑操作",
    "manage_asset_material": "写操作：设置参数、增删节点、连接连线、重新编译",
    # Struct
    "create_asset_struct":       "创建空白 UserDefinedStruct；用 manage 添加字段",
    "get_asset_struct":          "读取 UDS 字段定义；不含编辑操作",
    "manage_asset_struct_field": "写操作：增删/修改 UDS 字段",
    # DataAsset / DataTable
    "create_asset_data_asset": "创建 DataAsset；parentClass 默认为 PrimaryDataAsset",
    "create_asset_data_table": "创建空白数据表；需要 rowStructName",
    "get_asset_data_asset":    "读取 DataAsset 属性；不含编辑操作",
    "get_asset_data_table":    "读取数据表 Schema 或行值；不含编辑操作",
    "manage_asset_data_asset": "写操作：设置或重置 DataAsset 属性为 CDO 默认值",
    "manage_asset_data_table": "写操作：增删/设置数据表行值",
    # Widget
    "create_asset_user_widget": "创建空白 WBP；parentClass 可选（默认为 UserWidget）",
    "get_asset_user_widget":    "用户问控件树/UMG 动画 — 必须先调用，勿 grep 源码",
    "manage_asset_user_widget": "写操作：增删控件、改 Slot/属性",
    # Lua
    "get_asset_lua_binding":  "读取/编辑前先找到 Lua 文件路径",
    "dofile_runtime_lua":     "从 Content/Script/ 加载执行 .lua；需路径与 UnLua+PIE",
    "eval_runtime_lua":       "在 PIE/Game 执行 Lua 片段；返回压栈值",
    "gc_runtime_lua":         "控制 PIE 内 Lua GC；mode=collect|stop|restart|count",
    "get_runtime_lua_env":    "浏览所有键（用 env）或读取单个键值（用 value）",
    "get_runtime_lua_loaded": "枚举 package.loaded 已加载模块；支持名称过滤",
    "get_runtime_lua_memory": "在 gc_collect 前后检查堆内存大小",
    "get_runtime_lua_metatable": "查 __index 链追溯 OOP 类；查 UnLua 继承与属性",
    "get_runtime_lua_object": "查 UnLua 绑 Actor/UObject 的实例 Lua 表",
    "get_runtime_lua_stack":  "转储 Lua 调用栈；局部/上值；detail=locals|upvalues|all",
    "get_runtime_lua_value":  "读取单个键值（用此工具）vs 浏览所有键（用 env）",
    "hotreload_runtime_lua":  "热重载 UnLua 模块（2.x）；需运行中 PIE；UnLua 1.x 不执行热重载会 error",
    "set_runtime_lua":        "为 Lua 全局或嵌套字段赋值；路径含 string/number/bool/null",
    # Runtime
    "destroy_runtime_actor":           "从 PIE/Game 移除运行时 Actor；非 Level 盘修改",
    "diff_runtime_actors":             "对比两个运行时 Actor 属性差异；最多 50 项；propertyPaths 过滤",
    "get_runtime_actor_animation":     "从运行中 Pawn 读 AnimInstance；sections=state|slots|variables",
    "get_runtime_actor_behavior_tree": "读运行中 AI 的 BT 节点与 BB 值；写黑板/重启用 interact",
    "get_runtime_actor_property":  "只读字段，不做修改",
    "get_runtime_slate_widget":    "持有来自 Widget Reflector 的十六进制地址时使用",
    "get_runtime_widget_property": "只读 UMG 字段，不做修改",
    "interact_runtime_widget":     "触发运行时 UMG 事件；action=click|check|toggle|set|read",
    "list_runtime_actors":         "枚举 PIE/Game 中 Actor；类/标签/名称过滤；不含属性值",
    "list_runtime_widgets":        "枚举 PIE/Game 视口 UMG 实例；类/名/displayText 过滤",
    "set_runtime_actor_property":  "运行时修改 Actor 的实时字段",
    "set_runtime_widget_property": "运行时修改 UMG 元素的实时字段",
    "spawn_runtime_actor":         "在 PIE 实例化 Actor；blueprintPath 或 className；可设位置/旋转",
    "spawn_runtime_widget":        "在 PIE/Game 视口创建显示 UMG 面板；assetPath+zOrder",
    "destroy_runtime_widget":      "销毁运行时 UMG 面板",
    "get_runtime_actor_ability_system": "PIE 读 ASC 快照",
    "interact_runtime_actor_ability_system": "PIE 施放技能/Apply GE/改属性",
    "interact_runtime_actor_animation": "PIE 播放/停止蒙太奇或写 Anim 变量",
    "interact_runtime_actor_behavior_tree": "PIE 写黑板/重启或停止 BT",
    "export_asset":                "导出资产到磁盘文件",
    "reimport_asset":              "从源文件重新导入资产",
    "get_editor_context":          "读编辑器选中与 Content Browser 路径",
    "search_console_variables":    "搜索控制台变量名（只读）",
    "save_asset":                  "落盘脏包；Live Coding 时可能 deferred",
    "search_asset":                "先 search 再 get/manage；禁止猜路径",
    # AI
    "create_asset_behavior_tree":  "创建空白行为树；无节点，尚未关联黑板",
    "create_asset_blackboard":     "创建空白黑板；用 manage_asset_blackboard 添加键",
    "get_asset_behavior_tree":     "读取 BT 路径索引、装饰器参数与节点属性",
    "get_asset_blackboard":        "读取黑板键列表；运行时黑板值请使用 get_runtime_actor_behavior_tree",
    "manage_asset_behavior_tree":  "写操作：增删/移动节点、装饰器、服务，设置属性",
    "manage_asset_blackboard":     "写操作：增删/重命名黑板键，修改父黑板",
    "create_asset_gameplay_ability": "创建空白 GA BP",
    "create_asset_gameplay_effect":  "创建空白 GE BP",
    "create_asset_attribute_set":    "创建空白 AttributeSet BP",
    "get_asset_gameplay_ability":    "读 GA CDO 元数据/Tag/消耗",
    "get_asset_gameplay_effect":     "读 GE CDO 策略/Modifier/Tag",
    "get_asset_attribute_set":       "读 AttributeSet CDO 默认值",
    "manage_asset_gameplay_ability": "写 GA CDO 策略/Tag/Cost",
    "manage_asset_gameplay_effect":  "写 GE Modifier/Duration/Tag",
    "manage_asset_attribute_set":    "写 AttributeSet CDO 默认值",
}

# ── Regex 模式 ─────────────────────────────────────────────────────────────────

RE_NAME         = re.compile(r'Out\.Name\s*=\s*TEXT\("([^"]+)"\)')
RE_DESC         = re.compile(r'Out\.Description\s*=\s*TEXT\("([^"]+)"\)')
RE_RELATED      = re.compile(r'Out\.RelatedCapabilities\s*=\s*\{([^}]+)\}')
RE_PREREQ       = re.compile(r'Out\.Prerequisites\s*=\s*\{([^}]+)\}')
RE_WHEN         = re.compile(r'Out\.WhenToUse\s*=\s*TEXT\("([^"]+)"\)')
RE_TAG_ACCESS   = re.compile(r'FNexusMcpTags::(Readonly|Write)\b')
RE_TEXT_VALUES  = re.compile(r'TEXT\("([^"]+)"\)')
RE_USE_SECTIONS = re.compile(r'BuildSchemaWithSections\(\)')

# 单行 .Prop / .Required（Str / Int 描述在同行 TEXT 内）
RE_PROP_INLINE_STR = re.compile(
    r'\.(Prop|Required)\s*\(\s*TEXT\("([^"]+)"\)\s*,\s*FNexusSchema::Str(?:Arr)?\s*\(\s*TEXT\("((?:[^"\\]|\\.)*)"\)',
)
RE_PROP_INLINE_INT = re.compile(
    r'\.(Prop|Required)\s*\(\s*TEXT\("([^"]+)"\)\s*,\s*FNexusSchema::Int\s*\(\s*TEXT\("((?:[^"\\]|\\.)*)"\)',
)
RE_PROP_INLINE_ENUM_START = re.compile(
    r'\.(Prop|Required)\s*\(\s*TEXT\("([^"]+)"\)\s*,\s*FNexusSchema::Enum\s*\(\s*TEXT\("((?:[^"\\]|\\.)*)"\)',
)
# .Required({TEXT("a"), TEXT("b")})
RE_REQUIRED_LIST = re.compile(r'\.Required\s*\(\s*\{([^}]+)\}\s*\)')
# FNexusSchema::Enum(TEXT("label"), {TEXT("v1"), TEXT("v2"), ...})
RE_ENUM_BLOCK   = re.compile(
    r'FNexusSchema::Enum\s*\(\s*TEXT\("([^"]*)"\)\s*,\s*\{([^}]+)\}',
)
# GetSectionNames return { TEXT("a"), ... }
RE_SECTION_RETURN = re.compile(
    r'GetSectionNames\(\)\s*(?:const\s*)?\{.*?return\s*\{([^}]+)\}',
    re.DOTALL,
)
# BuildCapabilitySchema / Out.InputSchema 中 FNexusSchema::Object()…Build() 链（避免 .Required({}) 的 } 截断）
RE_SCHEMA_OBJECT_CHAIN = re.compile(
    r'FNexusSchema::Object\(\)(.*?)\.Build\(\s*\)',
    re.DOTALL,
)


def extract_text_values(raw: str) -> list[str]:
    return RE_TEXT_VALUES.findall(raw)


def extract_schema_object_chain(text: str, anchor: str | None = None) -> str:
    """
    从 C++ 源码提取 FNexusSchema::Object()…Build() 之间的链式调用文本。
    anchor 为 'BuildCapabilitySchema' 或 'InputSchema' 时仅在对应函数/赋值块内搜索。
    """
    search_text = text
    if anchor == "BuildCapabilitySchema":
        m = re.search(
            r"BuildCapabilitySchema\s*\(\s*\)\s*const\s*\{",
            text,
        )
        if m:
            search_text = text[m.start() :]
    elif anchor == "InputSchema":
        m = re.search(r"Out\.InputSchema\s*=", text)
        if m:
            search_text = text[m.start() :]

    m_chain = RE_SCHEMA_OBJECT_CHAIN.search(search_text)
    return m_chain.group(1) if m_chain else ""


# 参数说明兜底（C++ 未写描述或解析失败时）
COMMON_PARAM_DESCRIPTIONS: dict[str, str] = {
    "assetPath":       "资产包路径（须先 `search_asset` 取得，格式 `/Game/...`）",
    "assetPaths":      "资产包路径数组（批量）",
    "sections":        "查询段（可多选）；见各 cap 支持的 section 列表",
    "propertyPaths":   "反射属性路径（点分，如 `Health` / `Mesh.RelativeLocation`）",
    "actorName":       "运行时 Actor 名称（PIE 世界中 `GetName()`）",
    "actorNames":      "Actor 名称数组（批量）",
    "classFilter":     "类名过滤（子串/通配，可选）",
    "nameFilter":      "名称或标签过滤（可选）",
    "tagFilter":       "Actor Tag 精确匹配（可选）",
    "pathFilter":      "Content 路径前缀（如 `/Game/Feature/`）",
    "assetType":       "资产类型（如 Blueprint、Widget、World）；禁止 `all` 扫全库",
    "query":           "搜索关键词（1–2 词，AND 匹配）",
    "offset":          "分页偏移（从 0 起）",
    "limit":           "每页条数上限",
    "capability":      "Capability 精确名称",
    "capabilityName":  "Capability 精确名称（`search_capabilities` 短路）",
    "arguments":       "传给 Capability 的 JSON 对象（须嵌套，勿摊平到顶层）",
    "calls":           "批量调用列表：`[{capability, arguments}, ...]`",
    "action":          "操作名（见各 cap 文档枚举）",
    "mode":            "模式（见各 cap 文档枚举）",
    "direction":       "依赖方向：`dependencies` / `referencers`",
    "category":        "反馈或日志分类",
    "note":            "补充说明（可选）",
}


def infer_category(cpp_path: Path) -> str:
    """
    从 Capability cpp 文件路径推断文档分类。
    规则：Capabilities/ 的直接子目录优先级最高（如 Lua/、Asset/），
    次深子目录仅在直接子目录无独立语义时才提升（如 Asset/Blueprint → 蓝图工具）。
    """
    parts = cpp_path.parts
    for i, p in enumerate(parts):
        if p == "Capabilities" and i + 1 < len(parts):
            top_sub = parts[i + 1]
            # Lua/ 下所有内容均属 Lua 运行时工具，不被其子目录 Runtime/ 覆盖
            if top_sub == "Lua":
                return DIR_CATEGORY["Lua"]
            # 其余情况：最深子目录（最具体）优先
            for seg in reversed(parts[i + 1 : -1]):
                if seg in DIR_CATEGORY:
                    return DIR_CATEGORY[seg]
            return DIR_CATEGORY.get(top_sub, "通用资产工具")
    return "通用资产工具"


def parse_schema_block(text: str) -> tuple[list[dict[str, Any]], set[str]]:
    """
    从 C++ schema 文本片段中提取 (params_list, required_set)。
    params_list 每项为 {"name", "type", "description", ["enum"]}。
    """
    params: list[dict[str, Any]] = []
    required_fields: set[str] = set()

    # 先收集 .Required({...}) 列表
    for m in RE_REQUIRED_LIST.finditer(text):
        for v in extract_text_values(m.group(1)):
            required_fields.add(v)

    # 预收集文本中所有 Enum 块（label → values），供后续 Prop 行关联
    enum_map: dict[str, list[str]] = {}
    for m in RE_ENUM_BLOCK.finditer(text):
        label = m.group(1)
        vals  = extract_text_values(m.group(2))
        if vals:
            enum_map[label] = vals

    def _add_param(method: str, pname: str, schema_type: str, desc: str, enum_vals: list[str] | None = None) -> None:
        if pname in seen:
            return
        seen.add(pname)
        if method == "Required":
            required_fields.add(pname)
        p: dict[str, Any] = {
            "name":        pname,
            "type":        _map_type(schema_type),
            "description": desc,
        }
        if schema_type == "Enum" or enum_vals:
            p["type"] = "string (enum)"
            if enum_vals:
                p["enum"] = enum_vals
        params.append(p)

    seen: set[str] = set()
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        m_str = RE_PROP_INLINE_STR.search(line)
        if m_str:
            _add_param(m_str.group(1), m_str.group(2), "Str", m_str.group(3))
            i += 1
            continue
        m_int = RE_PROP_INLINE_INT.search(line)
        if m_int:
            _add_param(m_int.group(1), m_int.group(2), "Int", m_int.group(3))
            i += 1
            continue
        m_enum = RE_PROP_INLINE_ENUM_START.search(line)
        if m_enum:
            method, pname, desc = m_enum.group(1), m_enum.group(2), m_enum.group(3)
            # Enum 值列表可能跨多行，直到含 `}` 的闭合
            block = line[m_enum.end() :]
            j = i + 1
            while j < len(lines) and "}" not in block:
                block += "\n" + lines[j]
                j += 1
            vals = extract_text_values(block)
            _add_param(method, pname, "Enum", desc, vals or None)
            i = j
            continue
        i += 1

    return params, required_fields


def _map_type(schema_type: str) -> str:
    return {
        "Str":       "string",
        "StrArr":    "string[]",
        "Num":       "number",
        "Int":       "integer",
        "Bool":      "boolean",
        "Enum":      "string (enum)",
        "AnyObject": "object",
        "ArrayOf":   "array",
        "Array":     "array",
    }.get(schema_type, "string")


def parse_section_names(text: str) -> list[str]:
    """从 GetSectionNames() 方法体提取 section 枚举值"""
    m = RE_SECTION_RETURN.search(text)
    if m:
        return extract_text_values(m.group(1))
    return []


def parse_capability(cpp_path: Path) -> dict[str, Any] | None:
    """解析单个 Capability cpp 文件，返回 capability dict 或 None"""
    text = cpp_path.read_text(encoding="utf-8", errors="replace")

    m_name = RE_NAME.search(text)
    m_desc = RE_DESC.search(text)
    if not m_name or not m_desc:
        return None

    cap: dict[str, Any] = {
        "name":         m_name.group(1),
        "description":  m_desc.group(1),
        "category":     infer_category(cpp_path),
        "file":         str(cpp_path),
        "params":       [],
        "required":     set(),
        "related":      [],
        "prerequisites":[],
        "sections":     [],
        "access":       "readonly",
        "when_to_use":  "",
    }

    # 读写属性
    access_tags = RE_TAG_ACCESS.findall(text)
    if "Write" in access_tags:
        cap["access"] = "write"

    m_rel = RE_RELATED.search(text)
    if m_rel:
        cap["related"] = extract_text_values(m_rel.group(1))

    m_pre = RE_PREREQ.search(text)
    if m_pre:
        cap["prerequisites"] = extract_text_values(m_pre.group(1))

    m_when = RE_WHEN.search(text)
    if m_when:
        cap["when_to_use"] = m_when.group(1)

    # Schema 解析（Object()…Build() 链，避免 .Required({}) 导致早停）
    if RE_USE_SECTIONS.search(text):
        schema_text = extract_schema_object_chain(text, "BuildCapabilitySchema")
        if schema_text:
            params, req = parse_schema_block(schema_text)
            cap["params"]   = params
            cap["required"] = req
        cap["sections"] = parse_section_names(text)
    else:
        schema_text = extract_schema_object_chain(text, "InputSchema")
        if not schema_text:
            schema_text = extract_schema_object_chain(text)
        if schema_text:
            params, req = parse_schema_block(schema_text)
            cap["params"]   = params
            cap["required"] = req

    return cap


def parse_meta_tool(cpp_path: Path) -> dict[str, Any] | None:
    """解析元工具（MCP Tool）cpp 文件"""
    text = cpp_path.read_text(encoding="utf-8", errors="replace")

    m_name = RE_NAME.search(text)
    m_desc = RE_DESC.search(text)
    if not m_name or not m_desc:
        return None

    tool: dict[str, Any] = {
        "name":         m_name.group(1),
        "description":  m_desc.group(1),
        "category":     "元工具（Meta）",
        "file":         str(cpp_path),
        "params":       [],
        "required":     set(),
        "related":      [],
        "prerequisites":[],
        "sections":     [],
        "access":       "write",
        "when_to_use":  "",
    }

    schema_text = extract_schema_object_chain(text, "InputSchema")
    if schema_text:
        params, req = parse_schema_block(schema_text)
        tool["params"]   = params
        tool["required"] = req

    return tool


def render_cap_section(cap: dict[str, Any]) -> str:
    name = cap["name"]
    desc = ZH_DESCRIPTIONS.get(name) or cap["description"]
    lines = [f'### `{name}`\n', f'{desc}\n']

    if cap.get("prerequisites"):
        prereq_str = " / ".join(f"`{p}`" for p in cap["prerequisites"])
        lines.append(f'**前置条件**：{prereq_str}\n')

    when = ZH_WHEN_TO_USE.get(name) or cap.get("when_to_use", "")
    if when:
        lines.append(f'**适用场景**：{when}\n')

    # 构建参数展示列表
    required_set: set[str] = cap.get("required", set())
    params_display: list[dict[str, Any]] = []

    # sections 参数（多 section cap 自动注入）
    sections: list[str] = cap.get("sections", [])
    if sections:
        sec_vals = " / ".join(f'`{s}`' for s in sections)
        params_display.append({
            "name":        "sections",
            "type":        "string[]",
            "required":    False,
            "description": f"查询段（可多选）：{sec_vals}",
        })

    for p in cap.get("params", []):
        params_display.append({
            **p,
            "required": p["name"] in required_set,
        })

    if params_display:
        lines.append("| 参数 | 类型 | 必填 | 说明 |")
        lines.append("|------|------|:----:|------|")
        for p in params_display:
            req_mark = "★" if p.get("required") else ""
            typ = p.get("type", "string")
            desc_p = (p.get("description") or "").strip()
            if not desc_p:
                desc_p = COMMON_PARAM_DESCRIPTIONS.get(p["name"], "")
            if "enum" in p:
                enum_vals = " / ".join(f'`{v}`' for v in p["enum"])
                desc_p = f'{desc_p} 枚举值：{enum_vals}' if desc_p else f'枚举值：{enum_vals}'
            lines.append(f'| `{p["name"]}` | `{typ}` | {req_mark} | {desc_p} |')
        lines.append("")

    if cap.get("related"):
        rel_str = "、".join(f'`{r}`' for r in cap["related"])
        lines.append(f'**相关 Capability**：{rel_str}\n')

    return "\n".join(lines)


def render_markdown(categories: dict[str, list[dict]], header: str) -> str:
    cap_count  = sum(len(v) for k, v in categories.items() if k != "元工具（Meta）")
    tool_count = len(categories.get("元工具（Meta）", []))

    parts: list[str] = [header.rstrip(), ""]
    parts.append("<!-- 自动生成，由 build_tool_reference.py 产出；以下内容请勿手工修改 -->")
    parts.append(f"<!-- 共 {cap_count} 个 Capability + {tool_count} 个元工具 -->")
    parts.append("")

    # 目录
    parts.append("## 目录\n")
    for cat in CATEGORY_ORDER:
        if cat not in categories:
            continue
        # GitHub Markdown anchor 规则：小写、去特殊字符、空格→连字符
        anchor = re.sub(r"[（）/\s]+", "-", cat.lower()).strip("-")
        anchor = re.sub(r"-+", "-", anchor)
        parts.append(f"- [{cat}](#{anchor})")
    parts.append("")
    parts.append("---")
    parts.append("")

    for cat in CATEGORY_ORDER:
        if cat not in categories:
            continue
        caps = sorted(categories[cat], key=lambda c: c["name"])
        parts.append(f"## {cat}\n")
        for cap in caps:
            parts.append(render_cap_section(cap))
            parts.append("---")
            parts.append("")

    return "\n".join(parts)


# ── live schema 补全（可选，需要运行中的 UE） ──────────────────────────────────

def load_live_schema(url: str, cap_name: str) -> list[dict] | None:
    try:
        import httpx  # type: ignore
    except ImportError:
        return None

    try:
        # initialize
        r = httpx.post(
            url,
            json={
                "jsonrpc": "2.0", "id": 1, "method": "initialize",
                "params": {
                    "protocolVersion": "2025-06-18",
                    "clientInfo": {"name": "build_tool_reference", "version": "1.0"},
                    "capabilities": {},
                },
            },
            timeout=10,
        )
        r.raise_for_status()
        session_id = r.headers.get("Mcp-Session-Id", "")
        headers = {"Mcp-Session-Id": session_id} if session_id else {}

        # search_capabilities
        r2 = httpx.post(
            url, headers=headers,
            json={
                "jsonrpc": "2.0", "id": 2, "method": "tools/call",
                "params": {
                    "name": "search_capabilities",
                    "arguments": {"capabilityName": cap_name},
                },
            },
            timeout=15,
        )
        r2.raise_for_status()
        result = r2.json().get("result", {})
        content = result.get("content", [])
        if content:
            text_item = next((c for c in content if c.get("type") == "text"), None)
            if text_item:
                data = json.loads(text_item.get("text", "{}"))
                return data.get("capability", {}).get("parameters")
    except Exception as e:
        print(f"  [live] {cap_name}: {e}", file=sys.stderr)

    return None


def find_nexuslink_root(start: Path) -> Path:
    """定位含 NexusLink.uplugin 的插件仓根目录。"""
    for base in (start, start.parent, start.parent.parent):
        if (base / "NexusLink.uplugin").is_file():
            return base
        nested = base / "Plugins" / "Developer" / "NexusLink"
        if (nested / "NexusLink.uplugin").is_file():
            return nested
    raise SystemExit(f"ERROR: NexusLink.uplugin not found near {start}")


def resolve_doc_paths(link_root: Path) -> tuple[Path, Path, Path, Path]:
    """返回 cap_root, tools_dir, header_path, output_path。"""
    plugin_src = link_root / "Source" / "NexusLink"
    cap_root = plugin_src / "Private" / "Capabilities"
    tools_dir = plugin_src / "Private" / "Tools"
    docs = link_root / "docs"
    return cap_root, tools_dir, docs / "tool-reference.header.md", docs / "tool-reference.md"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo-root", default=None, help="NexusLink 仓根目录（含 NexusLink.uplugin）；默认自动探测")
    parser.add_argument("--live", default=None, metavar="URL",
                        help="UE MCP 端点 URL，用于 live schema 补全（可选，CI 不需要）")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    link_root = Path(args.repo_root) if args.repo_root else find_nexuslink_root(script_dir)
    cap_root, tools_dir, header_path, output_path = resolve_doc_paths(link_root)

    if not cap_root.exists():
        print(f"ERROR: Capabilities dir not found: {cap_root}", file=sys.stderr)
        sys.exit(1)

    # ── 解析 Capabilities ──────────────────────────────────────────────────────
    categories: dict[str, list[dict[str, Any]]] = {}
    cap_count = 0
    for cpp_file in sorted(cap_root.rglob("*.cpp")):
        cap = parse_capability(cpp_file)
        if cap:
            cat = cap["category"]
            categories.setdefault(cat, []).append(cap)
            cap_count += 1

    # ── 解析元工具 ─────────────────────────────────────────────────────────────
    META_ORDER = ["search_capabilities", "call_capability", "submit_feedback"]
    meta_tools: dict[str, dict] = {}
    if tools_dir.exists():
        for cpp_file in tools_dir.glob("*.cpp"):
            tool = parse_meta_tool(cpp_file)
            if tool and tool["name"] in META_ORDER:
                meta_tools[tool["name"]] = tool
    categories["元工具（Meta）"] = [
        meta_tools[n] for n in META_ORDER if n in meta_tools
    ]

    print(f"解析完成：{cap_count} capabilities, {len(meta_tools)} meta tools", file=sys.stderr)
    for cat, caps in categories.items():
        print(f"  {cat}: {len(caps)}", file=sys.stderr)

    # ── 可选：live schema 补全 ─────────────────────────────────────────────────
    if args.live:
        print(f"\nLive schema supplement: {args.live}", file=sys.stderr)
        for cap_list in categories.values():
            for cap in cap_list:
                live_params = load_live_schema(args.live, cap["name"])
                if live_params:
                    # live 结果优先（含完整 enum），与静态解析合并
                    live_map = {p["name"]: p for p in live_params}
                    merged: list[dict] = []
                    seen_live: set[str] = set()
                    for p in cap["params"]:
                        lp = live_map.get(p["name"])
                        if lp:
                            merged.append({**p, **lp})
                            seen_live.add(p["name"])
                        else:
                            merged.append(p)
                    for lname, lp in live_map.items():
                        if lname not in seen_live:
                            merged.append(lp)
                    cap["params"] = merged
                    print(f"  ✓ {cap['name']} ({len(merged)} params)", file=sys.stderr)

    # ── 加载 header ────────────────────────────────────────────────────────────
    if header_path.exists():
        header = header_path.read_text(encoding="utf-8")
    else:
        header = "# NexusLink 工具参考手册\n\n"
        print(f"WARNING: header 文件不存在 {header_path}，使用最小 header", file=sys.stderr)

    # ── 生成并写出 ─────────────────────────────────────────────────────────────
    content = render_markdown(categories, header)
    output_path.write_text(content, encoding="utf-8")

    size_kb = output_path.stat().st_size // 1024
    print(f"\n生成完毕：{output_path}（{size_kb} KB）", file=sys.stderr)
    print("禁止手工编辑 AUTO-GENERATED 段，改 schema 后重跑本脚本。", file=sys.stderr)


if __name__ == "__main__":
    main()
