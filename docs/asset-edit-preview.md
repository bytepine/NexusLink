# Asset edit preview / 资产修改预览

[文档导航 / Documentation](README.md) · [项目首页 / Home](../README.md)

`GetAssetEditPreview` 在临时属性存储中解析值，返回 `PropertyType`、`CurrentValue`、规范化的 `TargetValue`、`bValid` 与逐项 `Error`。它不写入真实属性、不编译、不保存、不创建 Undo。请求包含单个 `AssetPath`、目标 `Capability` 和 `Operations`，参数保持原有写操作格式：

```json
{
  "AssetPath": "/Game/YOUR_TABLE",
  "Capability": "ManageAssetDataTable",
  "Operations": [{"Action": "Set", "RowName": "YOUR_ROW", "FieldName": "YOUR_FIELD", "Value": "12"}]
}
```

路径、行和字段名须先通过读取工具确认。首批支持 `ManageAssetBlueprint/SetDefaults`、`ManageAssetDataTable/Set`、`ManageAssetUserWidget/SetProperty`。叶子类型限 bool、数值、枚举、字符串、Name、Text；支持结构体中的标量字段，拒绝对象引用、容器、固定数组及经对象/容器跳转的路径。BP 必须已有可用的生成类和 CDO；预览不会为缺失的目标编译或创建 CDO。每条预览读取当前真实值，不模拟前序预览的写入。

写入时在 operation 中传入 `ExpectedValue`（建议使用预览的 `CurrentValue`），在顶层设置 `bVerify=true`。`ExpectedValue` 按相同类型规范化后比较，空字符串也是有效前值。前值不匹配时不写入。

```json
{
  "AssetPath": "/Game/YOUR_TABLE",
  "bVerify": true,
  "bSaveToDisk": true,
  "Operations": [{"Action": "Set", "RowName": "YOUR_ROW", "FieldName": "YOUR_FIELD", "Value": "12", "ExpectedValue": "7"}]
}
```

`bVerify` 默认关闭；启用后仅接受上述对应操作。顺序为检查前值、在 Undo 记录后写入、编译 BP/WBP、重新定位属性并读回、全部操作验证成功后按 `bSaveToDisk` 保存。`bCompile=false` 不会跳过验证所需编译。结果提供 `OldValue`、`NewValue`、`ReadbackValue`、`bWritten`、`bVerified`、`Verification`；编译错误、读取不一致及保存错误分别报告。未请求验证时为 `Verification=NotRequested`。

单次 Manage 保持部分成功：前项成功的修改保留，任何验证失败阻止该调用自动保存，不能描述为资产已还原。跨调用 `CallCapability.Calls` 批次沿用事务回滚，保存在内存事务完成后进行，保存失败不宣称磁盘整体回滚。失败写入不自动重试。

Preview uses temporary property storage and never mutates, compiles, saves or records Undo. Only the three listed scalar edits are supported. ExpectedValue is an optional optimistic precondition. bVerify compiles BP/WBP, resolves the property again and compares the readback before automatic save. Existing partial-success and batch rollback boundaries remain in effect.
