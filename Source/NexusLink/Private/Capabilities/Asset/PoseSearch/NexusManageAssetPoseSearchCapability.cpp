// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/PoseSearch/NexusManageAssetPoseSearchCapability.h"

#if WITH_POSE_SEARCH

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"

void FManageAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("manage_asset_pose_search");
	Out.SearchAssetTypes = {TEXT("PoseSearchDatabase"), TEXT("PoseSearchSchema")};
	Out.Description = TEXT("Manage PoseSearchDatabase: set_schema/add_tag/remove_tag (UE 5.4+).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("PoseSearchDatabase asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrOfObj(TEXT("Operation list")))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("schema"), TEXT("tag") };
	Out.RelatedCapabilities = { TEXT("get_asset_pose_search"), TEXT("create_asset_pose_search"), TEXT("search_asset") };
	Out.WhenToUse = TEXT("Set PoseSearch Database Schema or modify Tags");
}

struct FPoseSearchActionState
{
	UPoseSearchDatabase* DB = nullptr;
	bool bDirty = false;
};

static FPoseSearchActionState* PSState(FNexusActionContext& Ctx)
{
	return static_cast<FPoseSearchActionState*>(Ctx.Target);
}

static UPoseSearchDatabase* PSFrom(FNexusActionContext& Ctx)
{
	FPoseSearchActionState* S = PSState(Ctx);
	return S ? S->DB : nullptr;
}

static void MarkPSDirty(FNexusActionContext& Ctx)
{
	if (FPoseSearchActionState* S = PSState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandlePS_SetSchema(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPoseSearchDatabase* DB = PSFrom(Ctx);
	FString SchemaPath;
	Op->TryGetStringField(TEXT("schemaPath"), SchemaPath);
	if (SchemaPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_schema requires schemaPath"));
		return;
	}
	UPoseSearchSchema* Schema = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchSchema>(SchemaPath);
	if (!Schema)
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			FString::Printf(TEXT("PoseSearchSchema not found: %s"), *SchemaPath));
		return;
	}
	DB->Schema = Schema;
	MarkPSDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("schemaPath"), SchemaPath);
}

static void HandlePS_AddTag(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPoseSearchDatabase* DB = PSFrom(Ctx);
	FString TagStr;
	Op->TryGetStringField(TEXT("tag"), TagStr);
	if (TagStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_tag requires tag parameter"));
		return;
	}
	const FName TagName(*TagStr);
	if (!DB->Tags.Contains(TagName))
	{
		DB->Tags.Add(TagName);
		MarkPSDirty(Ctx);
	}
	Ctx.Entry->SetStringField(TEXT("tag"), TagStr);
}

static void HandlePS_RemoveTag(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPoseSearchDatabase* DB = PSFrom(Ctx);
	FString TagStr;
	Op->TryGetStringField(TEXT("tag"), TagStr);
	const FName TagName(*TagStr);
	const int32 Removed = DB->Tags.Remove(TagName);
	if (Removed > 0) MarkPSDirty(Ctx);
	if (Removed == 0)
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("tag '%s' does not exist"), *TagStr));
}

bool FManageAssetPoseSearchCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UPoseSearchDatabase* DB = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchDatabase>(AssetPath);
	if (!DB)
	{
		OutError = FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *AssetPath);
		return false;
	}
	FPoseSearchActionState* State = new FPoseSearchActionState();
	State->DB = DB;
	OutTarget = State;
	return true;
}

void FManageAssetPoseSearchCapability::FinalizeTarget(void* Target) const
{
	FPoseSearchActionState* State = static_cast<FPoseSearchActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->DB)
	{
		State->DB->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetPoseSearchCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_schema"),  &HandlePS_SetSchema);
	OutHandlers.Add(TEXT("add_tag"),     &HandlePS_AddTag);
	OutHandlers.Add(TEXT("remove_tag"),  &HandlePS_RemoveTag);
}

REGISTER_MCP_CAPABILITY(FManageAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
