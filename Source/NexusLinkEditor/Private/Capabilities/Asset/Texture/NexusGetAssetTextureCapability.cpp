// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusGetAssetTextureCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/Texture2D.h"
#include "NexusMcpTool.h"

void FNexusGetAssetTextureCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetAssetTexture");
	Out.SearchAssetTypes = {TEXT("Texture2D")};
	Out.Description = TEXT("Inspect Texture2D snapshot. Writes via ManageAssetTexture.");
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"), FNexusSchema::Str(TEXT("Texture2D asset path")))
		.Required({ TEXT("AssetPath") })
		.Build();
	Out.Tags = { FNexusMcpTags::Readonly, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("Texture"), TEXT("image"), TEXT("png"), TEXT("size"), TEXT("compress") };
	Out.RelatedCapabilities = { TEXT("ManageAssetTexture"), TEXT("SearchAsset"), TEXT("GetAssetRefs"), TEXT("SaveAsset") };
	Out.WhenToUse = TEXT("Read texture metadata; use ManageAssetTexture for writes");
}

FCapabilityResult FNexusGetAssetTextureCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString Path = A.Str(TEXT("AssetPath"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Path"), Path);

		UTexture2D* Tex = FNexusAssetUtils::LoadAssetWithFallback<UTexture2D>(Path);
		if (!Tex)
		{
			Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Texture2D not found: %s"), *Path));
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
			return;
		}

		Entry->SetStringField(TEXT("name"), Tex->GetName());
		Entry->SetStringField(TEXT("AssetType"), TEXT("Texture2D"));

		int32 TexW = 0;
		int32 TexH = 0;
		FNexusAssetUtils::GetTexture2DSurfaceSize(Tex, TexW, TexH);
		Entry->SetNumberField(TEXT("Width"),  TexW);
		Entry->SetNumberField(TEXT("Height"), TexH);
		Entry->SetNumberField(TEXT("PixelFormat"), static_cast<double>(static_cast<int32>(Tex->GetPixelFormat())));
		Entry->SetNumberField(TEXT("CompressionSettings"), static_cast<double>(static_cast<int32>(Tex->CompressionSettings)));
		Entry->SetNumberField(TEXT("LODGroup"), static_cast<double>(static_cast<int32>(Tex->LODGroup)));
		Entry->SetBoolField(TEXT("bSRGB"), Tex->SRGB);
		Entry->SetNumberField(TEXT("MipCount"), Tex->GetNumMips());

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
	});
}

REGISTER_MCP_CAPABILITY(FNexusGetAssetTextureCapability)
