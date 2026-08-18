// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Media/NexusManageAssetMediaSourceCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "FileMediaSource.h"
#include "NexusMcpTool.h"

void FManageAssetMediaSourceCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_media_source");
	Out.SearchAssetTypes = {TEXT("FileMediaSource"), TEXT("MediaSource")};
	Out.Description = TEXT("Batch edit FileMediaSource. action=set_file_path/set_loop.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_file_path"), TEXT("set_loop") }))
		.Prop(TEXT("mediaPath"), FNexusSchema::Str(TEXT("Media file path (set_file_path)")))
		.Prop(TEXT("loop"), FNexusSchema::Bool(TEXT("Loop flag (set_loop, reflected field)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("MediaSource asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("video"), TEXT("path"), TEXT("loop"), TEXT("media") };
	Out.RelatedCapabilities = { TEXT("get_asset_media_source"), TEXT("create_asset_media_source") };
}

struct FMediaSourceActionState
{
	UFileMediaSource* Source = nullptr;
	bool bDirty = false;
};

static FMediaSourceActionState* MSState(FNexusActionContext& Ctx)
{
	return static_cast<FMediaSourceActionState*>(Ctx.Target);
}

static UFileMediaSource* MSFrom(FNexusActionContext& Ctx)
{
	FMediaSourceActionState* S = MSState(Ctx);
	return S ? S->Source : nullptr;
}

static void MarkMSDirty(FNexusActionContext& Ctx)
{
	if (FMediaSourceActionState* S = MSState(Ctx))
	{
		S->bDirty = true;
	}
}

static void HandleMS_SetFilePath(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFileMediaSource* Source = MSFrom(Ctx);
	FString FilePath;
	if (!Op->TryGetStringField(TEXT("mediaPath"), FilePath) || FilePath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_file_path requires mediaPath"));
		return;
	}
	Source->SetFilePath(FilePath);
	MarkMSDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("mediaPath"), Source->GetFilePath());
}

static void HandleMS_SetLoop(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UFileMediaSource* Source = MSFrom(Ctx);
	if (!Op->HasField(TEXT("loop")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_loop requires loop"));
		return;
	}
	const bool bLoop = Op->GetBoolField(TEXT("loop"));
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(
		Source, { TEXT("Loop") }, 0, bLoop ? TEXT("True") : TEXT("False"), OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"),
			Err.IsEmpty() ? TEXT("no Loop field") : Err);
		return;
	}
	MarkMSDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("loop"), bLoop);
}

bool FManageAssetMediaSourceCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UFileMediaSource* Source = FNexusAssetUtils::LoadAssetWithFallback<UFileMediaSource>(AssetPath);
	if (!Source)
	{
		OutError = FString::Printf(TEXT("Failed to load FileMediaSource: %s"), *AssetPath);
		return false;
	}
	FMediaSourceActionState* State = new FMediaSourceActionState();
	State->Source = Source;
	OutTarget = State;
	return true;
}

void FManageAssetMediaSourceCapability::FinalizeTarget(void* Target) const
{
	FMediaSourceActionState* State = static_cast<FMediaSourceActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->Source)
	{
		State->Source->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetMediaSourceCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_file_path"), &HandleMS_SetFilePath);
	OutHandlers.Add(TEXT("set_loop"),      &HandleMS_SetLoop);
}

REGISTER_MCP_CAPABILITY(FManageAssetMediaSourceCapability)
