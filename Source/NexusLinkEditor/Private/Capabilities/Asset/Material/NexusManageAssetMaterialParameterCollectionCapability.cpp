// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Material/NexusManageAssetMaterialParameterCollectionCapability.h"
#include "Utils/NexusArgs.h"
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

struct FMPCActionState
{
	UMaterialParameterCollection* MPC = nullptr;
	bool bDirty = false;
};

static FMPCActionState* MPCState(FNexusActionContext& Ctx)
{
	return static_cast<FMPCActionState*>(Ctx.Target);
}

static UMaterialParameterCollection* MPCFrom(FNexusActionContext& Ctx)
{
	FMPCActionState* S = MPCState(Ctx);
	return S ? S->MPC : nullptr;
}

static void MarkMPCDirty(FNexusActionContext& Ctx)
{
	if (FMPCActionState* S = MPCState(Ctx))
	{
		S->bDirty = true;
	}
}

static FString RequireParamName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, const TCHAR* Err)
{
	FString ParamName;
	Op->TryGetStringField(TEXT("paramName"), ParamName);
	if (ParamName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
	}
	return ParamName;
}

static void HandleMPC_AddScalar(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMaterialParameterCollection* MPC = MPCFrom(Ctx);
	const FString ParamName = RequireParamName(Op, Ctx, TEXT("add_scalar requires paramName"));
	if (ParamName.IsEmpty()) return;
	const bool bExists = MPC->ScalarParameters.ContainsByPredicate(
		[&](const FCollectionScalarParameter& P) { return P.ParameterName == *ParamName; });
	if (bExists)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Scalar parameter '%s' already exists"), *ParamName));
		return;
	}
	FCollectionScalarParameter NewParam;
	NewParam.ParameterName = *ParamName;
	double DefaultVal = 0.0;
	Op->TryGetNumberField(TEXT("defaultValue"), DefaultVal);
	NewParam.DefaultValue = static_cast<float>(DefaultVal);
	MPC->ScalarParameters.Add(NewParam);
	MarkMPCDirty(Ctx);
}

static void HandleMPC_AddVector(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMaterialParameterCollection* MPC = MPCFrom(Ctx);
	const FString ParamName = RequireParamName(Op, Ctx, TEXT("add_vector requires paramName"));
	if (ParamName.IsEmpty()) return;
	const bool bExists = MPC->VectorParameters.ContainsByPredicate(
		[&](const FCollectionVectorParameter& P) { return P.ParameterName == *ParamName; });
	if (bExists)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Vector parameter '%s' already exists"), *ParamName));
		return;
	}
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
	MarkMPCDirty(Ctx);
}

static void HandleMPC_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMaterialParameterCollection* MPC = MPCFrom(Ctx);
	const FString ParamName = RequireParamName(Op, Ctx, TEXT("remove requires paramName"));
	if (ParamName.IsEmpty()) return;
	const int32 SBefore = MPC->ScalarParameters.Num();
	const int32 VBefore = MPC->VectorParameters.Num();
	MPC->ScalarParameters.RemoveAll(
		[&](const FCollectionScalarParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
	MPC->VectorParameters.RemoveAll(
		[&](const FCollectionVectorParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
	const int32 Removed = (SBefore - MPC->ScalarParameters.Num()) + (VBefore - MPC->VectorParameters.Num());
	Ctx.Entry->SetNumberField(TEXT("removedCount"), Removed);
	if (Removed > 0) MarkMPCDirty(Ctx);
}

static void HandleMPC_SetScalarDefault(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMaterialParameterCollection* MPC = MPCFrom(Ctx);
	const FString ParamName = RequireParamName(Op, Ctx, TEXT("set_scalar_default requires paramName"));
	if (ParamName.IsEmpty()) return;
	FCollectionScalarParameter* Found = MPC->ScalarParameters.FindByPredicate(
		[&](const FCollectionScalarParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
	if (!Found)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Scalar parameter not found: %s"), *ParamName));
		return;
	}
	double DefaultVal = Found->DefaultValue;
	Op->TryGetNumberField(TEXT("defaultValue"), DefaultVal);
	Found->DefaultValue = static_cast<float>(DefaultVal);
	MarkMPCDirty(Ctx);
}

static void HandleMPC_SetVectorDefault(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UMaterialParameterCollection* MPC = MPCFrom(Ctx);
	const FString ParamName = RequireParamName(Op, Ctx, TEXT("set_vector_default requires paramName"));
	if (ParamName.IsEmpty()) return;
	FCollectionVectorParameter* Found = MPC->VectorParameters.FindByPredicate(
		[&](const FCollectionVectorParameter& P) { return P.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase); });
	if (!Found)
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Vector parameter not found: %s"), *ParamName));
		return;
	}
	double R = Found->DefaultValue.R, G = Found->DefaultValue.G;
	double B = Found->DefaultValue.B, Alpha = Found->DefaultValue.A;
	Op->TryGetNumberField(TEXT("r"), R);
	Op->TryGetNumberField(TEXT("g"), G);
	Op->TryGetNumberField(TEXT("b"), B);
	Op->TryGetNumberField(TEXT("a"), Alpha);
	Found->DefaultValue = FLinearColor(
		static_cast<float>(R), static_cast<float>(G),
		static_cast<float>(B), static_cast<float>(Alpha));
	MarkMPCDirty(Ctx);
}

bool FManageAssetMaterialParameterCollectionCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UMaterialParameterCollection* MPC = FNexusAssetUtils::LoadAssetWithFallback<UMaterialParameterCollection>(AssetPath);
	if (!MPC)
	{
		OutError = FString::Printf(TEXT("MaterialParameterCollection not found: %s"), *AssetPath);
		return false;
	}
	FMPCActionState* State = new FMPCActionState();
	State->MPC = MPC;
	OutTarget = State;
	return true;
}

void FManageAssetMaterialParameterCollectionCapability::FinalizeTarget(void* Target) const
{
	FMPCActionState* State = static_cast<FMPCActionState*>(Target);
	if (!State) return;
	if (State->bDirty && State->MPC)
	{
		State->MPC->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetMaterialParameterCollectionCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("add_scalar"),          &HandleMPC_AddScalar);
	OutHandlers.Add(TEXT("add_vector"),          &HandleMPC_AddVector);
	OutHandlers.Add(TEXT("remove"),              &HandleMPC_Remove);
	OutHandlers.Add(TEXT("set_scalar_default"),  &HandleMPC_SetScalarDefault);
	OutHandlers.Add(TEXT("set_vector_default"),  &HandleMPC_SetVectorDefault);
}

REGISTER_MCP_CAPABILITY(FManageAssetMaterialParameterCollectionCapability)
