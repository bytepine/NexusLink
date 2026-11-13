<a id="mvvm-数据绑定"></a>
# MVVM data binding
[中文](mvvm-bindings.md) · English

[Documentation](README.en.md) · [Home](../README.en.md)

Available on UE 5.5+ with the engine ModelViewViewModel plugin enabled in the host. Implementation is in NexusLinkEditor; NexusLinkExt and the official MCP service are unnecessary.

<a id="创建完整绑定"></a>
## Creating a complete binding

Find the target WBP with SearchAsset(AssetType="Widget", PathFilter=...), read current configuration using GetAssetViewModel, then edit through ManageAssetViewModel.Operations[].

AddBinding SourceContext / DestinationContext use exact formats:

- Self: the WBP itself, also the default when omitted.
- ViewModel:<name>: an added ViewModel context; same-class instances are distinguished by name.
- Widget:<name>: an existing widget in the WBP WidgetTree.

SourcePath / DestinationPath are relative property paths without context prefixes. Objects/structs resolve segment by segment, such as Health or Nested.Health; array indices/function segments are unsupported. Missing fields/contexts/endpoints or incompatible types prevent creation. Nested structs through native Getters are engine-limited and may fail compilation; inspect CompileError. Sources must be readable and destinations writable. Final Blueprint compilation checks FieldNotify requirements for continuous binding.

Example with existing HealthVM and HealthBar:

```json
{
  "AssetPath": "<WBP path returned by SearchAsset>",
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

BindingType accepts OneWayToDestination (default), OneWayToSource, TwoWay, OneTimeToDestination, OneTimeToSource. Per-operation bEnabled / bCompile control initial runtime enablement and inclusion in compilation, both default true. **Only top-level bCompile compiles the WBP for this request.** Compilation failure prevents automatic save, but memory edits may remain. Inspect CompileError / bCompiled, then fix or undo.

For compatibility, AddBinding without paths or new configuration fields still creates an empty, not runnable, binding. ViewModel creation strategy follows engine constructor defaults; read CreationType. The tool does not instantiate it in the current runtime World.

<a id="转换函数"></a>
## Conversion functions

GetAssetViewModel(bIncludeConversionFunctions=true) lists full single-input UFunction paths available to the WBP. ConversionFilter filters paths; ConversionLimit bounds results (default 100, range 1–500). Results include ConversionFunctions, filtered total ConversionFunctionsCount, and bConversionsTruncated.

Set a discovered path as ConversionFunction (source to destination) or ReverseConversionFunction (destination to source). Directions are independently validated; disabled directions cannot configure conversion. No automatic selection occurs. Only unary UFunctions are supported, excluding multi-input conversions, K2 nodes, or custom conversion graphs. Lists are not filtered by endpoint type; creation revalidates.

<a id="读回删除和撤销"></a>
## Readback, removal, and Undo

Creation returns BindingId, BindingIndex, BindingsCount. Readback includes contexts, direction, enable/compile flags, and both conversions. Historical SourcePath / DestinationPath display text remains; use SourcePropertyPath / DestinationPropertyPath as paths when recreating. ViewModel lists also include Id and full ClassPath. Merge Bindings_defaults / ViewModels_defaults into entries when reading compressed lists.

Prefer read-back BindingId for RemoveBinding; explicit BindingIndex remains compatible. Supply exactly one. Missing/invalid identifiers never delete the first entry. Edits use existing Undo transactions; batches do not guarantee all succeed, so inspect per-operation Error.
