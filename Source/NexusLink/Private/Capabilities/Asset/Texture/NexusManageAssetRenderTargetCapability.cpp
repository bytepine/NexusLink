// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Texture/NexusManageAssetRenderTargetCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "NexusMcpTool.h"

void FManageAssetRenderTargetCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_render_target");
	Out.SearchAssetTypes = {TEXT("TextureRenderTarget2D")};
	Out.Description = TEXT("修改 TextureRenderTarget2D：sizeX/sizeY/formatValue/clearColor(r,g,b,a)。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("操作"), { TEXT("set") }))
		.Prop(TEXT("sizeX"),        FNexusSchema::Int(TEXT("宽度（≥1）"), 0, 1))
		.Prop(TEXT("sizeY"),        FNexusSchema::Int(TEXT("高度（≥1）"), 0, 1))
		.Prop(TEXT("formatValue"),  FNexusSchema::Int(TEXT("ETextureRenderTargetFormat 枚举值：0=RGBA8,1=RGBA16f…")))
		.Prop(TEXT("clearColorR"),  FNexusSchema::Num(TEXT("ClearColor R [0,1]")))
		.Prop(TEXT("clearColorG"),  FNexusSchema::Num(TEXT("ClearColor G [0,1]")))
		.Prop(TEXT("clearColorB"),  FNexusSchema::Num(TEXT("ClearColor B [0,1]")))
		.Prop(TEXT("clearColorA"),  FNexusSchema::Num(TEXT("ClearColor A [0,1]")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("RenderTarget 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("render"), TEXT("target"), TEXT("resize"), TEXT("format"), TEXT("clear") };
	Out.RelatedCapabilities = { TEXT("create_asset_render_target"), TEXT("get_asset_render_target") };
}

FCapabilityResult FManageAssetRenderTargetCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		if (!Arguments.IsValid() || !Arguments->HasField(TEXT("assetPath")))
		{
			OutError = TEXT("缺少 assetPath");
			return;
		}

		const FString AssetPath = Arguments->GetStringField(TEXT("assetPath"));
		UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath);
		if (!RT)
		{
			OutError = FString::Printf(TEXT("加载 RenderTarget 失败: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("缺少 operations 或为空");
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const TSharedPtr<FJsonObject>* OpPtr = nullptr;
			if (!OpVal.IsValid() || !OpVal->TryGetObject(OpPtr) || !OpPtr)
			{
				Entry->SetStringField(TEXT("error"), TEXT("无效的 operation 项"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			const TSharedPtr<FJsonObject>& Op = *OpPtr;

			const FString Action = Op->HasField(TEXT("action")) ? Op->GetStringField(TEXT("action")).ToLower() : TEXT("");
			Entry->SetStringField(TEXT("action"), Action);
			if (Action != TEXT("set"))
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'（仅 set）"), *Action));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			bool bResized = false;
			if (Op->HasField(TEXT("sizeX"))) { RT->SizeX = FMath::Max(1, (int32)Op->GetNumberField(TEXT("sizeX"))); bResized = true; }
			if (Op->HasField(TEXT("sizeY"))) { RT->SizeY = FMath::Max(1, (int32)Op->GetNumberField(TEXT("sizeY"))); bResized = true; }
			if (Op->HasField(TEXT("formatValue")))
			{
				RT->RenderTargetFormat = ETextureRenderTargetFormat((int32)Op->GetNumberField(TEXT("formatValue")));
				bResized = true;
			}
			if (Op->HasField(TEXT("clearColorR"))) RT->ClearColor.R = (float)Op->GetNumberField(TEXT("clearColorR"));
			if (Op->HasField(TEXT("clearColorG"))) RT->ClearColor.G = (float)Op->GetNumberField(TEXT("clearColorG"));
			if (Op->HasField(TEXT("clearColorB"))) RT->ClearColor.B = (float)Op->GetNumberField(TEXT("clearColorB"));
			if (Op->HasField(TEXT("clearColorA"))) RT->ClearColor.A = (float)Op->GetNumberField(TEXT("clearColorA"));

			if (bResized)
				RT->UpdateResourceImmediate(true);

			RT->MarkPackageDirty();

			Entry->SetStringField(TEXT("name"),        RT->GetName());
			Entry->SetNumberField(TEXT("sizeX"),       RT->SizeX);
			Entry->SetNumberField(TEXT("sizeY"),       RT->SizeY);
			Entry->SetNumberField(TEXT("formatValue"), (double)(int32)RT->RenderTargetFormat);
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetRenderTargetCapability)
