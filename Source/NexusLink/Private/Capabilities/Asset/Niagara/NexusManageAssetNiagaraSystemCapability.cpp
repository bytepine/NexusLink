// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Asset/Niagara/NexusManageAssetNiagaraSystemCapability.h"

#if WITH_NIAGARA

#include "Utils/NexusCapabilityResultBuilder.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusJsonUtils.h"
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
	Out.Description = TEXT("Batch edit Niagara. set_property/set_user_parameter/Emitter CRUD; add_module/remove_module (editor).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_property"), TEXT("set_user_parameter"),
			  TEXT("set_emitter_enabled"), TEXT("rename_emitter"),
			  TEXT("add_emitter"), TEXT("remove_emitter"),
			  TEXT("add_module"), TEXT("remove_module") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("parameterName"), FNexusSchema::Str(TEXT("User parameter name (set_user_parameter)")))
		.Prop(TEXT("emitterName"), FNexusSchema::Str(TEXT("Emitter name")))
		.Prop(TEXT("newName"), FNexusSchema::Str(TEXT("rename_emitter new name")))
		.Prop(TEXT("enabled"), FNexusSchema::Bool(TEXT("set_emitter_enabled")))
		.Prop(TEXT("emitterPath"), FNexusSchema::Str(TEXT("add_emitter: existing NiagaraEmitter path; omit to create empty emitter")))
		.Prop(TEXT("modulePath"), FNexusSchema::Str(TEXT("add_module: NiagaraScript module path")))
		.Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("remove_module: module display name")))
		.Prop(TEXT("usage"), FNexusSchema::Enum(TEXT("add_module script slot"),
			{ TEXT("Spawn"), TEXT("Update"), TEXT("EmitterSpawn"), TEXT("EmitterUpdate") }))
		.Prop(TEXT("value"), FNexusSchema::Str(TEXT("New value formats; other structs via ImportText")))
		.Required({ TEXT("action") })
		.Build();
#else
	Out.Description = TEXT("Batch edit Niagara. set_property/Emitter CRUD; add_module/remove_module (editor).");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"), FNexusSchema::Enum(TEXT("Action"),
			{ TEXT("set_property"), TEXT("set_emitter_enabled"), TEXT("rename_emitter"),
			  TEXT("add_emitter"), TEXT("remove_emitter"),
			  TEXT("add_module"), TEXT("remove_module") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("Property path (set_property)")))
		.Prop(TEXT("emitterName"), FNexusSchema::Str(TEXT("Emitter name")))
		.Prop(TEXT("newName"), FNexusSchema::Str(TEXT("rename_emitter new name")))
		.Prop(TEXT("enabled"), FNexusSchema::Bool(TEXT("set_emitter_enabled")))
		.Prop(TEXT("emitterPath"), FNexusSchema::Str(TEXT("add_emitter: existing NiagaraEmitter path; omit to create empty emitter")))
		.Prop(TEXT("modulePath"), FNexusSchema::Str(TEXT("add_module: NiagaraScript module path")))
		.Prop(TEXT("moduleName"), FNexusSchema::Str(TEXT("remove_module: module display name")))
		.Prop(TEXT("usage"), FNexusSchema::Enum(TEXT("add_module script slot"),
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
	Out.WhenToUse = TEXT("Edit system props/user params/emitter CRUD; module stack via add_module/remove_module (editor)");
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

FCapabilityResult FManageAssetNiagaraSystemCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	return FNexusCapabilityResultBuilder::Build([&](auto& OutEntries, auto& OutTop, auto& OutError)
	{
		FString AssetPath;
		if (!FNexusCapability::RequireString(Arguments, TEXT("assetPath"), AssetPath, OutEntries, {})) return;

		UNiagaraSystem* System = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraSystem>(AssetPath);
		if (!System)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}},
				FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("Missing or empty operations"));
			return;
		}

		for (const TSharedPtr<FJsonValue>& OpVal : Ops)
		{
		const TSharedPtr<FJsonObject>* OpObjPtr = nullptr;
		if (!OpVal.IsValid() || !OpVal->TryGetObject(OpObjPtr) || !OpObjPtr) continue;
		const TSharedPtr<FJsonObject>& OpArgs = *OpObjPtr;

		FString Action, PropPath, ParamName, Value;
		OpArgs->TryGetStringField(TEXT("action"), Action);
		OpArgs->TryGetStringField(TEXT("propertyPath"), PropPath);
		OpArgs->TryGetStringField(TEXT("parameterName"), ParamName);
		OpArgs->TryGetStringField(TEXT("value"), Value);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("path"), AssetPath);
		Entry->SetStringField(TEXT("action"), Action);

		if (Action.Equals(TEXT("set_property"), ESearchCase::IgnoreCase))
		{
			if (PropPath.IsEmpty() || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_property requires propertyPath and value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString OldVal, ActualVal, Err;
			if (!FNexusPropertyUtils::WritePropertyAndEcho(System, { PropPath }, 0, Value, OldVal, ActualVal, Err))
			{
				Entry->SetStringField(TEXT("error"), Err);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			System->MarkPackageDirty();
			Entry->SetStringField(TEXT("propertyPath"), PropPath);
			if (!OldVal.IsEmpty()) Entry->SetStringField(TEXT("oldValue"), OldVal);
			if (!ActualVal.IsEmpty()) Entry->SetStringField(TEXT("newValue"), ActualVal);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
		else if (Action.Equals(TEXT("set_user_parameter"), ESearchCase::IgnoreCase))
		{
			if (ParamName.IsEmpty() || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_user_parameter requires parameterName and value"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString ParamErr;
			if (!SetNiagaraUserParameter(System, ParamName, Value, ParamErr))
			{
				Entry->SetStringField(TEXT("error"), ParamErr);
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			System->MarkPackageDirty();
			Entry->SetStringField(TEXT("parameterName"), ParamName);
			Entry->SetStringField(TEXT("newValue"), Value);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
#endif
		else if (Action.Equals(TEXT("set_emitter_enabled"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("rename_emitter"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("remove_emitter"), ESearchCase::IgnoreCase))
		{
			FString EmitterName;
			OpArgs->TryGetStringField(TEXT("emitterName"), EmitterName);
			if (EmitterName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("emitterName required"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			int32 FoundIdx = INDEX_NONE;
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
					FoundIdx = i;
					break;
				}
			}
			if (FoundIdx == INDEX_NONE)
			{
				Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Emitter not found: %s"), *EmitterName));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(FoundIdx);
			if (Action.Equals(TEXT("set_emitter_enabled"), ESearchCase::IgnoreCase))
			{
				bool bEnabled = true;
				if (OpArgs->HasField(TEXT("enabled"))) OpArgs->TryGetBoolField(TEXT("enabled"), bEnabled);
				Handle.SetIsEnabled(bEnabled, *System, true);
				Entry->SetBoolField(TEXT("enabled"), bEnabled);
			}
			else if (Action.Equals(TEXT("rename_emitter"), ESearchCase::IgnoreCase))
			{
				FString NewName;
				OpArgs->TryGetStringField(TEXT("newName"), NewName);
				if (NewName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("rename_emitter requires newName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Handle.SetName(FName(*NewName), *System);
				Entry->SetStringField(TEXT("newName"), NewName);
			}
			else
			{
#if NX_UE_HAS_NIAGARA_REMOVE_EMITTER_BY_ID
				TSet<FGuid> Ids;
				Ids.Add(Handle.GetId());
				System->RemoveEmitterHandlesById(Ids);
#else
				System->RemoveEmitterHandle(Handle);
#endif
				Entry->SetStringField(TEXT("removed"), EmitterName);
			}
			System->MarkPackageDirty();
			Entry->SetStringField(TEXT("emitterName"), EmitterName);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("add_emitter"), ESearchCase::IgnoreCase))
		{
			FString EmitterPath, EmitterName;
			OpArgs->TryGetStringField(TEXT("emitterPath"), EmitterPath);
			OpArgs->TryGetStringField(TEXT("emitterName"), EmitterName);
			UNiagaraEmitter* Emitter = nullptr;
			if (!EmitterPath.IsEmpty())
			{
				Emitter = FNexusAssetUtils::LoadAssetWithFallback<UNiagaraEmitter>(EmitterPath);
				if (!Emitter)
				{
					Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("NiagaraEmitter not found: %s"), *EmitterPath));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
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
					Entry->SetStringField(TEXT("error"), CreateErr);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
#else
				Entry->SetStringField(TEXT("error"), TEXT("add_emitter without emitterPath is editor-only"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
#endif
			}
			const FName AddName = EmitterName.IsEmpty() ? Emitter->GetFName() : FName(*EmitterName);
#if NX_UE_HAS_NIAGARA_ADD_EMITTER_VERSION
			System->AddEmitterHandle(*Emitter, AddName, FGuid());
#else
			System->AddEmitterHandle(*Emitter, AddName);
#endif
			System->MarkPackageDirty();
			Entry->SetStringField(TEXT("emitterName"), AddName.ToString());
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
		}
		else if (Action.Equals(TEXT("add_module"), ESearchCase::IgnoreCase)
			|| Action.Equals(TEXT("remove_module"), ESearchCase::IgnoreCase))
		{
#if WITH_EDITOR
			FString EmitterName, ModulePath, ModuleName, Usage;
			OpArgs->TryGetStringField(TEXT("emitterName"), EmitterName);
			OpArgs->TryGetStringField(TEXT("modulePath"), ModulePath);
			OpArgs->TryGetStringField(TEXT("moduleName"), ModuleName);
			OpArgs->TryGetStringField(TEXT("usage"), Usage);
			if (EmitterName.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("emitterName required"));
				OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			FString ModErr;
			if (Action.Equals(TEXT("add_module"), ESearchCase::IgnoreCase))
			{
				if (ModulePath.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("add_module requires modulePath"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				FString AddedName;
				if (!FNexusNiagaraGraphUtils::AddModule(System, EmitterName, ModulePath, Usage, AddedName, ModErr))
				{
					Entry->SetStringField(TEXT("error"), ModErr);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Entry->SetStringField(TEXT("moduleName"), AddedName);
				Entry->SetStringField(TEXT("usage"), Usage.IsEmpty() ? TEXT("Update") : Usage);
			}
			else
			{
				if (ModuleName.IsEmpty())
				{
					Entry->SetStringField(TEXT("error"), TEXT("remove_module requires moduleName"));
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				if (!FNexusNiagaraGraphUtils::RemoveModule(System, EmitterName, ModuleName, ModErr))
				{
					Entry->SetStringField(TEXT("error"), ModErr);
					OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
					continue;
				}
				Entry->SetStringField(TEXT("removed"), ModuleName);
			}
			System->MarkPackageDirty();
			Entry->SetStringField(TEXT("emitterName"), EmitterName);
			Entry->SetStringField(TEXT("note"), TEXT("persist with save_asset"));
#else
			Entry->SetStringField(TEXT("error"), TEXT("add_module/remove_module editor only"));
#endif
		}
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetNiagaraSystemCapability)

#endif // WITH_NIAGARA
