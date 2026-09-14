// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusGetAssetStructCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
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
	Out.SearchAssetTypes = {TEXT("Struct")};
	Out.Description = TEXT("Inspect UDS field definitions. Optional propertyPaths filter.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),     FNexusSchema::Str(TEXT("UserDefinedStruct asset path")))
		.Prop(TEXT("propertyPaths"), FNexusSchema::StrArr(TEXT("Field name filter (first segment)")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = {
		TEXT("uds"), TEXT("userstruct"), TEXT("fields"), TEXT("members"), TEXT("schema")
	};
	Out.RelatedCapabilities = { TEXT("manage_asset_struct_field"), TEXT("create_asset_struct") };
	Out.WhenToUse = TEXT("Read UDS field definitions; no edits");
}

FCapabilityResult FGetAssetStructCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{

	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString Path = A.Str(TEXT("assetPath"));

		TArray<FString> PropertyPaths;
		FNexusPropertyUtils::ReadStringArray(Arguments, TEXT("propertyPaths"), PropertyPaths);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

		UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
		if (!Obj) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

		UUserDefinedStruct* US = Cast<UUserDefinedStruct>(Obj);
		if (!US) { Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a UserDefinedStruct: %s"), *Path)); OutEntries.Add(MakeShared<FJsonValueObject>(Entry)); return; }

#if WITH_EDITOR
		TSharedPtr<FJsonObject> One = HandleStruct(US, PropertyPaths);
		for (const auto& Pair : One->Values) Entry->SetField(Pair.Key, Pair.Value);
#else
		Entry->SetStringField(TEXT("error"), TEXT("get_asset_struct only available in editor builds"));
#endif

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetStructCapability)
