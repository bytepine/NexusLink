// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusGetAssetTextureCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FGetAssetTextureCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_texture");
	Out.SearchAssetTypes = {TEXT("Texture2D")};
	Out.Description = TEXT("检查 Texture2D 快照。尺寸/压缩/sRGB/LOD。写用 manage_asset_texture。");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("Texture2D 资产路径")))
		.Required({ TEXT("assetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("texture"), TEXT("image"), TEXT("png"), TEXT("size"), TEXT("compress") };
	Out.RelatedCapabilities = { TEXT("manage_asset_texture"), TEXT("search_asset"), TEXT("get_asset_refs"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("读贴图元数据；写用 manage_asset_texture");
}

FCapabilityResult FGetAssetTextureCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString Path;
		if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("assetPath"), Path) || Path.IsEmpty())
		{
			OutError = TEXT("需要 assetPath");
			return;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), Path);

		UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(Path);
		if (!Tex)
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Texture2D 未找到: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Tex->GetName());
		Entry->SetStringField(TEXT("assetType"), TEXT("Texture2D"));

		int32 TexW = 0;
		int32 TexH = 0;
		FNexusAssetUtils::GetTexture2DSurfaceSize(Tex, TexW, TexH);
		Entry->SetNumberField(TEXT("width"),  TexW);
		Entry->SetNumberField(TEXT("height"), TexH);
		Entry->SetNumberField(TEXT("pixelFormat"), static_cast<double>(static_cast<int32>(Tex->GetPixelFormat())));
		Entry->SetNumberField(TEXT("compressionSettings"), static_cast<double>(static_cast<int32>(Tex->CompressionSettings)));
		Entry->SetNumberField(TEXT("lodGroup"), static_cast<double>(static_cast<int32>(Tex->LODGroup)));
		Entry->SetBoolField(TEXT("sRGB"), Tex->SRGB);
		Entry->SetNumberField(TEXT("mipCount"), Tex->GetNumMips());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FGetAssetTextureCapability)
