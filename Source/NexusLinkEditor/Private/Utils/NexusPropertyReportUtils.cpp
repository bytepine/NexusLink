// Copyright byteyang. All Rights Reserved.

#include "Utils/NexusPropertyReportUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusJsonUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"

// GetCPPType() 是虚函数，会解引用属性所引用的类型（PropertyClass/Struct/Inner...）。
// 病态资产（引用已删除/未加载类型）可能使这些引用为空，直接调用会解空指针崩溃。
// 递归校验类型图可解析后再调用真实 GetCPPType，不可解析时回退为属性类名。
static bool CanResolveCPPType(const FProperty* Prop)
{
	if (!Prop) return false;

	if (const FStructProperty* SP = CastField<FStructProperty>(Prop))
		return SP->Struct != nullptr;
	if (const FInterfaceProperty* IP = CastField<FInterfaceProperty>(Prop))
		return IP->InterfaceClass != nullptr;
	if (const FClassProperty* CP = CastField<FClassProperty>(Prop))
		return CP->MetaClass != nullptr && CP->PropertyClass != nullptr;
	if (const FSoftClassProperty* SCP = CastField<FSoftClassProperty>(Prop))
		return SCP->MetaClass != nullptr;
	if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
		return OP->PropertyClass != nullptr;
	if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		return EP->GetEnum() != nullptr && CanResolveCPPType(EP->GetUnderlyingProperty());
	if (const FArrayProperty* AP = CastField<FArrayProperty>(Prop))
		return CanResolveCPPType(AP->Inner);
	if (const FSetProperty* SetP = CastField<FSetProperty>(Prop))
		return CanResolveCPPType(SetP->ElementProp);
	if (const FMapProperty* MP = CastField<FMapProperty>(Prop))
		return CanResolveCPPType(MP->KeyProp) && CanResolveCPPType(MP->ValueProp);
	if (const FDelegateProperty* DP = CastField<FDelegateProperty>(Prop))
		return DP->SignatureFunction != nullptr;
	if (const FMulticastDelegateProperty* MDP = CastField<FMulticastDelegateProperty>(Prop))
		return MDP->SignatureFunction != nullptr;

	return true;
}

static FString SafeGetCPPType(const FProperty* Prop)
{
	return CanResolveCPPType(Prop) ? Prop->GetCPPType() : Prop->GetClass()->GetName();
}

TArray<TSharedPtr<FJsonValue>> FNexusPropertyReportUtils::BuildEditablePropsPage(
	UClass*                      Class,
	void*                        Instance,
	UClass*                      LeafClass,
	const FString&               NameFilter,
	const TArray<FString>&       PropertyPaths,
	int32                        Offset,
	int32                        Limit,
	int32&                       OutTotal,
	int32                        SubobjectDepth,
	int32                        SubobjectMaxCount)
{
	// 先收集满足过滤条件的全部属性
	TArray<TSharedPtr<FJsonObject>> All;
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
		const FString PN = Prop->GetName();
		if (!NameFilter.IsEmpty() && !FNexusStringMatchUtils::Matches(PN, NameFilter)) continue;
		if (!FNexusAssetUtils::MatchesPropertyPathsFilter(PropertyPaths, PN)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), PN);
		Entry->SetStringField(TEXT("type"), SafeGetCPPType(Prop));

		void* ValuePtr = Instance ? Prop->ContainerPtrToValuePtr<void>(Instance) : nullptr;

		if (ValuePtr)
		{
			FString Val;
			FNexusPropertyUtils::ExportText(Prop, Val, ValuePtr);
			if (!Val.IsEmpty()) Entry->SetStringField(TEXT("value"), Val);

			// opt-in：递归展开 instanced/EditInline 子对象属性
			if (SubobjectDepth > 0)
			{
				if (const FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
				{
					const bool bInstanced = Prop->HasAnyPropertyFlags(CPF_InstancedReference);
					const bool bEditInline = ObjProp->PropertyClass
						&& ObjProp->PropertyClass->HasAnyClassFlags(CLASS_EditInlineNew);
					if (bInstanced || bEditInline)
					{
						UObject* SubObj = ObjProp->GetObjectPropertyValue(ValuePtr);
						if (SubObj)
						{
							int32 SubTotal = 0;
							TArray<TSharedPtr<FJsonValue>> SubProps = BuildEditablePropsPage(
								SubObj->GetClass(),
								SubObj,
								SubObj->GetClass(),
								TEXT(""),
								TArray<FString>(),
								0,
								SubobjectMaxCount,
								SubTotal,
								SubobjectDepth - 1,
								SubobjectMaxCount);
							if (SubProps.Num() > 0)
							{
								Entry->SetArrayField(TEXT("subProperties"), SubProps);
							}
						}
					}
				}
			}
		}

		// 继承标记始终写出（false 也写），否则稀疏字段无法进入响应压缩 defaults
		if (LeafClass)
		{
			bool bInherited = false;
			if (UClass* OwnerCls = Prop->GetOwnerClass())
			{
				bInherited = (OwnerCls != LeafClass);
			}
			Entry->SetBoolField(TEXT("inherited"), bInherited);
		}

		All.Add(Entry);
	}

	OutTotal = All.Num();

	// 分页切片
	int32 Start, End;
	FNexusJsonUtils::ComputeSlice(OutTotal, Offset, Limit, Start, End);

	TArray<TSharedPtr<FJsonValue>> Page;
	for (int32 i = Start; i < End; ++i)
		Page.Add(MakeShared<FJsonValueObject>(All[i]));
	return Page;
}
