// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/GeometryCollection/NexusManageAssetGeometryCollectionCapability.h"
#if WITH_GEOMETRY_COLLECTION
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "NexusMcpTool.h"

void FManageAssetGeometryCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_geometry_collection");
	Out.SearchAssetTypes = {TEXT("GeometryCollection")};
	Out.Description = TEXT("批量编辑 GeometryCollection。operations[].action=set_damage_threshold/set_property。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("set_damage_threshold"), TEXT("set_property") }))
		.Prop(TEXT("index"), FNexusSchema::Int(TEXT("阈值索引（set_damage_threshold）"), 0))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("阈值或属性值")))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Required({ TEXT("action") })
		.Build();
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"), FNexusSchema::Str(TEXT("GeometryCollection 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Data };
	Out.ExtraSearchKeywords = { TEXT("chaos"), TEXT("damage"), TEXT("threshold") };
	Out.RelatedCapabilities = { TEXT("get_asset_geometry_collection"), TEXT("create_asset_geometry_collection") };
}

FCapabilityResult FManageAssetGeometryCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;
		UGeometryCollection* GC = FNexusAssetUtils::LoadAssetWithFallback<UGeometryCollection>(AssetPath);
		if (!GC)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("加载 GeometryCollection 失败: %s"), *AssetPath));
			return;
		}
		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
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
			if (Action == TEXT("set_damage_threshold"))
			{
				if (!Op->HasField(TEXT("value")))
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_damage_threshold 需要 value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				const int32 Idx = Op->HasField(TEXT("index")) ? static_cast<int32>(Op->GetNumberField(TEXT("index"))) : 0;
				const float Val = static_cast<float>(Op->GetNumberField(TEXT("value")));
				if (GC->DamageThreshold.Num() == 0) GC->DamageThreshold.Add(Val);
				else if (GC->DamageThreshold.IsValidIndex(Idx)) GC->DamageThreshold[Idx] = Val;
				else GC->DamageThreshold.Add(Val);
				bDirty = true;
				Entry->SetNumberField(TEXT("index"), Idx);
				Entry->SetNumberField(TEXT("value"), Val);
			}
			else if (Action == TEXT("set_property"))
			{
				FString PropPath, Value;
				Op->TryGetStringField(TEXT("propertyPath"), PropPath);
				Op->TryGetStringField(TEXT("value"), Value);
				if (PropPath.IsEmpty() || Value.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString OldVal, ActualVal, Err;
				if (!FNexusPropertyUtils::WritePropertyAndEcho(GC, { PropPath }, 0, Value, OldVal, ActualVal, Err))
				{
					Entry->SetStringField(TEXT("error"), Err);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				bDirty = true;
				Entry->SetStringField(TEXT("propertyPath"), PropPath);
			}
			else
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("不支持的操作: '%s'"), *Action));
			}
			OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		if (bDirty) GC->MarkPackageDirty();
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetGeometryCollectionCapability)
#endif
