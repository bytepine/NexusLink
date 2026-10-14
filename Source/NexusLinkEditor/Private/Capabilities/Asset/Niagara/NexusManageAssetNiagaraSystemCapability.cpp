// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusManageAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "Utils/NexusArgs.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NexusMcpTool.h"
#include "UObject/Package.h"
#if WITH_EDITOR
#include "Utils/NexusNiagaraGraphUtils.h"
#endif

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"
#include "Math/Color.h"
#endif

void FNexusManageAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("ManageAssetNiagaraSystem");
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
	Out.Description = TEXT("Batch edit Niagara. Emitter CRUD/module stack/RI params. Editor-only module ops.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetProperty"), TEXT("SetUserParameter"),
			  TEXT("SetEmitterEnabled"), TEXT("RenameEmitter"),
			  TEXT("AddEmitter"), TEXT("RemoveEmitter"),
			  TEXT("AddModule"), TEXT("RemoveModule"),
			  TEXT("SetModuleParameter") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Prop(TEXT("ParameterName"), FNexusSchema::Str(TEXT("User param or module input short name (e.g. SpawnRate)")))
		.Prop(TEXT("EmitterName"), FNexusSchema::Str(TEXT("Emitter name")))
		.Prop(TEXT("NewName"), FNexusSchema::Str(TEXT("RenameEmitter new name")))
		.Prop(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("SetEmitterEnabled")))
		.Prop(TEXT("EmitterPath"), FNexusSchema::Str(TEXT("AddEmitter: existing NiagaraEmitter path; omit to create empty emitter")))
		.Prop(TEXT("ModulePath"), FNexusSchema::Str(TEXT("AddModule: NiagaraScript module path")))
		.Prop(TEXT("ModuleName"), FNexusSchema::Str(TEXT("RemoveModule / SetModuleParameter FunctionCall name")))
		.Prop(TEXT("Usage"), FNexusSchema::Enum(TEXT("AddModule / SetModuleParameter script slot"),
			{ TEXT("Spawn"), TEXT("Update"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") }))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New value formats; other structs via ImportText")))
		.Required({ TEXT("Action") })
		.Build();
#else
	Out.Description = TEXT("Batch edit Niagara. Emitter CRUD/module stack/RI params. Editor-only module ops.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("Action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("SetProperty"), TEXT("SetEmitterEnabled"), TEXT("RenameEmitter"),
			  TEXT("AddEmitter"), TEXT("RemoveEmitter"),
			  TEXT("AddModule"), TEXT("RemoveModule"),
			  TEXT("SetModuleParameter") }))
		.Prop(TEXT("PropertyPath"), FNexusSchema::Str(TEXT("Property path (SetProperty)")))
		.Prop(TEXT("ParameterName"), FNexusSchema::Str(TEXT("Module input short name (SetModuleParameter, e.g. SpawnRate)")))
		.Prop(TEXT("EmitterName"), FNexusSchema::Str(TEXT("Emitter name")))
		.Prop(TEXT("NewName"), FNexusSchema::Str(TEXT("RenameEmitter new name")))
		.Prop(TEXT("bEnabled"), FNexusSchema::Bool(TEXT("SetEmitterEnabled")))
		.Prop(TEXT("EmitterPath"), FNexusSchema::Str(TEXT("AddEmitter: existing NiagaraEmitter path; omit to create empty emitter")))
		.Prop(TEXT("ModulePath"), FNexusSchema::Str(TEXT("AddModule: NiagaraScript module path")))
		.Prop(TEXT("ModuleName"), FNexusSchema::Str(TEXT("RemoveModule / SetModuleParameter FunctionCall name")))
		.Prop(TEXT("Usage"), FNexusSchema::Enum(TEXT("AddModule / SetModuleParameter script slot"),
			{ TEXT("Spawn"), TEXT("Update"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") }))
		.Prop(TEXT("Value"), FNexusSchema::Str(TEXT("New value string")))
		.Required({ TEXT("Action") })
		.Build();
#endif
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("AssetPath"),  FNexusSchema::Str(TEXT("NiagaraSystem asset path")))
		.Prop(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("AssetPath"), TEXT("Operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle"), TEXT("fx"), TEXT("parameter") };
	Out.RelatedCapabilities = { TEXT("GetAssetNiagaraSystem"), TEXT("CreateAssetNiagaraSystem"), TEXT("SearchAsset") };
	Out.Prerequisites = { TEXT("EditorOnly") };
	Out.WhenToUse = TEXT("Edit emitters/module stack/RI inputs; GetAssetNiagaraSystem to read");
}

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
/** 按逗号或空白拆分数值分量。 */
static bool ParseNiagaraFloatParts(const FString& Value, int32 Expected, TArray<float>& OutParts, FString& OutError, const TCHAR* FormatHint)
{
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != Expected)
	{
		Value.ParseIntoArrayWS(Parts);
	}
	if (Parts.Num() != Expected)
	{
		OutError = FString::Printf(TEXT("%s"), FormatHint);
		return false;
	}
	OutParts.Reset();
	OutParts.Reserve(Expected);
	for (const FString& P : Parts)
	{
		OutParts.Add(FCString::Atof(*P.TrimStartAndEnd()));
	}
	return true;
}

static bool SetNiagaraUserParameter(UNiagaraSystem* System, const FString& ParamName, const FString& Value, FString& OutError)
{
	if (!System || ParamName.IsEmpty())
	{
		OutError = TEXT("Invalid System or ParameterName");
		return false;
	}
	FNiagaraParameterStore& Store = System->GetExposedParameters();
	for (const FNiagaraVariableWithOffset& Var : Store.ReadParameterVariables())
	{
		if (!Var.GetName().ToString().Equals(ParamName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FNiagaraTypeDefinition& TypeDef = Var.GetType();
		const FName TypeName = TypeDef.GetFName();
		TArray<uint8> Data;
		Data.SetNumZeroed(TypeDef.GetSize());

		if (TypeName == FName(TEXT("float")) || TypeName == FName(TEXT("Float")))
		{
			const float FVal = FCString::Atof(*Value);
			FMemory::Memcpy(Data.GetData(), &FVal, sizeof(float));
		}
		else if (TypeName == FName(TEXT("int32")) || TypeName == FName(TEXT("Int32"))
			|| TypeName == FName(TEXT("int")) || TypeName == FName(TEXT("Integer")))
		{
			const int32 IVal = FCString::Atoi(*Value);
			FMemory::Memcpy(Data.GetData(), &IVal, sizeof(int32));
		}
		else if (TypeName == FName(TEXT("bool")) || TypeName == FName(TEXT("Bool")))
		{
			const bool BVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			FMemory::Memcpy(Data.GetData(), &BVal, sizeof(bool));
		}
		else if (TypeName == FName(TEXT("Vector2D")) || TypeName == FName(TEXT("Vector2"))
			|| TypeName == FName(TEXT("Vec2")))
		{
			TArray<float> Parts;
			if (!ParseNiagaraFloatParts(Value, 2, Parts, OutError, TEXT("Vector2 parameter value format must be x,y")))
			{
				return false;
			}
			if (TypeDef.GetSize() < (int32)sizeof(FVector2D))
			{
				OutError = TEXT("Vector2 parameter storage size mismatch");
				return false;
			}
			const FVector2D V(Parts[0], Parts[1]);
			FMemory::Memcpy(Data.GetData(), &V, sizeof(FVector2D));
		}
		else if (TypeName == FName(TEXT("Vector")) || TypeName == FName(TEXT("Vector3"))
			|| TypeName == FName(TEXT("Vec3")) || TypeName == FName(TEXT("Position")))
		{
			TArray<float> Parts;
			if (!ParseNiagaraFloatParts(Value, 3, Parts, OutError, TEXT("Vector/Position parameter value format must be x,y,z")))
			{
				return false;
			}
			if (TypeDef.GetSize() < (int32)sizeof(FVector))
			{
				OutError = TEXT("Vector/Position parameter storage size mismatch");
				return false;
			}
			const FVector V(Parts[0], Parts[1], Parts[2]);
			FMemory::Memcpy(Data.GetData(), &V, sizeof(FVector));
		}
		else if (TypeName == FName(TEXT("Vector4")) || TypeName == FName(TEXT("Vec4")))
		{
			TArray<float> Parts;
			if (!ParseNiagaraFloatParts(Value, 4, Parts, OutError, TEXT("Vector4 parameter value format must be x,y,z,w")))
			{
				return false;
			}
			if (TypeDef.GetSize() < (int32)sizeof(FVector4))
			{
				OutError = TEXT("Vector4 parameter storage size mismatch");
				return false;
			}
			const FVector4 V(Parts[0], Parts[1], Parts[2], Parts[3]);
			FMemory::Memcpy(Data.GetData(), &V, sizeof(FVector4));
		}
		else if (TypeName == FName(TEXT("LinearColor")) || TypeName == FName(TEXT("Color")))
		{
			TArray<float> Parts;
			if (!ParseNiagaraFloatParts(Value, 4, Parts, OutError, TEXT("Color/LinearColor parameter value format must be r,g,b,a")))
			{
				return false;
			}
			if (TypeDef.GetSize() < (int32)sizeof(FLinearColor))
			{
				OutError = TEXT("Color parameter storage size mismatch");
				return false;
			}
			const FLinearColor C(Parts[0], Parts[1], Parts[2], Parts[3]);
			FMemory::Memcpy(Data.GetData(), &C, sizeof(FLinearColor));
		}
		else if (TypeName == FName(TEXT("Quat")) || TypeName == FName(TEXT("Quaternion")))
		{
			TArray<float> Parts;
			if (!ParseNiagaraFloatParts(Value, 4, Parts, OutError, TEXT("Quat parameter value format must be x,y,z,w")))
			{
				return false;
			}
			if (TypeDef.GetSize() < (int32)sizeof(FQuat))
			{
				OutError = TEXT("Quat parameter storage size mismatch");
				return false;
			}
			const FQuat Q(Parts[0], Parts[1], Parts[2], Parts[3]);
			FMemory::Memcpy(Data.GetData(), &Q, sizeof(FQuat));
		}
		else if (UScriptStruct* Struct = TypeDef.GetScriptStruct())
		{
			// 通用回退：UE ImportText（Matrix 等结构体）
			Struct->InitializeStruct(Data.GetData());
			const TCHAR* Result = Struct->ImportText(*Value, Data.GetData(), nullptr, PPF_None, nullptr, Struct->GetName());
			if (!Result)
			{
				OutError = FString::Printf(TEXT("Unable to parse struct parameter %s, value: %s"), *TypeName.ToString(), *Value);
				return false;
			}
		}
		else
		{
			OutError = FString::Printf(
				TEXT("Unsupported user parameter type: %s (supported: float/int32/bool/Vector2/Vector/Position/Vector4/Color/LinearColor/Quat)"),
				*TypeName.ToString());
			return false;
		}

		FNiagaraVariable VarToSet(Var);
		Store.SetParameterData(Data.GetData(), VarToSet, false);
		return true;
	}
	OutError = FString::Printf(TEXT("User parameter not found: %s"), *ParamName);
	return false;
}
#endif

static UNiagaraSystem* NiagaraFrom(FNexusActionContext& Ctx)
{
	return static_cast<UNiagaraSystem*>(Ctx.Target);
}

static void MarkNiagaraDirty(FNexusActionContext& Ctx)
{
	if (UNiagaraSystem* System = NiagaraFrom(Ctx))
	{
		System->MarkPackageDirty();
	}
}

static int32 FindEmitterIndex(UNiagaraSystem* System, const FString& EmitterName)
{
	if (!System || EmitterName.IsEmpty()) return INDEX_NONE;
	const int32 NumEm =
#if NX_UE_HAS_NIAGARA_EMITTER_HANDLES_API
		System->GetEmitterHandles().Num();
#else
		System->GetNumEmitters();
#endif
	for (int32 i = 0; i < NumEm; ++i)
	{
		if (System->GetEmitterHandle(i).GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

static FNiagaraEmitterHandle* FindEmitterHandle(UNiagaraSystem* System, const FString& EmitterName, TSharedPtr<FJsonObject>& Entry)
{
	if (EmitterName.IsEmpty())
	{
		Entry->SetStringField(TEXT("Error"), TEXT("EmitterName required"));
		return nullptr;
	}
	const int32 FoundIdx = FindEmitterIndex(System, EmitterName);
	if (FoundIdx == INDEX_NONE)
	{
		Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
		return nullptr;
	}
	return &System->GetEmitterHandle(FoundIdx);
}

static void HandleNiagara_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("PropertyPath"));
	const FString Value = A.Str(TEXT("Value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetProperty requires PropertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(System, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), Err);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("PropertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("OldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("NewValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
static void HandleNiagara_SetUserParameter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ParamName = A.Str(TEXT("ParameterName"));
	const FString Value = A.Str(TEXT("Value"));
	if (ParamName.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetUserParameter requires ParameterName and value"));
		return;
	}
	FString ParamErr;
	if (!SetNiagaraUserParameter(System, ParamName, Value, ParamErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ParamErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("ParameterName"), ParamName);
	Ctx.Entry->SetStringField(TEXT("NewValue"), Value);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}
#endif

static void HandleNiagara_SetEmitterEnabled(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FString EmitterName = FNexusArgs(Op).Str(TEXT("EmitterName"));
	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, Ctx.Entry);
	if (!Handle) return;
	const bool bEnabled = FNexusArgs(Op).Bool(TEXT("bEnabled"), true);
	Handle->SetIsEnabled(bEnabled, *System, true);
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("bEnabled"), bEnabled);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleNiagara_RenameEmitter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("EmitterName"));
	const FString NewName = A.Str(TEXT("NewName"));
	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, Ctx.Entry);
	if (!Handle) return;
	if (NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RenameEmitter requires NewName"));
		return;
	}
	Handle->SetName(FName(*NewName), *System);
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("NewName"), NewName);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleNiagara_RemoveEmitter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FString EmitterName = FNexusArgs(Op).Str(TEXT("EmitterName"));
	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, Ctx.Entry);
	if (!Handle) return;
#if NX_UE_HAS_NIAGARA_REMOVE_EMITTER_BY_ID
	TSet<FGuid> Ids;
	Ids.Add(Handle->GetId());
	System->RemoveEmitterHandlesById(Ids);
#else
	System->RemoveEmitterHandle(*Handle);
#endif
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("bRemoved"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleNiagara_AddEmitter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterPath = A.Str(TEXT("EmitterPath"));
	const FString EmitterName = A.Str(TEXT("EmitterName"));
	UNiagaraEmitter* Emitter = nullptr;
	if (!EmitterPath.IsEmpty())
	{
		Emitter = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraEmitter>(EmitterPath);
		if (!Emitter)
		{
			Ctx.Entry->SetStringField(TEXT("Error"), FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
			return;
		}
	}
	else
	{
#if WITH_EDITOR
		FString CreateErr;
		const FName EmptyName = EmitterName.IsEmpty() ? FName(TEXT("EmptyEmitter")) : FName(*EmitterName);
		Emitter = FNexusNiagaraGraphUtils::CreateEmptyEmitter(GetTransientPackage(), EmptyName, CreateErr);
		if (!Emitter)
		{
			Ctx.Entry->SetStringField(TEXT("Error"), CreateErr);
			return;
		}
#else
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddEmitter without EmitterPath is editor-only"));
		return;
#endif
	}
	const FName AddName = EmitterName.IsEmpty() ? Emitter->GetFName() : FName(*EmitterName);
#if NX_UE_HAS_NIAGARA_ADD_EMITTER_VERSION
	System->AddEmitterHandle(*Emitter, AddName, FGuid());
#else
	System->AddEmitterHandle(*Emitter, AddName);
#endif
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), AddName.ToString());
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
}

static void HandleNiagara_AddModule(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("EmitterName"));
	const FString ModulePath = A.Str(TEXT("ModulePath"));
	const FString Usage = A.Str(TEXT("Usage"));
	if (EmitterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("EmitterName required"));
		return;
	}
	if (ModulePath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddModule requires ModulePath"));
		return;
	}
	FString AddedName, ModErr;
	if (!FNexusNiagaraGraphUtils::AddModule(System, EmitterName, ModulePath, Usage, AddedName, ModErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ModErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("ModuleName"), AddedName);
	Ctx.Entry->SetStringField(TEXT("Usage"), Usage.IsEmpty() ? TEXT("Update") : Usage);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
#else
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddModule/RemoveModule editor only"));
#endif
}

static void HandleNiagara_RemoveModule(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("EmitterName"));
	const FString ModuleName = A.Str(TEXT("ModuleName"));
	if (EmitterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("EmitterName required"));
		return;
	}
	if (ModuleName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("RemoveModule requires ModuleName"));
		return;
	}
	FString ModErr;
	if (!FNexusNiagaraGraphUtils::RemoveModule(System, EmitterName, ModuleName, ModErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ModErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("bRemoved"), ModuleName);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
#else
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("AddModule/RemoveModule editor only"));
#endif
}

static void HandleNiagara_SetModuleParameter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("EmitterName"));
	const FString ModuleName = A.Str(TEXT("ModuleName"));
	const FString ParameterName = A.Str(TEXT("ParameterName"));
	const FString Value = A.Str(TEXT("Value"));
	const FString Usage = A.Str(TEXT("Usage"));
	if (EmitterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("EmitterName required"));
		return;
	}
	if (ModuleName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetModuleParameter requires ModuleName"));
		return;
	}
	if (ParameterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetModuleParameter requires ParameterName"));
		return;
	}
	FString ModErr;
	if (!FNexusNiagaraGraphUtils::SetModuleParameter(System, EmitterName, ModuleName, ParameterName, Usage, Value, ModErr))
	{
		Ctx.Entry->SetStringField(TEXT("Error"), ModErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("EmitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("ModuleName"), ModuleName);
	Ctx.Entry->SetStringField(TEXT("ParameterName"), ParameterName);
	Ctx.Entry->SetStringField(TEXT("Value"), Value);
	if (!Usage.IsEmpty()) Ctx.Entry->SetStringField(TEXT("Usage"), Usage);
	Ctx.Entry->SetStringField(TEXT("Note"), TEXT("persist with SaveAsset"));
#else
	Ctx.Entry->SetStringField(TEXT("Error"), TEXT("SetModuleParameter editor only"));
#endif
}

bool FNexusManageAssetNiagaraSystemCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("AssetPath"));
	Entry->SetStringField(TEXT("Path"), AssetPath);
	UNiagaraSystem* System = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraSystem>(AssetPath);
	if (!System)
	{
		OutError = FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = System;
	return true;
}

void FNexusManageAssetNiagaraSystemCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("SetProperty"),        &HandleNiagara_SetProperty);
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
	OutHandlers.Add(TEXT("SetUserParameter"),  &HandleNiagara_SetUserParameter);
#endif
	OutHandlers.Add(TEXT("SetEmitterEnabled"), &HandleNiagara_SetEmitterEnabled);
	OutHandlers.Add(TEXT("RenameEmitter"),      &HandleNiagara_RenameEmitter);
	OutHandlers.Add(TEXT("RemoveEmitter"),      &HandleNiagara_RemoveEmitter);
	OutHandlers.Add(TEXT("AddEmitter"),         &HandleNiagara_AddEmitter);
	OutHandlers.Add(TEXT("AddModule"),          &HandleNiagara_AddModule);
	OutHandlers.Add(TEXT("RemoveModule"),       &HandleNiagara_RemoveModule);
	OutHandlers.Add(TEXT("SetModuleParameter"), &HandleNiagara_SetModuleParameter);
}

REGISTER_MCP_CAPABILITY(FNexusManageAssetNiagaraSystemCapability)

#endif // WITH_NIAGARA
