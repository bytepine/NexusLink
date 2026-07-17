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
#include "NexusMcpTool.h"

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
#include "NiagaraParameterStore.h"
#include "NiagaraTypes.h"
#endif

void FManageAssetNiagaraSystemCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("manage_asset_niagara_system");
	Out.SearchAssetTypes = {TEXT("NiagaraSystem")};
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
	Out.Description = TEXT("批量编辑 Niagara 系统。operations[].action=set_property/set_user_parameter；无 Emitter 图。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),         FNexusSchema::Enum(TEXT("操作"),
			{ TEXT("set_property"), TEXT("set_user_parameter") }))
		.Prop(TEXT("propertyPath"),   FNexusSchema::Str(TEXT("属性路径（set_property）")))
		.Prop(TEXT("parameterName"),  FNexusSchema::Str(TEXT("用户参数名（set_user_parameter）")))
		.Prop(TEXT("value"),          FNexusSchema::Str(TEXT("新值字符串")))
		.Required({ TEXT("action") })
		.Build();
#else
	Out.Description = TEXT("批量编辑 Niagara 系统属性。operations[].action=set_property；无 Emitter 图。");
	TSharedPtr<FJsonObject> OpSchema = FNexusSchema::Object()
		.Prop(TEXT("action"),       FNexusSchema::Enum(TEXT("操作"), { TEXT("set_property") }))
		.Prop(TEXT("propertyPath"), FNexusSchema::Str(TEXT("属性路径")))
		.Prop(TEXT("value"),        FNexusSchema::Str(TEXT("属性新值字符串")))
		.Required({ TEXT("action") })
		.Build();
#endif
	Out.InputSchema = FNexusSchema::Object()
		.Prop(TEXT("assetPath"),  FNexusSchema::Str(TEXT("NiagaraSystem 资产路径")))
		.Prop(TEXT("operations"), FNexusSchema::ArrayOf(TEXT("批量操作（至少一项）"), OpSchema.ToSharedRef()))
		.Required({ TEXT("assetPath"), TEXT("operations") })
		.Build();
	Out.Tags = { FNexusMcpTags::Write, FNexusMcpTags::Editor };
	Out.ExtraSearchKeywords = { TEXT("niagara"), TEXT("vfx"), TEXT("particle"), TEXT("fx"), TEXT("parameter") };
	Out.RelatedCapabilities = { TEXT("get_asset_niagara_system"), TEXT("search_asset") };
	Out.Prerequisites = { TEXT("editor_only") };
	Out.WhenToUse = TEXT("改系统属性/用户参数；无 Emitter 图编辑");
}

#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
static bool SetNiagaraUserParameter(UNiagaraSystem* System, const FString& ParamName, const FString& Value, FString& OutError)
{
	if (!System || ParamName.IsEmpty())
	{
		OutError = TEXT("System 或 parameterName 无效");
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
		else if (TypeName == FName(TEXT("int32")) || TypeName == FName(TEXT("Int32")))
		{
			const int32 IVal = FCString::Atoi(*Value);
			FMemory::Memcpy(Data.GetData(), &IVal, sizeof(int32));
		}
		else if (TypeName == FName(TEXT("bool")) || TypeName == FName(TEXT("Bool")))
		{
			const bool BVal = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
			FMemory::Memcpy(Data.GetData(), &BVal, sizeof(bool));
		}
		else if (TypeName == FName(TEXT("Vector")) || TypeName == FName(TEXT("Vector3")))
		{
			FVector V(0.f);
			TArray<FString> Parts;
			Value.ParseIntoArray(Parts, TEXT(","), true);
			if (Parts.Num() != 3)
			{
				Value.ParseIntoArrayWS(Parts);
			}
			if (Parts.Num() != 3)
			{
				OutError = TEXT("Vector 参数值格式应为 x,y,z");
				return false;
			}
			V.X = FCString::Atof(*Parts[0]);
			V.Y = FCString::Atof(*Parts[1]);
			V.Z = FCString::Atof(*Parts[2]);
			FMemory::Memcpy(Data.GetData(), &V, sizeof(FVector));
		}
		else
		{
			OutError = FString::Printf(TEXT("暂不支持的用户参数类型: %s"), *TypeName.ToString());
			return false;
		}

		FNiagaraVariable VarToSet(Var);
		Store.SetParameterData(Data.GetData(), VarToSet, false);
		return true;
	}
	OutError = FString::Printf(TEXT("用户参数未找到: %s"), *ParamName);
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
				FString::Printf(TEXT("NiagaraSystem 未找到: %s"), *AssetPath));
			return;
		}

		const TArray<TSharedPtr<FJsonValue>> Ops = FNexusJsonUtils::ExtractOperations(Arguments);
		if (Ops.Num() == 0)
		{
			FNexusCapability::EmitError(OutEntries, {{TEXT("path"), AssetPath}}, TEXT("缺少 operations 或为空"));
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
				Entry->SetStringField(TEXT("error"), TEXT("set_property 需要 propertyPath 和 value"));
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
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
#if NX_UE_HAS_NIAGARA_EXPOSED_PARAMETERS
		else if (Action.Equals(TEXT("set_user_parameter"), ESearchCase::IgnoreCase))
		{
			if (ParamName.IsEmpty() || Value.IsEmpty())
			{
				Entry->SetStringField(TEXT("error"), TEXT("set_user_parameter 需要 parameterName 和 value"));
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
			Entry->SetStringField(TEXT("note"), TEXT("用 save_asset 落盘"));
		}
#endif
		else
		{
			Entry->SetStringField(TEXT("error"), FString::Printf(TEXT("未知 action: %s"), *Action));
		}

		OutEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});
}

REGISTER_MCP_CAPABILITY(FManageAssetNiagaraSystemCapability)

#endif // WITH_NIAGARA
