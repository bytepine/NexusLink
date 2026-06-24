// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusGetAssetStructCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#if WITH_EDITOR
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#endif

#if WITH_EDITOR
static TSharedPtr<FJsonObject> HandleStruct(UUserDefinedStruct* Struct, const TArray<FString>& PropertyPaths)
{
	const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);
	TArray<TSharedPtr<FJsonValue>> Fields;
	for (const FStructVariableDescription& Var : VarDescs)
	{
		if (!FNexusAssetUtils::MatchesPropertyPathsFilter(PropertyPaths, Var.FriendlyName)) continue;

		// ToPinType() 只调一次
		const FEdGraphPinType PT = Var.ToPinType();

		TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
		F->SetStringField(TEXT("name"), Var.FriendlyName);
		F->SetStringField(TEXT("type"), PT.PinCategory.ToString());
		if (!PT.PinSubCategory.IsNone()) F->SetStringField(TEXT("subType"), PT.PinSubCategory.ToString());
		// 对象/结构引用的具体类名（e.g. struct/object 引用类型）
		if (UObject* SubObj = PT.PinSubCategoryObject.Get())
		{
			F->SetStringField(TEXT("subCategoryObject"), SubObj->GetName());
		}
		if (!Var.DefaultValue.IsEmpty()) F->SetStringField(TEXT("defaultValue"), Var.DefaultValue);
		F->SetStringField(TEXT("guid"), Var.VarGuid.ToString());
		Fields.Add(MakeShared<FJsonValueObject>(F));
	}
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetNumberField(TEXT("fieldCount"), VarDescs.Num());
	R->SetArrayField(TEXT("fields"), Fields);
	return R;
}
#endif // WITH_EDITOR

void FGetAssetStructCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_struct");
	Out.Description = TEXT("检查 UDS 字段定义。含 name/type/subType/defaultValue；可 propertyPaths 过滤。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),     FNexusSchema::Str(TEXT("UserDefinedStruct 资产路径")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("字段名过滤（首段）")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = {
		TEXT("uds"), TEXT("userstruct"), TEXT("fields"), TEXT("members"), TEXT("schema")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_struct_field"), TEXT("create_asset_struct") };
	Out.WhenToUse = TEXT("读 UDS 字段定义；不含编辑");
}

FCapabilityResult FGetAssetStructCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{

		FString Path;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		TArray<FString> PropertyPaths;
		FNexusPropertyUtils::ReadStringArray(Arguments, TEXT("propertyPaths"), PropertyPaths);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		if (!Obj) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("资产未找到: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UUserDefinedStruct* US = Cast<UUserDefinedStruct>(Obj);
		if (!US) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("资产不是 UserDefinedStruct: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

#if WITH_EDITOR
		TSharedPtr<FJsonObject> One = HandleStruct(US, PropertyPaths);
		for (const auto& Pair : One->Values) Entry->SetField(Pair.Key, Pair.Value);
#else
		Entry->SetStringField(TEXT("error"), TEXT("get_asset_struct 仅在编辑器构建可用"));
#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetStructCapability)
