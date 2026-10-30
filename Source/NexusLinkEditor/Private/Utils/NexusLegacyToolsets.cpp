// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusLegacyToolsets.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusPropertyUtils.h"
#include "JsonObjectConverter.h"
#include "Misc/Paths.h"

bool FNexusLegacyToolsets::Handles(const FString& Domain)
{
    return Domain == TEXT("PCGToolset") || Domain == TEXT("NiagaraToolsets") || Domain.StartsWith(TEXT("animation_toolset.toolsets.")) || Domain.StartsWith(TEXT("sequencer_animmixer_toolset.")) || Domain==TEXT("NexusSequenceFolders");
}

UObject* FNexusLegacyToolsets::Resolve(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key)
{
    FString Path;
    if (!Args.IsValid()) return nullptr;
    const TSharedPtr<FJsonObject>* Ref = nullptr;
    if (Args->TryGetObjectField(Key, Ref)) (*Ref)->TryGetStringField(TEXT("refPath"), Path);
    else Args->TryGetStringField(Key, Path);
    if (Path.IsEmpty()) return nullptr;
    if (!Path.Contains(TEXT(".")) && !Path.StartsWith(TEXT("/Script/"))) Path += TEXT(".") + FPaths::GetBaseFilename(Path);
    if (UObject* Loaded=StaticFindObject(UObject::StaticClass(),nullptr,*Path)) return Loaded;
    if (Path.StartsWith(TEXT("/Script/"))) return LoadObject<UObject>(nullptr,*Path);
    return FNexusAssetUtils::LoadAssetWithFallback<UObject>(Path);
}

bool FNexusLegacyToolsets::SetProperties(UObject* Target, const TSharedPtr<FJsonObject>& Values, FString& Error)
{
    if (!Target || !Values.IsValid() || Values->Values.Num() == 0) { Error = TEXT("Target and nonempty property data are required"); return false; }
    // 在副本上完整转换和验证，再修改对象，避免非法输入留下半次写入。
    UObject* Copy = DuplicateObject(Target, GetTransientPackage());
    if (!Copy) { Error = TEXT("Could not validate property data"); return false; }
    TArray<FProperty*> Properties;
    for (const auto& Pair : Values->Values) {
        FProperty* Property = FindFProperty<FProperty>(Target->GetClass(), *Pair.Key);
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient)
            || !FJsonObjectConverter::JsonValueToUProperty(Pair.Value, Property, Property->ContainerPtrToValuePtr<void>(Copy), 0, 0)) {
            Error = TEXT("Invalid or noneditable property: ") + FString(*Pair.Key); return false;
        }
        Properties.Add(Property);
    }
    Target->Modify();
    for (FProperty* Property : Properties) Property->CopyCompleteValue_InContainer(Target, Copy);
    Target->PostEditChange(); Target->MarkPackageDirty();
    return true;
}

void FNexusLegacyToolsets::Call(const FString& Domain, const FString& Tool, const TSharedPtr<FJsonObject>& Args,
    const TSharedPtr<FNexusEditorOperation>& Operation)
{
    Operation->Backend = TEXT("EngineCompatibility");
    auto Input = MakeShared<FJsonObject>();
    for (const auto& Pair : Args->Values) { FString Key(*Pair.Key); if (!Key.IsEmpty()) Key[0] = FChar::ToLower(Key[0]); Input->SetField(Key, Pair.Value); }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>(); FString Error;
    const bool bOk = Domain == TEXT("PCGToolset") ? PCG(Tool, Input, Data, Error)
        : Domain == TEXT("NiagaraToolsets") ? Niagara(Tool, Input, Data, Error) : Animation(Domain, Tool, Input, Data, Error);
    if (!bOk) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, Error.IsEmpty() ? TEXT("Action is not supported by this engine compatibility backend") : Error); return; }
    auto Result = MakeShared<FJsonObject>(); Result->SetObjectField(TEXT("CompatibilityResult"), Data);
    FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result);
}
