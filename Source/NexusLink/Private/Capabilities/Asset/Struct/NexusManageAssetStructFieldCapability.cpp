// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Struct/NexusManageAssetStructFieldCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPinTypeUtils.h"
#if NX_UE_HAS_STRUCT_UTILS_HEADER
#include "StructUtils/UserDefinedStruct.h"
#else
#include "Engine/UserDefinedStruct.h"
#endif
#if WITH_EDITOR
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#endif
#include "NexusMcpTool.h"

void FManageAssetStructFieldCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_struct_field");
	Out.SearchAssetTypes = {TEXT("Struct")};
	Out.Description = TEXT("Batch edit UDS fields: add/remove/modify; auto-compile after changes.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("Field operation"), { TEXT("add"), TEXT("remove"), TEXT("set") }))
		.Prop(TEXT("fieldName"),    FNexusSchema::Str(TEXT("Field display name")))
		.Prop(TEXT("fieldType"),    FNexusSchema::Str(TEXT("Field type (add)")))
		.Prop(TEXT("defaultValue"), FNexusSchema::Str(TEXT("Default value (add/set)")))
		.Prop(TEXT("newName"),      FNexusSchema::Str(TEXT("New display name (set)")))
		.Prop(TEXT("newType"),      FNexusSchema::Str(TEXT("New field type (set)")))
		.Required({ TEXT("action"), TEXT("fieldName") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("UserDefinedStruct asset path (shared)")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch field ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = {
		TEXT("uds"), TEXT("field"), TEXT("member"), TEXT("schema"), TEXT("type")
	};
	Out.RelatedCapabilities = { TEXT("get_asset_struct"), TEXT("create_asset_struct"), TEXT("save_asset") };
	Out.WhenToUse = TEXT("Write ops: add/remove/modify UDS fields");
}

struct FStructFieldActionState
{
	UUserDefinedStruct* Struct = nullptr;
	bool bDidMutate = false;
};

#if WITH_EDITOR

static FStructFieldActionState* UDSState(FNexusActionContext& Ctx)
{
	return static_cast<FStructFieldActionState*>(Ctx.Target);
}

static UUserDefinedStruct* UDSFrom(FNexusActionContext& Ctx)
{
	FStructFieldActionState* S = UDSState(Ctx);
	return S ? S->Struct : nullptr;
}

static void MarkUDSMutated(FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	if (!Struct) return;
	FStructureEditorUtils::CompileStructure(Struct);
	if (FStructFieldActionState* S = UDSState(Ctx))
	{
		S->bDidMutate = true;
	}
}

static FGuid FindUDSFieldGuid(UUserDefinedStruct* Struct, const FString& Name)
{
	for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
	{
		if (Var.FriendlyName == Name) return Var.VarGuid;
	}
	return FGuid();
}

static bool RequireFieldName(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx, FString& OutName)
{
	OutName = FNexusArgs(Op).Str(TEXT("fieldName"));
	Ctx.Entry->SetStringField(TEXT("fieldName"), OutName);
	if (OutName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("action and fieldName is required"));
		return false;
	}
	return true;
}

static void HandleUDS_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	FString FieldName;
	if (!RequireFieldName(Op, Ctx, FieldName)) return;
	const FGuid Target = FindUDSFieldGuid(Struct, FieldName);
	if (!Target.IsValid())
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Field not found: %s"), *FieldName));
		return;
	}
	if (!FStructureEditorUtils::RemoveVariable(Struct, Target))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Failed to delete field"));
		return;
	}
	MarkUDSMutated(Ctx);
}

static void HandleUDS_Add(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	FString FieldName;
	if (!RequireFieldName(Op, Ctx, FieldName)) return;
	if (!Op->HasField(TEXT("fieldType")))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("fieldType is required when action=add"));
		return;
	}
	FEdGraphPinType PinType; FString TypeErr;
	if (!FNexusPinTypeUtils::ParsePinType(Op->GetStringField(TEXT("fieldType")), PinType, TypeErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TypeErr);
		return;
	}
	if (!FStructureEditorUtils::AddVariable(Struct, PinType))
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Failed to add field"));
		return;
	}
	const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);
	if (VarDescs.Num() > 0)
	{
		const FGuid NewGuid = VarDescs.Last().VarGuid;
		FStructureEditorUtils::RenameVariable(Struct, NewGuid, FieldName);
		if (Op->HasField(TEXT("defaultValue")))
			FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewGuid, Op->GetStringField(TEXT("defaultValue")));
	}
	Ctx.Entry->SetStringField(TEXT("fieldType"), Op->GetStringField(TEXT("fieldType")));
	MarkUDSMutated(Ctx);
}

static void HandleUDS_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	FString FieldName;
	if (!RequireFieldName(Op, Ctx, FieldName)) return;
	const FGuid Target = FindUDSFieldGuid(Struct, FieldName);
	if (!Target.IsValid())
	{
		Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Field not found: %s"), *FieldName));
		return;
	}
	bool bChanged = false;
	if (Op->HasField(TEXT("newType")))
	{
		FEdGraphPinType PinType; FString TypeErr;
		if (!FNexusPinTypeUtils::ParsePinType(Op->GetStringField(TEXT("newType")), PinType, TypeErr))
			Ctx.Entry->SetStringField(TEXT("error"), TypeErr);
		else { FStructureEditorUtils::ChangeVariableType(Struct, Target, PinType); bChanged = true; }
	}
	if (Op->HasField(TEXT("defaultValue")))
	{
		FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Target, Op->GetStringField(TEXT("defaultValue")));
		bChanged = true;
	}
	if (Op->HasField(TEXT("newName")))
	{
		FStructureEditorUtils::RenameVariable(Struct, Target, Op->GetStringField(TEXT("newName")));
		bChanged = true;
	}
	if (!bChanged)
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("Provide at least one of newName / newType / defaultValue"));
		return;
	}
	MarkUDSMutated(Ctx);
}

#endif // WITH_EDITOR

bool FManageAssetStructFieldCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	OutError = TEXT("manage_asset_struct_field only available in editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UUserDefinedStruct* Struct = FNexusAssetUtils::LoadAssetWithFallback<UUserDefinedStruct>(AssetPath);
	if (!Struct)
	{
		OutError = FString::Printf(TEXT("UserDefinedStruct not found: %s"), *AssetPath);
		return false;
	}
	FStructFieldActionState* State = new FStructFieldActionState();
	State->Struct = Struct;
	OutTarget = State;
	return true;
#endif
}

void FManageAssetStructFieldCapability::FinalizeTarget(void* Target) const
{
	FStructFieldActionState* State = static_cast<FStructFieldActionState*>(Target);
	if (!State) return;
	if (State->bDidMutate && State->Struct)
	{
		State->Struct->MarkPackageDirty();
	}
	delete State;
}

void FManageAssetStructFieldCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("remove"), &HandleUDS_Remove);
	OutHandlers.Add(TEXT("add"),    &HandleUDS_Add);
	OutHandlers.Add(TEXT("set"),    &HandleUDS_Set);
#endif
}

REGISTER_MCP_CAPABILITY(FManageAssetStructFieldCapability)
