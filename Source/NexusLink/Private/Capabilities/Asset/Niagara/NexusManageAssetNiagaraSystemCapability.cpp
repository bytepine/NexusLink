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

void FManageAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_niagara_system");
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
	Out.Description = TEXT("Batch edit Niagara. Emitter CRUD/module stack/RI params. Editor-only module ops.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_property"), TEXT("set_user_parameter"),
			  TEXT("set_emitter_enabled"), TEXT("rename_emitter"),
			  TEXT("add_emitter"), TEXT("remove_emitter"),
			  TEXT("add_module"), TEXT("remove_module"),
			  TEXT("set_module_parameter") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("parameterName"), FNexusSchema::Str(TEXT("User param or module input short name (e.g. SpawnRate)")))
		.Prop(TEXT("emitterName"), FNexusSchema::Str(TEXT("Emitter name")))
		.Prop(TEXT("newName"), FNexusSchema::Str(TEXT("rename_emitter new name")))
		.Prop(TEXT("enabled"), FNexusSchema::Bool(TEXT("set_emitter_enabled")))
		.Prop(TEXT("emitterPath"), FNexusSchema::Str(TEXT("add_emitter: existing NiagaraEmitter path; omit to create empty emitter")))
		.Prop(TEXT("modulePath"), FNexusSchema::Str(TEXT("add_module: NiagaraScript module path")))
		.Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("remove_module / set_module_parameter FunctionCall name")))
		.Prop(TEXT("usage"), FNexusSchema::Enum(TEXT("add_module / set_module_parameter script slot"),
			{ TEXT("Spawn"), TEXT("Update"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") }))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New value formats; other structs via ImportText")))
		.Required({ TEXT("action") })
		.Build();
#else
	Out.Description = TEXT("Batch edit Niagara. Emitter CRUD/module stack/RI params. Editor-only module ops.");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_property"), TEXT("set_emitter_enabled"), TEXT("rename_emitter"),
			  TEXT("add_emitter"), TEXT("remove_emitter"),
			  TEXT("add_module"), TEXT("remove_module"),
			  TEXT("set_module_parameter") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("parameterName"), FNexusSchema::Str(TEXT("Module input short name (set_module_parameter, e.g. SpawnRate)")))
		.Prop(TEXT("emitterName"), FNexusSchema::Str(TEXT("Emitter name")))
		.Prop(TEXT("newName"), FNexusSchema::Str(TEXT("rename_emitter new name")))
		.Prop(TEXT("enabled"), FNexusSchema::Bool(TEXT("set_emitter_enabled")))
		.Prop(TEXT("emitterPath"), FNexusSchema::Str(TEXT("add_emitter: existing NiagaraEmitter path; omit to create empty emitter")))
		.Prop(TEXT("modulePath"), FNexusSchema::Str(TEXT("add_module: NiagaraScript module path")))
		.Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("remove_module / set_module_parameter FunctionCall name")))
		.Prop(TEXT("usage"), FNexusSchema::Enum(TEXT("add_module / set_module_parameter script slot"),
			{ TEXT("Spawn"), TEXT("Update"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") }))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New value string")))
		.Required({ TEXT("action") })
		.Build();
#endif
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("NiagaraSystem asset path")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("Batch ops (at least one)"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle"), TEXT("fx"), TEXT("parameter") };
	Out.RelatedCapabilities = { TEXT("get_asset_niagara_system"), TEXT("create_asset_niagara_system"), TEXT("search_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("Edit emitters/module stack/RI inputs; get_asset_niagara_system to read");
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
		OutError = TEXT("Invalid System or parameterName");
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
		const FName TypeName = TypeDef.GetName();
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
		Entry->SetStringField(TEXT("error"), TEXT("emitterName required"));
		return nullptr;
	}
	const int32 FoundIdx = FindEmitterIndex(System, EmitterName);
	if (FoundIdx == INDEX_NONE)
	{
		Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
		return nullptr;
	}
	return &System->GetEmitterHandle(FoundIdx);
}

static void HandleNiagara_SetProperty(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString PropPath = A.Str(TEXT("propertyPath"));
	const FString Value = A.Str(TEXT("value"));
	if (PropPath.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
		return;
	}
	FString OldVal, ActualVal, Err;
	if (!FNexusPropertyUtils::WritePropertyAndEcho(System, { PropPath }, 0, Value, OldVal, ActualVal, Err))
	{
		Ctx.Entry->SetStringField(TEXT("error"), Err);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("propertyPath"), PropPath);
	if (!OldVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("oldValue"), OldVal);
	if (!ActualVal.IsEmpty()) Ctx.Entry->SetStringField(TEXT("newValue"), ActualVal);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
static void HandleNiagara_SetUserParameter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString ParamName = A.Str(TEXT("parameterName"));
	const FString Value = A.Str(TEXT("value"));
	if (ParamName.IsEmpty() || Value.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_user_parameter requires parameterName and value"));
		return;
	}
	FString ParamErr;
	if (!SetNiagaraUserParameter(System, ParamName, Value, ParamErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), ParamErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("parameterName"), ParamName);
	Ctx.Entry->SetStringField(TEXT("newValue"), Value);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}
#endif

static void HandleNiagara_SetEmitterEnabled(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FString EmitterName = FNexusArgs(Op).Str(TEXT("emitterName"));
	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, Ctx.Entry);
	if (!Handle) return;
	const bool bEnabled = FNexusArgs(Op).Bool(TEXT("enabled"), true);
	Handle->SetIsEnabled(bEnabled, *System, true);
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetBoolField(TEXT("enabled"), bEnabled);
	Ctx.Entry->SetStringField(TEXT("emitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleNiagara_RenameEmitter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("emitterName"));
	const FString NewName = A.Str(TEXT("newName"));
	FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName, Ctx.Entry);
	if (!Handle) return;
	if (NewName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("rename_emitter requires newName"));
		return;
	}
	Handle->SetName(FName(*NewName), *System);
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("newName"), NewName);
	Ctx.Entry->SetStringField(TEXT("emitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleNiagara_RemoveEmitter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FString EmitterName = FNexusArgs(Op).Str(TEXT("emitterName"));
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
	Ctx.Entry->SetStringField(TEXT("removed"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("emitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleNiagara_AddEmitter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterPath = A.Str(TEXT("emitterPath"));
	const FString EmitterName = A.Str(TEXT("emitterName"));
	UNiagaraEmitter* Emitter = nullptr;
	if (!EmitterPath.IsEmpty())
	{
		Emitter = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraEmitter>(EmitterPath);
		if (!Emitter)
		{
			Ctx.Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
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
			Ctx.Entry->SetStringField(TEXT("error"), CreateErr);
			return;
		}
#else
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_emitter without emitterPath is editor-only"));
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
	Ctx.Entry->SetStringField(TEXT("emitterName"), AddName.ToString());
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
}

static void HandleNiagara_AddModule(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("emitterName"));
	const FString ModulePath = A.Str(TEXT("modulePath"));
	const FString Usage = A.Str(TEXT("usage"));
	if (EmitterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("emitterName required"));
		return;
	}
	if (ModulePath.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_module requires modulePath"));
		return;
	}
	FString AddedName, ModErr;
	if (!FNexusNiagaraGraphUtils::AddModule(System, EmitterName, ModulePath, Usage, AddedName, ModErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), ModErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("moduleName"), AddedName);
	Ctx.Entry->SetStringField(TEXT("usage"), Usage.IsEmpty() ? TEXT("Update") : Usage);
	Ctx.Entry->SetStringField(TEXT("emitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_module/remove_module editor only"));
#endif
}

static void HandleNiagara_RemoveModule(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("emitterName"));
	const FString ModuleName = A.Str(TEXT("moduleName"));
	if (EmitterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("emitterName required"));
		return;
	}
	if (ModuleName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("remove_module requires moduleName"));
		return;
	}
	FString ModErr;
	if (!FNexusNiagaraGraphUtils::RemoveModule(System, EmitterName, ModuleName, ModErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), ModErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("removed"), ModuleName);
	Ctx.Entry->SetStringField(TEXT("emitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("add_module/remove_module editor only"));
#endif
}

static void HandleNiagara_SetModuleParameter(const TSharedPtr<FJsonObject>& Op, FNexusActionContext& Ctx)
{
#if WITH_EDITOR
	UNiagaraSystem* System = NiagaraFrom(Ctx);
	const FNexusArgs A(Op);
	const FString EmitterName = A.Str(TEXT("emitterName"));
	const FString ModuleName = A.Str(TEXT("moduleName"));
	const FString ParameterName = A.Str(TEXT("parameterName"));
	const FString Value = A.Str(TEXT("value"));
	const FString Usage = A.Str(TEXT("usage"));
	if (EmitterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("emitterName required"));
		return;
	}
	if (ModuleName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_module_parameter requires moduleName"));
		return;
	}
	if (ParameterName.IsEmpty())
	{
		Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_module_parameter requires parameterName"));
		return;
	}
	FString ModErr;
	if (!FNexusNiagaraGraphUtils::SetModuleParameter(System, EmitterName, ModuleName, ParameterName, Usage, Value, ModErr))
	{
		Ctx.Entry->SetStringField(TEXT("error"), ModErr);
		return;
	}
	MarkNiagaraDirty(Ctx);
	Ctx.Entry->SetStringField(TEXT("emitterName"), EmitterName);
	Ctx.Entry->SetStringField(TEXT("moduleName"), ModuleName);
	Ctx.Entry->SetStringField(TEXT("parameterName"), ParameterName);
	Ctx.Entry->SetStringField(TEXT("value"), Value);
	if (!Usage.IsEmpty()) Ctx.Entry->SetStringField(TEXT("usage"), Usage);
	Ctx.Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
	Ctx.Entry->SetStringField(TEXT("error"), TEXT("set_module_parameter editor only"));
#endif
}

bool FManageAssetNiagaraSystemCapability::PrepareTarget(
	const TSharedPtr<FJsonObject>& Args,
	TSharedPtr<FJsonObject>& Entry,
	void*& OutTarget,
	FString& OutError) const
{
	const FString AssetPath = FNexusArgs(Args).Str(TEXT("assetPath"));
	Entry->SetStringField(TEXT("path"), AssetPath);
	UNiagaraSystem* System = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraSystem>(AssetPath);
	if (!System)
	{
		OutError = FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath);
		return false;
	}
	OutTarget = System;
	return true;
}

void FManageAssetNiagaraSystemCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
	OutHandlers.Add(TEXT("set_property"),        &HandleNiagara_SetProperty);
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
	OutHandlers.Add(TEXT("set_user_parameter"),  &HandleNiagara_SetUserParameter);
#endif
	OutHandlers.Add(TEXT("set_emitter_enabled"), &HandleNiagara_SetEmitterEnabled);
	OutHandlers.Add(TEXT("rename_emitter"),      &HandleNiagara_RenameEmitter);
	OutHandlers.Add(TEXT("remove_emitter"),      &HandleNiagara_RemoveEmitter);
	OutHandlers.Add(TEXT("add_emitter"),         &HandleNiagara_AddEmitter);
	OutHandlers.Add(TEXT("add_module"),          &HandleNiagara_AddModule);
	OutHandlers.Add(TEXT("remove_module"),       &HandleNiagara_RemoveModule);
	OutHandlers.Add(TEXT("set_module_parameter"), &HandleNiagara_SetModuleParameter);
}

REGISTER_MCP_CAPABILITY(FManageAssetNiagaraSystemCapability)

#endif // WITH_NIAGARA
