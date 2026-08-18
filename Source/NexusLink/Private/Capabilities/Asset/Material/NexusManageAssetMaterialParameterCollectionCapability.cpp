// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusCapabilityResultBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("Add/remove/edit MPC scalar/vector params (add_scalar/add_vector/remove/set_default).");

	TSharedPtr<FJsonObject> OpSchemaPtr = FNexusSchema::Object()
		.Required(TEXT("action"), FNexusSchema::Enum(
			TEXT("Operation type"),
			{ TEXT("add_scalar"), TEXT("add_vector"), TEXT("remove"), TEXT("set_scalar_default"), TEXT("set_vector_default") }))
		.Prop(TEXT("paramName"),     FNexusSchema::Str(TEXT("Parameter name")))
		.Prop(TEXT("defaultValue"),  FNexusSchema::Num(TEXT("Scalar default (add_scalar/set_scalar_default)")))
		.Prop(TEXT("r"), FNexusSchema::Num(TEXT("Vector R component")))
		.Prop(TEXT("g"), FNexusSchema::Num(TEXT("Vector G component")))
		.Prop(TEXT("b"), FNexusSchema::Num(TEXT("Vector B component")))
		.Prop(TEXT("a"), FNexusSchema::Num(TEXT("Vector A component")))
		.Build();
	const TSharedRef<FJsonObject> OpSchema = OpSchemaPtr.ToSharedRef();

	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("MPC asset path")))
		.Required(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Operation list"), OpSchema))
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Material };
	Out.ExtraSearchKeywords = { TEXT("mpc"), TEXT("parameter"), TEXT("collection"), TEXT("scalar"), TEXT("vector"), TEXT("global") };
	Out.RelatedCapabilities = { TEXT("get_asset_material_parameter_collection"), TEXT("manage_asset_material") };
	Out.WhenToUse = TEXT("Add/remove/edit MPC scalar/vector parameters");
}

FCapabilityResult FManageAssetMaterialParameterCollectionCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		const FNexusArgs A(Arguments);
		const FString AssetPath = A.Str(TEXT("assetPath"));

		UMaterialParameterCollection* MPC = FNexusAssetUtils::LoadAssetWithFallback<UMaterialParameterCollection>(AssetPath);
		if (!MPC)
		{
			OutError = FString::Printf(TEXT("MaterialParameterCollection not found: %s"), *AssetPath);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			OutError = TEXT("operations is a required array");
			return;
		}

		bool bDirty = false;

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
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
					OpResult->SetStringField(TEXT("error"), TEXT("add_scalar requires paramName"));
				}
				else
				{
					// 重名检查
					bool bExists = MPC->ScalarParameters.ContainsByPredicate(
						[&](const FCollectionScalarParameter& P) { return P.ParameterName == *ParamName; });
					if (bExists)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Scalar parameter '%s' already exists"), *ParamName));
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
					OpResult->SetStringField(TEXT("error"), TEXT("add_vector requires paramName"));
				}
				else
				{
					bool bExists = MPC->VectorParameters.ContainsByPredicate(
						[&](const FCollectionVectorParameter& P) { return P.ParameterName == *ParamName; });
					if (bExists)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Vector parameter '%s' already exists"), *ParamName));
					}
					else
					{
						FCollectionVectorParameter NewParam;
						NewParam.ParameterName = *ParamName;
						double R = 0, G = 0, B = 0, Alpha = 1;
						Op->TryGetNumberField(TEXT("r"), R);
						Op->TryGetNumberField(TEXT("g"), G);
						Op->TryGetNumberField(TEXT("b"), B);
						Op->TryGetNumberField(TEXT("a"), Alpha);
						NewParam.DefaultValue = FLinearColor(
							static_cast<float>(R), static_cast<float>(G),
							static_cast<float>(B), static_cast<float>(Alpha));
						MPC->VectorParameters.Add(NewParam);
						bDirty = true;
					}
				}
			}
			else if (Action == TEXT("remove"))
			{
				if (ParamName.IsEmpty())
				{
					OpResult->SetStringField(TEXT("error"), TEXT("remove requires paramName"));
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
					OpResult->SetStringField(TEXT("error"), TEXT("set_scalar_default requires paramName"));
				}
				else
				{
					FCollectionScalarParameter* Found = MPC->ScalarParameters.FindByPredicate(
						[&](const FCollectionScalarParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
					if (!Found)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Scalar parameter not found: %s"), *ParamName));
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
					OpResult->SetStringField(TEXT("error"), TEXT("set_vector_default requires paramName"));
				}
				else
				{
					FCollectionVectorParameter* Found = MPC->VectorParameters.FindByPredicate(
						[&](const FCollectionVectorParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
					if (!Found)
					{
						OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Vector parameter not found: %s"), *ParamName));
					}
					else
					{
						double R = Found->DefaultValue.R, G = Found->DefaultValue.G;
						double B = Found->DefaultValue.B, Alpha = Found->DefaultValue.A;
						Op->TryGetNumberField(TEXT("r"), R);
						Op->TryGetNumberField(TEXT("g"), G);
						Op->TryGetNumberField(TEXT("b"), B);
						Op->TryGetNumberField(TEXT("a"), Alpha);
						Found->DefaultValue = FLinearColor(
							static_cast<float>(R), static_cast<float>(G),
							static_cast<float>(B), static_cast<float>(Alpha));
						bDirty = true;
					}
				}
			}
			else
			{
				OpResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
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
