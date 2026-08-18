// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusCreateAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FCreateAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_render_target");
	Out.Description = TEXT("Create TextureRenderTarget2D; edit size/format via manage.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Asset package path")))
		.Prop(TEXT("sizeX"),     FNexusSchema::Int(TEXT("Width (default 256)"), 256, 1))
		.Prop(TEXT("sizeY"),     FNexusSchema::Int(TEXT("Height (default 256)"), 256, 1))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("target"), TEXT("texture"), TEXT("rt"), TEXT("offscreen") };
	Out.RelatedCapabilities = { TEXT("get_asset_render_target"), TEXT("manage_asset_render_target") };
	Out.WhenToUse = TEXT("Create render target texture asset");
}

FCapabilityResult FCreateAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);

		const FString AssetPath = A.Str(TEXT("assetPath"));
		const int32 SizeX = static_cast<int32>(A.Num(TEXT("sizeX"), 256));
		const int32 SizeY = static_cast<int32>(A.Num(TEXT("sizeY"), 256));

		if (LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("RenderTarget already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("Failed to create package")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!RT) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("RenderTarget Createfailed")); return; }

		RT->SizeX = SizeX;
		RT->SizeY = SizeY;
		RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		RT->ClearColor = FLinearColor::Black;
		RT->UpdateResourceImmediate(true);

		FNexusAssetUtils::NotifyAndSaveCreated(Package, RT, AssetPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),    RT->GetName());
		Entry->SetStringField(TEXT("path"),    RT->GetPathName());
		Entry->SetNumberField(TEXT("sizeX"),   RT->SizeX);
		Entry->SetNumberField(TEXT("sizeY"),   RT->SizeY);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetRenderTargetCapability)
