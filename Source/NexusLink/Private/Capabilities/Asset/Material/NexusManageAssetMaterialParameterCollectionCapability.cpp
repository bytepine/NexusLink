// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "NexusMcpTool.h"
#include "Materials/MaterialParameterCollection.h"
#if WITH_EDITOR
#include "MaterialEditorUtilities.h"
#endif

void FManageAssetMaterialParameterCollectionCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_material_parameter_collection");
	Out.SearchAssetTypes = {TEXT("MaterialParameterCollection")};
	Out.Description = TEXT("增删改 MPC 的标量/向量参数（add_scalar/add_vector/remove/set_default）。");

	TSharedPtr<FJsonObject> OpSchemaPtr = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("操作类型"),
			{ TEXT("add_scalar"), TEXT("add_vector"), TEXT("remove"), TEXT("set_scalar_default"), TEXT("set_vector_default") }))
		.Prop(TEXT("paramName"),     FNexusSchema::Str(TEXT("参数名")))
		.Prop(TEXT("defaultValue"),  FNexusSchema::Num(TEXT("标量默认值（add_scalar/set_scalar_default）")))
		.Prop(TEXT("r"), FNexusSchema::Num(TEXT("向量 R 分量")))
		.Prop(TEXT("g"), FNexusSchema::Num(TEXT("向量 G 分量")))
		.Prop(TEXT("b"), FNexusSchema::Num(TEXT("向量 B 分量")))
		.Prop(TEXT("a"), FNexusSchema::Num(TEXT("向量 A 分量")))
		.Build();
	const TSharedRef<FJsonObject> OpSchema = OpSchemaPtr.ToSharedRef();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("MPC 资产路径")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("操作列表"), OpSchema))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("scalar"), TEXT("vector"), TEXT("global") };
	Out.RelatedCapabilities = { TEXT("get_asset_material_parameter_collection"), TEXT("manage_asset_material") };
	Out.WhenToUse = TEXT("往 MPC 里增删改标量/向量参数");
}

FCapabilityResult FManageAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!Arguments->TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("assetPath 为必填项");
			return;
		}

		UMaterialParameterCollection* MPC = FNexusAssetUtils::LoadAssetWithFallback<UMaterialParameterCollection>(AssetPath);
		if (!MPC)
		{
			OutError = FString::Printf(TEXT("MaterialParameterCollection 未找到: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Ops;
		if (!Arguments->TryGetArrayField(TEXT("operations"), Ops) || !Ops)
		{
			OutError = TEXT("operations 为必填数组");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : *Ops)
		{
			TSharedPtr<FJsonObject> Op = OpVal->AsObject();
			if (!Op.IsValid()) continue;

			TSharedPtr<FJsonObject> OpResult = MakeShared<FJsonObject>();
			FString Action;
			Op->TryGetStringField(TEXT("action"), Action);

			FString ParamName;
			Op->TryGetStringField(TEXT("paramName"), ParamName);

			if (Action == TEXT("add_scalar"))
			{
				if (ParamName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_scalar 需要 paramName"));
				}
				else
				{
					// 重名检查
					bool bExists = MPC->ScalarParameters.ContainsByPredicate(
						[&](const FCollectionScalarParameter& P) { return P.ParameterName == *ParamName; });
					if (bExists)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("标量参数 '%s' 已存在"), *ParamName));
					}
					else
					{
						FCollectionScalarParameter NewParam;
						NewParam.ParameterName = *ParamName;
						double DefaultVal = 0.0;
						Op->TryGetNumberField(TEXT("defaultValue"), DefaultVal);
						NewParam.DefaultValue = static_cast<float>(DefaultVal);
						MPC->ScalarParameters.Add(NewParam);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("add_vector"))
			{
				if (ParamName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("add_vector 需要 paramName"));
				}
				else
				{
					bool bExists = MPC->VectorParameters.ContainsByPredicate(
						[&](const FCollectionVectorParameter& P) { return P.ParameterName == *ParamName; });
					if (bExists)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("向量参数 '%s' 已存在"), *ParamName));
					}
					else
					{
						FCollectionVectorParameter NewParam;
						NewParam.ParameterName = *ParamName;
						double R = 0, G = 0, B = 0, A = 1;
						Op->TryGetNumberField(TEXT("r"), R);
						Op->TryGetNumberField(TEXT("g"), G);
						Op->TryGetNumberField(TEXT("b"), B);
						Op->TryGetNumberField(TEXT("a"), A);
						NewParam.DefaultValue = FLinearColor(
							static_cast<float>(R), static_cast<float>(G),
							static_cast<float>(B), static_cast<float>(A));
						MPC->VectorParameters.Add(NewParam);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove"))
			{
				if (ParamName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove 需要 paramName"));
				}
				else
				{
					int32 SBefore = MPC->ScalarParameters.Num();
					int32 VBefore = MPC->VectorParameters.Num();
					MPC->ScalarParameters.RemoveAll(
						[&](const FCollectionScalarParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
					MPC->VectorParameters.RemoveAll(
						[&](const FCollectionVectorParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
					int32 Removed = (SBefore - MPC->ScalarParameters.Num()) + (VBefore - MPC->VectorParameters.Num());
					OpResult->SetNumberField(TEXT("removedCount"), Removed);
					if (Removed > 0) bDirty = true;
				}
			}
			else if (Action == TEXT("set_scalar_default"))
			{
				if (ParamName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_scalar_default 需要 paramName"));
				}
				else
				{
					FCollectionScalarParameter* Found = MPC->ScalarParameters.FindByPredicate(
						[&](const FCollectionScalarParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
					if (!Found)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到标量参数: %s"), *ParamName));
					}
					else
					{
						double DefaultVal = Found->DefaultValue;
						Op->TryGetNumberField(TEXT("defaultValue"), DefaultVal);
						Found->DefaultValue = static_cast<float>(DefaultVal);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("set_vector_default"))
			{
				if (ParamName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("set_vector_default 需要 paramName"));
				}
				else
				{
					FCollectionVectorParameter* Found = MPC->VectorParameters.FindByPredicate(
						[&](const FCollectionVectorParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
					if (!Found)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未找到向量参数: %s"), *ParamName));
					}
					else
					{
						double R = Found->DefaultValue.R, G = Found->DefaultValue.G;
						double B = Found->DefaultValue.B, A = Found->DefaultValue.A;
						Op->TryGetNumberField(TEXT("r"), R);
						Op->TryGetNumberField(TEXT("g"), G);
						Op->TryGetNumberField(TEXT("b"), B);
						Op->TryGetNumberField(TEXT("a"), A);
						Found->DefaultValue = FLinearColor(
							static_cast<float>(R), static_cast<float>(G),
							static_cast<float>(B), static_cast<float>(A));
						bDirty = true;
					}
				}
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
			}

			OutEntries.Add(MakeShared<FJsonValueObject>(OpResult));
		}

		if (bDirty)
		{
			MPC->MarkPackageDirty();
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetMaterialParameterCollectionCapability)
