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

void FNexusManageAssetPoseSearchCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name        = TEXT("ManageAssetPoseSearch");
	Out.SearchAssetTypes = {TEXT("PoseSearchDatabase"), TEXT("PoseSearchSchema")};
	Out.Description = TEXT("Manage PoseSearchDatabase: SetSchema/AddTag/RemoveTag (UE 5.4+).");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("PoseSearchDatabase asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrOfObj(TEXT("Operation list")))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("pose"), TEXT("search"), TEXT("motion"), TEXT("matching"), TEXT("Schema"), TEXT("Tag") };
	Out.RelatedCapabilities = { TEXT("GetAssetPoseSearch"), TEXT("CreateAssetPoseSearch"), TEXT("SearchAsset") };
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
	if (!DB)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PoseSearchDatabase is null"));
		return;
	}
	FString SchemaPath;
	Op->TryGetStringField(TEXT("SchemaPath"), SchemaPath);
	if (SchemaPath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetSchema requires SchemaPath"));
		return;
	}
	UPoseSearchSchema* Schema = FNexusAssetUtils::LoadAssetWithFallback<UPoseSearchSchema>(SchemaPath);
	if (!Schema)
	{
		Ctx.Entry->SetStringField(TEXT("Error"),
			FString::Printf(TEXT("PoseSearchSchema not found: %s"), *SchemaPath));
		return;
	}
	DB->Schema = Schema;
	MarkPSDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("SchemaPath"), SchemaPath);
}

static void HandlePS_AddTag(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPoseSearchDatabase* DB = PSFrom(Ctx);
	if (!DB)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PoseSearchDatabase is null"));
		return;
	}
	FString TagStr;
	Op->TryGetStringField(TEXT("Tag"), TagStr);
	if (TagStr.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddTag requires tag parameter"));
		return;
	}
	const FName TagName(*TagStr);
	if (!DB->Tags.Contains(TagName))
	{
		DB->Tags.Add(TagName);
		MarkPSDirty(Ctx);
	}
	Ctx.Entry->SetStringField(TEXT("Tag"), TagStr);
}

static void HandlePS_RemoveTag(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UPoseSearchDatabase* DB = PSFrom(Ctx);
	if (!DB)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("PoseSearchDatabase is null"));
		return;
	}
	FString TagStr;
	Op->TryGetStringField(TEXT("Tag"), TagStr);
	const FName TagName(*TagStr);
	const int32 Removed = DB->Tags.Remove(TagName);
	if (Removed > 0) MarkPSDirty(Ctx);
	if (Removed == 0)
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("tag '%s' does not exist"), *TagStr));
}

bool FNexusManageAssetPoseSearchCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetPoseSearchCapability::FinalizeTarget(void* Target) const
{
	FPoseSearchActionState* State = static_cast<FPoseSearchActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->DB)
	{
		State->DB->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetPoseSearchCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetSchema"),  &HandlePS_SetSchema);
	OutHandlers.Add(TEXT("AddTag"),     &HandlePS_AddTag);
	OutHandlers.Add(TEXT("RemoveTag"),  &HandlePS_RemoveTag);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetPoseSearchCapability)

#endif // WITH_POSE_SEARCH
