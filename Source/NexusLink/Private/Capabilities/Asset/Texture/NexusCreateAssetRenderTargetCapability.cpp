// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusCreateAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FCreateAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("create_asset_render_target");
	Out.Description = TEXT("创建 TextureRenderTarget2D 资产；用 manage 修改尺寸/格式。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("资产包路径")))
		.Prop(TEXT("sizeX"),     FNexusSchema::Int(TEXT("宽度（默认256）"), 256, 1))
		.Prop(TEXT("sizeY"),     FNexusSchema::Int(TEXT("高度（默认256）"), 256, 1))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("target"), TEXT("texture"), TEXT("rt"), TEXT("offscreen") };
	Out.RelatedCapabilities = { TEXT("get_asset_render_target"), TEXT("manage_asset_render_target") };
	Out.WhenToUse = TEXT("创建渲染目标纹理资产");
}

FCapabilityResult FCreateAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		const int32 SizeX = Arguments->HasField(TEXT("sizeX")) ? (int32)Arguments->GetNumberField(TEXT("sizeX")) : 256;
		const int32 SizeY = Arguments->HasField(TEXT("sizeY")) ? (int32)Arguments->GetNumberField(TEXT("sizeY")) : 256;

		if (LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath))
		{
			FNexusCapabilityResultBuilder::AddEntryError(OutEntries,
				FString::Printf(TEXT("RenderTarget already exists: %s"), *AssetPath));
			return;
		}

		UPackage* Package = CreatePackage(*AssetPath);
		if (!Package) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("创建包失败")); return; }

		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!RT) { FNexusCapabilityResultBuilder::AddEntryError(OutEntries, TEXT("RenderTarget 创建失败")); return; }

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
		Entry->SetBoolField(TEXT("success"),   true);
		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FCreateAssetRenderTargetCapability)
