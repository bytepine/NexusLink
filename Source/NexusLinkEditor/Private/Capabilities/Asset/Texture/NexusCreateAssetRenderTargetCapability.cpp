// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusCreateAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FNexusCreateAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("CreateAssetRenderTarget");
	Out.Description = TEXT("Create TextureRenderTarget2D; edit size/format via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("SizeX"),     FNexusSchema::Int(TEXT("Width (default 256)"), 256, 1))
		.Prop(TEXT("SizeY"),     FNexusSchema::Int(TEXT("Height (default 256)"), 256, 1))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("Target"), TEXT("Texture"), TEXT("rt"), TEXT("offscreen") };
	Out.RelatedCapabilities = { TEXT("GetAssetRenderTarget"), TEXT("ManageAssetRenderTarget") };
	Out.WhenToUse = TEXT("Create render target texture asset");
}

FCapabilityResult FNexusCreateAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("AssetPath"));
		const int32 SizeX = static_cast<int32>(A.Num(TEXT("SizeX"), 256));
		const int32 SizeY = static_cast<int32>(A.Num(TEXT("SizeY"), 256));

		const FNexusAssetUtils::FAssetCreateOutcome Created =
			FNexusAssetUtils::CreatePlainAsset<UTextureRenderTarget2D>(AssetPath, RF_Public | RF_Standalone, false);
		if (!Created.Ok())
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, Created.Error);
			return;
		}
		UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Created.Asset);
		if (!RT)
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Create failed"));
			return;
		}
		RT->SizeX = SizeX;
		RT->SizeY = SizeY;
		RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		RT->ClearColor = FLinearColor::Black;
		RT->UpdateResourceImmediate(true);

		FNexusAssetUtils::NotifyAndSaveCreated(RT->GetOutermost(), RT, AssetPath);

		TSharedPtr<FJsonObject> Entry = FNexusCapabilityResultBuilder::MakeCreatedEntry(RT);
		Entry->SetNumberField(TEXT("SizeX"),   RT->SizeX);
		Entry->SetNumberField(TEXT("SizeY"),   RT->SizeY);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusCreateAssetRenderTargetCapability)
