// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Asset/NexusManageAssetLuaBindingCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
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
	Out.Description = TEXT("绑定/解绑 BP 的 UnLua 接口。action=bind|unbind。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(TEXT("操作"), { TEXT("bind"), TEXT("unbind") }))
		.Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("Lua 模块名（bind，可选，默认按资产路径）")))
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("蓝图资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema.ToSharedRef()))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("unlua"), TEXT("binding"), TEXT("module") };
	Out.RelatedCapabilities = { TEXT("get_asset_lua_binding"), TEXT("get_runtime_lua_object") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("editor_only") };
	Out.WhenToUse = TEXT("给 BP 实现/移除 UnLuaInterface；勿用 set_* 非 property");
}

FCapabilityResult FManageAssetLuaBindingCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
#if !WITH_EDITOR
		OutError = TEXT("manage_asset_lua_binding 仅在编辑器构建可用");
		return;
#else
		FString AssetPath;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}
		UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
		if (!BP) { OutError = FString::Printf(TEXT("Blueprint 未找到: %s"), *AssetPath); return; }

		const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
			return;
		}

#if !NX_UE_HAS_BP_INTERFACE_ASSET_PATH
		const FName IfaceName(TEXT("UnLuaInterface"));
#endif
		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
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
				Res->SetStringField(TEXT("note"), TEXT("已请求实现 UnLuaInterface；GetModuleName 需在图中返回模块名"));
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
				Res->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s（仅 bind/unbind）"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Res));
		}
		BP->MarkPackageDirty();
#endif
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetLuaBindingCapability)

#endif
