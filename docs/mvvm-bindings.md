# MVVM 数据绑定

[文档导航](README.md) · [项目首页](../README.md)

UE 5.5+ 且宿主启用引擎 ModelViewViewModel 插件时可用。实现位于 NexusLinkEditor，不需要 NexusLinkExt 或官方 MCP 服务。

## 创建完整绑定

先以 `SearchAsset(AssetType="Widget", PathFilter=...)` 找到目标 WBP，用 `GetAssetViewModel` 读取当前配置，再通过 `ManageAssetViewModel.Operations[]` 编辑。

`AddBinding` 的 `SourceContext` / `DestinationContext` 使用精确格式：

- `Self`：WBP 自身；省略上下文时使用此值。
- `ViewModel:<名称>`：已添加的 ViewModel 上下文。同一类的多个 ViewModel 按名称区分。
- `Widget:<名称>`：WBP WidgetTree 中已有的控件。

`SourcePath` / `DestinationPath` 是相对上述上下文的属性路径，不含上下文前缀。支持逐段解析对象和结构体，例如 `Health`、`Nested.Health`；不支持数组下标或函数段。字段/上下文不存在、任意一端缺失或类型不兼容时，不新增绑定。嵌套结构体经过原生 Getter 时受引擎限制，可能在编译阶段被拒绝，须检查 CompileError。方向要求源可读、目标可写；持续绑定的 FieldNotify 要求由最终蓝图编译检查。

已有 `HealthVM` 和 `HealthBar` 时，调用示例：

```json
{
  "AssetPath": "<SearchAsset 返回的 WBP 路径>",
  "bCompile": true,
  "bSaveToDisk": true,
  "Operations": [{
    "Action": "AddBinding",
    "SourceContext": "ViewModel:HealthVM",
    "SourcePath": "HealthPercent",
    "DestinationContext": "Widget:HealthBar",
    "DestinationPath": "Percent",
    "BindingType": "OneWayToDestination"
  }]
}
```

`BindingType` 支持 `OneWayToDestination`（默认）、`OneWayToSource`、`TwoWay`、`OneTimeToDestination`、`OneTimeToSource`。单项 `bEnabled` / `bCompile` 控制运行时初始启用和是否参与编译，均默认 true；**顶层 `bCompile` 才触发本次 WBP 编译**。编译失败不自动保存，但内存修改可能保留；检查 `CompileError` / `bCompiled` 后修正或撤销。

兼容旧调用：完全不提供路径和新配置字段的 `AddBinding` 仍创建空绑定，不代表已建立可运行绑定。ViewModel 创建策略采用引擎构造器默认值，以读回的 CreationType 为准；本工具不在当前运行世界中主动实例化。

## 转换函数

`GetAssetViewModel(bIncludeConversionFunctions=true)` 列出当前 WBP 可用的单输入 UFunction 完整路径。用 `ConversionFilter` 筛选路径，用 `ConversionLimit` 限制数量（默认 100，范围 1–500）。结果为 `ConversionFunctions`、筛选后总数 `ConversionFunctionsCount` 与 `bConversionsTruncated`。

将发现的路径填到 `ConversionFunction`（源到目标）或 `ReverseConversionFunction`（目标到源）；双向分别校验，禁止为未启用方向配置转换。不会自动选择转换。当前支持单输入 UFunction，不支持多输入转换、K2 节点或自定义转换图；列表未按端点类型过滤，创建时会重新校验。

## 读回、删除和撤销

新增返回 `BindingId`、`BindingIndex` 和 `BindingsCount`。读回包含上下文、方向、启用/编译标记及两个方向的转换函数。历史 `SourcePath` / `DestinationPath` 展示文本保留；重新创建时，将 `SourcePropertyPath` / `DestinationPropertyPath` 用作输入的路径。ViewModel 列表还包含 Id 与完整 ClassPath。读取压缩列表时，将 `Bindings_defaults` / `ViewModels_defaults` 与单项合并。

`RemoveBinding` 优先传读回的 `BindingId`，也兼容显式 `BindingIndex`；两者只传其一。缺少或无效标识不会删除第一项。编辑纳入现有 Undo 事务；批次不保证所有操作同时成功，须检查逐项 Error。

English: use explicit Self / ViewModel:name / Widget:name contexts and dotted property paths. BindingType controls direction; unary conversion UFunctions are discovered through GetAssetViewModel. Top-level bCompile compiles the WBP; per-operation bCompile controls binding inclusion. Read back contexts, property paths and conversion references after compilation. Remove by BindingId to avoid index changes.
