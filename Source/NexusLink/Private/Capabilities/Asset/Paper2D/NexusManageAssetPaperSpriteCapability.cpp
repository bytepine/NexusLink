// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Paper2D/NexusManageAssetPaperSpriteCapability.h"
#if WITH_PAPER2D
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "PaperSprite.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FManageAssetPaperSpriteCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_paper_sprite");
	Out.SearchAssetTypes = {TEXT("PaperSprite")};
	Out.Description = TEXT("Batch edit PaperSprite. action=set_source/set_pivot.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"), { TEXT("set_source"), TEXT("set_pivot") }))
		.Prop(TEXT("sourceTexturePath"), FNexusSchema::Str(TEXT("Source Texture2D (set_source)")))
		.Prop(TEXT("pivotX"), FNexusSchema::Num(TEXT("Pivot X (set_pivot)")))
		.Prop(TEXT("pivotY"), FNexusSchema::Num(TEXT("Pivot Y (set_pivot)")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("PaperSprite asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("sprite"), TEXT("pivot"), TEXT("source") };
	Out.RelatedCapabilities = { TEXT("get_asset_paper_sprite"), TEXT("create_asset_paper_sprite") };
}

FCapabilityResult FManageAssetPaperSpriteCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UPaperSprite* Sprite = FNexusAssetUtils::LoadAssetWithFallback<UPaperSprite>(AssetPath);
		if (!Sprite)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("Failed to load PaperSprite: %s"), *AssetPath));
			return;
		}
		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}
		bool bDirty = false;
		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr) continue;
			const TSharedPtr<FJsonObject>& Op = *OpPtr;
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);
			Action = Action.ToLower();
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("path"), AssetPath);
			Entry->SetStringField(TEXT("action"), Action);
			if (Action == TEXT("set_source"))
			{
				FString TexPath;
				if (!Op->TryGetStringField(TEXT("sourceTexturePath"), TexPath) || TexPath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_source requires sourceTexturePath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(TexPath);
				if (!Tex)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Texture2D not found: %s"), *TexPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Sprite->SetSourceTexture(Tex);
				bDirty = true;
				Entry->SetStringField(TEXT("sourceTexture"), Tex->GetPathName());
			}
			else if (Action == TEXT("set_pivot"))
			{
				FVector2D Pivot = Sprite->GetPivotPosition();
				if (Op->HasField(TEXT("pivotX"))) Pivot.X = static_cast<float>(Op->GetNumberField(TEXT("pivotX")));
				if (Op->HasField(TEXT("pivotY"))) Pivot.Y = static_cast<float>(Op->GetNumberField(TEXT("pivotY")));
				Sprite->SetPivotPosition(Pivot);
				bDirty = true;
				Entry->SetNumberField(TEXT("pivotX"), Pivot.X);
				Entry->SetNumberField(TEXT("pivotY"), Pivot.Y);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unsupported operation: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) Sprite->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetPaperSpriteCapability)
#endif
