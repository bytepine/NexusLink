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

void FNexusManageAssetStructFieldCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetStructField");
	Out.SearchAssetTypes = {TEXT("Struct")};
	Out.Description = TEXT("Batch edit UDS fields: add/remove/modify; auto-compile after changes.");
	Out.InputSchema = [this]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> ItemSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"),       FNexusSchema::Enum(TEXT("Field operation"), { TEXT("Add"), TEXT("Remove"), TEXT("Set") }))
		.Prop(TEXT("FieldName"),    FNexusSchema::Str(TEXT("Field display name")))
		.Prop(TEXT("FieldType"),    FNexusSchema::Str(TEXT("Field type (add)")))
		.Prop(TEXT("DefaultValue"), FNexusSchema::Str(TEXT("Default value (add/set)")))
		.Prop(TEXT("NewName"),      FNexusSchema::Str(TEXT("New display name (set)")))
		.Prop(TEXT("NewType"),      FNexusSchema::Str(TEXT("New field type (set)")))
		.Required({ TEXT("Action"), TEXT("FieldName") })
		.Build();

		return FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("UserDefinedStruct asset path (shared)")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch field ops"), ItemSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	}();
	Out.Tags = {FNexusMcpTags::Write, FNexusMcpTags::Struct };
	Out.ExtraSearchKeywords = {
		TEXT("uds"), TEXT("field"), TEXT("member"), TEXT("Schema"), TEXT("type")
	};
	Out.RelatedCapabilities = { TEXT("GetAssetStruct"), TEXT("CreateAssetStruct"), TEXT("SaveAsset") };
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
	OutName = FNexusArgs(Op).Str(TEXT("FieldName"));
	Ctx.Entry->SetStringField(TEXT("FieldName"), OutName);
	if (OutName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("action and FieldName is required"));
		return false;
	}
	return true;
}

static void HandleUDS_Remove(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("UserDefinedStruct is null"));
		return;
	}
	FString FieldName;
	if (!RequireFieldName(Op, Ctx, FieldName)) return;
	const FGuid Target = FindUDSFieldGuid(Struct, FieldName);
	if (!Target.IsValid())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Field not found: %s"), *FieldName));
		return;
	}
	if (!FStructureEditorUtils::RemoveVariable(Struct, Target))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Failed to delete field"));
		return;
	}
	MarkUDSMutated(Ctx);
}

static void HandleUDS_Add(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("UserDefinedStruct is null"));
		return;
	}
	FString FieldName;
	if (!RequireFieldName(Op, Ctx, FieldName)) return;
	if (!Op->HasField(TEXT("FieldType")))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("FieldType is required when Action=add"));
		return;
	}
	FEdGraphPinType PinType; FString TypeErr;
	if (!FNexusPinTypeUtils::ParsePinType(Op->GetStringField(TEXT("FieldType")), PinType, TypeErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TypeErr);
		return;
	}
	if (!FStructureEditorUtils::AddVariable(Struct, PinType))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Failed to add field"));
		return;
	}
	const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(Struct);
	if (VarDescs.Num() > 0)
	{
		const FGuid NewGuid = VarDescs.Last().VarGuid;
		FStructureEditorUtils::RenameVariable(Struct, NewGuid, FieldName);
		if (Op->HasField(TEXT("DefaultValue")))
			FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewGuid, Op->GetStringField(TEXT("DefaultValue")));
	}
	Ctx.Entry->SetStringField(TEXT("FieldType"), Op->GetStringField(TEXT("FieldType")));
	MarkUDSMutated(Ctx);
}

static void HandleUDS_Set(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UUserDefinedStruct* Struct = UDSFrom(Ctx);
	if (!Struct)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("UserDefinedStruct is null"));
		return;
	}
	FString FieldName;
	if (!RequireFieldName(Op, Ctx, FieldName)) return;
	const FGuid Target = FindUDSFieldGuid(Struct, FieldName);
	if (!Target.IsValid())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Field not found: %s"), *FieldName));
		return;
	}
	bool bChanged = false;
	if (Op->HasField(TEXT("NewType")))
	{
		FEdGraphPinType PinType; FString TypeErr;
		if (!FNexusPinTypeUtils::ParsePinType(Op->GetStringField(TEXT("NewType")), PinType, TypeErr))
			Ctx.Entry->SetStringField(TEXT("Error"), TypeErr);
		else { FStructureEditorUtils::ChangeVariableType(Struct, Target, PinType); bChanged = true; }
	}
	if (Op->HasField(TEXT("DefaultValue")))
	{
		FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Target, Op->GetStringField(TEXT("DefaultValue")));
		bChanged = true;
	}
	if (Op->HasField(TEXT("NewName")))
	{
		FStructureEditorUtils::RenameVariable(Struct, Target, Op->GetStringField(TEXT("NewName")));
		bChanged = true;
	}
	if (!bChanged)
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("Provide at least one of NewName / NewType / DefaultValue"));
		return;
	}
	MarkUDSMutated(Ctx);
}

#endif // WITH_EDITOR

bool FNexusManageAssetStructFieldCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
#if !WITH_EDITOR
	OutError = TEXT("ManageAssetStructField only available in editor builds");
	return false;
#else
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
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

void FNexusManageAssetStructFieldCapability::FinalizeTarget(void* Target) const
{
	FStructFieldActionState* State = static_cast<FStructFieldActionState*>(Target);
	if (!State) return;
	if (State->bDidMutate && State->Struct)
	{
		State->Struct->MarkPackageDirty();
	}
	delete State;
}

void FNexusManageAssetStructFieldCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
#if WITH_EDITOR
	OutHandlers.Add(TEXT("Remove"), &HandleUDS_Remove);
	OutHandlers.Add(TEXT("Add"),    &HandleUDS_Add);
	OutHandlers.Add(TEXT("Set"),    &HandleUDS_Set);
#endif
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetStructFieldCapability)
