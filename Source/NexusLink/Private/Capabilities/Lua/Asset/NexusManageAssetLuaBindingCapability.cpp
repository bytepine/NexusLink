// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Asset/NexusManageAssetLuaBindingCapability.h"

#if WITH_UNLUA

#include "Utils/NexusJsonUtils.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Engine/Blueprint.h"
#include "NexusMcpTool.h"
#if WITH_EDITOR
#include "Kismet2/BlueprintEditorUtils.h"
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
#include "UObject/TopLevelAssetPath.h"
#endif
#endif

void FManageAssetLuaBindingCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_lua_binding");
	Out.Description = TEXT("Bind/unbind BP UnLua interface. action=bind|unbind.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("bind"), TEXT("unbind") }))
		.Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("Lua module name (bind; optional, defaults from asset path)")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("Blueprint asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("unlua"), TEXT("binding"), TEXT("module") };
	Out.RelatedCapabilities = { TEXT("get_asset_lua_binding"), TEXT("get_runtime_lua_object") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("editor_only") };
	Out.WhenToUse = TEXT("Implement/remove UnLuaInterface on BP; do not set_* non-property");
}

FCapabilityResult FManageAssetLuaBindingCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_lua_binding only available in editor builds");
		return;
#else
		const FString AssetPath = A.Str(TEXT("assetPath"));
		UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
		if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

#if !NX_UE_HAS_BP_INTERFACE_ASSET_PATH
		const FName IfaceName(TEXT("UnLuaInterface"));
#endif
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Res->SetStringField(TEXT("action"), Action);
			if (Action == TEXT("bind"))
			{
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
				FBlueprintEditorUtils::ImplementNewInterface(BP, FTopLevelAssetPath(TEXT("/Script/UnLua"), TEXT("UnLuaInterface")));
#else
				FBlueprintEditorUtils::ImplementNewInterface(BP, IfaceName);
#endif
				FString ModuleName;
				Op->TryGetStringField(TEXT("moduleName"), ModuleName);
				if (!ModuleName.IsEmpty()) Res->SetStringField(TEXT("moduleName"), ModuleName);
				Res->SetStringField(TEXT("note"), TEXT("UnLuaInterface requested; GetModuleName must return module in graph"));
			}
			else if (Action == TEXT("unbind"))
			{
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
				FBlueprintEditorUtils::RemoveInterface(BP, FTopLevelAssetPath(TEXT("/Script/UnLua"), TEXT("UnLuaInterface")));
#else
				FBlueprintEditorUtils::RemoveInterface(BP, IfaceName);
#endif
				Res->SetBoolField(TEXT("unbound"), true);
			}
			else
			{
				Res->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s (bind/unbind only)"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Res));
		}
		BP->MarkPackageDirty();
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetLuaBindingCapability)

#endif
