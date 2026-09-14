// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Asset/NexusManageAssetLuaBindingCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
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

#if WITH_EDITOR
struct FLuaBindingActionState
{
	UBlueprint* BP = nullptr;
	bool bDirty = false;
};

static FLuaBindingActionState* LuaBindState(FNexusActionContext& Ctx)
{
	return static_cast<FLuaBindingActionState*>(Ctx.Target);
}

static UBlueprint* LuaBindFrom(FNexusActionContext& Ctx)
{
	FLuaBindingActionState* S = LuaBindState(Ctx);
	return S ? S->BP : nullptr;
}

static void MarkLuaBindDirty(FNexusActionContext& Ctx)
{
	if (FLuaBindingActionState* S = LuaBindState(Ctx))
	{
		S->bDirty = true;
	}
}

#if !NX_UE_HAS_BP_INTERFACE_ASSET_PATH
static const FName UnLuaIfaceName(TEXT("UnLuaInterface"));
#endif

static void HandleLua_Bind(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UBlueprint* BP = LuaBindFrom(Ctx);
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
	FBlueprintEditorUtils::ImplementNewInterface(BP, FTopLevelAssetPath(TEXT("/Script/UnLua"), TEXT("UnLuaInterface")));
#else
	FBlueprintEditorUtils::ImplementNewInterface(BP, UnLuaIfaceName);
#endif
	FString ModuleName;
	Op->TryGetStringField(TEXT("moduleName"), ModuleName);
	if (!ModuleName.IsEmpty()) Ctx.Entry->SetStringField(TEXT("moduleName"), ModuleName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("UnLuaInterface requested; GetModuleName must return module in graph"));
	MarkLuaBindDirty(Ctx);
}

static void HandleLua_Unbind(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	(void)Op;
	UBlueprint* BP = LuaBindFrom(Ctx);
#if NX_UE_HAS_BP_INTERFACE_ASSET_PATH
	FBlueprintEditorUtils::RemoveInterface(BP, FTopLevelAssetPath(TEXT("/Script/UnLua"), TEXT("UnLuaInterface")));
#else
	FBlueprintEditorUtils::RemoveInterface(BP, UnLuaIfaceName);
#endif
	Ctx.Entry->SetBoolField(TEXT("unbound"), true);
	MarkLuaBindDirty(Ctx);
}
#endif // WITH_EDITOR

bool FManageAssetLuaBindingCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	(void)Args;
	(void)Entry;
	(void)OutTarget;
	OutError = TEXT("manage_asset_lua_binding only available in editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UBlueprint* BP = FNexusAssetUtils::LoadAssetWithFallback<UBlueprint>(AssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath);
		return false;
	}
	FLuaBindingActionState* State = new FLuaBindingActionState();
	State->BP = BP;
	OutTarget = State;
	return true;
#endif
}

void FManageAssetLuaBindingCapability::FinalizeTarget(void* Target) const
{
#if WITH_EDITOR
	FLuaBindingActionState* State = static_cast<FLuaBindingActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->BP)
	{
		State->BP->MarkPackageDirty();
	}
	delete State;
#else
	(void)Target;
#endif
}

void FManageAssetLuaBindingCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("bind"),   &HandleLua_Bind);
	OutHandlers.Add(TEXT("unbind"), &HandleLua_Unbind);
#else
	(void)OutHandlers;
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetLuaBindingCapability)

#endif
