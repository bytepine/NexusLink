// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusManageProjectSettingsCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusEditorToolsets.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "ISettingsModule.h"
#include "ISettingsContainer.h"
#include "ISettingsCategory.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"

struct FNexusSettingsEditTarget final
{
    TSharedPtr<ISettingsSection> Section;
    UObject* Object=nullptr;
    TSharedPtr<FJsonObject> NativeSelector;
};

static void NexusEditSettings(const TSharedPtr<FJsonObject>& Operation,FNexusActionContext& Context)
{
    auto* Target=static_cast<FNexusSettingsEditTarget*>(Context.Target); const auto Fail=[&](const FString& Error){Context.Entry->SetStringField(TEXT("Error"),Error);};
    if (!Target || !Target->Object || !Target->Section.IsValid()) { Fail(TEXT("Settings target is unavailable")); return; }
    auto Input=MakeShared<FJsonObject>(); Input->Values=Target->NativeSelector->Values; TArray<FProperty*> Properties; UObject* Candidate=nullptr; bool bRestart=false;
    FString Tool;
    if (Context.Action==TEXT("SetProperties")) {
        const TSharedPtr<FJsonObject>* Values=nullptr;
        if (!Target->Section->CanEdit() || !Target->Section->CanSave() || !Operation->TryGetObjectField(TEXT("Properties"),Values) || (*Values)->Values.Num()==0 || (*Values)->Values.Num()>64) { Fail(TEXT("SetProperties requires an editable/saveable section and 1-64 typed Properties")); return; }
        Candidate=DuplicateObject(Target->Object,GetTransientPackage());
        for (const auto& Pair : (*Values)->Values) {
            FProperty* Property=FindFProperty<FProperty>(Target->Object->GetClass(),*Pair.Key);
            if (!Candidate || !Property || !Property->HasAllPropertyFlags(CPF_Edit|CPF_Config) || Property->HasAnyPropertyFlags(CPF_EditConst|CPF_Transient) || !FJsonObjectConverter::JsonValueToUProperty(Pair.Value,Property,Property->ContainerPtrToValuePtr<void>(Candidate),0,0)) { Fail(FString(TEXT("Invalid editable config property: "))+*Pair.Key); return; }
            Properties.Add(Property); bRestart|=Property->HasMetaData(TEXT("ConfigRestartRequired")) || Property->HasMetaData(TEXT("RestartRequired"));
        }
        FString Encoded; FJsonSerializer::Serialize((*Values).ToSharedRef(),TJsonWriterFactory<>::Create(&Encoded)); Input->SetStringField(TEXT("PropertiesJson"),Encoded); Tool=TEXT("SetSectionProperties");
    } else if (Context.Action==TEXT("Save")) { if (!Target->Section->CanSave()) { Fail(TEXT("Section cannot be saved")); return; } Tool=TEXT("SaveSection"); }
    else { if (!Target->Section->CanResetDefaults()) { Fail(TEXT("Section does not support resetting defaults")); return; } Tool=TEXT("ResetSectionToDefaults"); bRestart=true; }
    if (!FNexusNativeEditorTools::EditSync(TEXT("ConfigSettingsToolset"),Tool,Input,Context.Entry)) {
        bool bSuccess=false;
        if (Context.Action==TEXT("SetProperties")) {
            Target->Object->Modify();
            for (FProperty* Property : Properties) Target->Object->PreEditChange(Property);
            for (FProperty* Property : Properties) Property->CopyCompleteValue_InContainer(Target->Object,Candidate);
            for (FProperty* Property : Properties) { FPropertyChangedEvent Event(Property,EPropertyChangeType::ValueSet); Target->Object->PostEditChangeProperty(Event); }
            bSuccess=Target->Section->Save();
        } else bSuccess=Context.Action==TEXT("Save")?Target->Section->Save():Target->Section->ResetDefaults();
        if (!bSuccess) { Fail(TEXT("Settings operation failed; in-memory values may already have changed")); return; }
        Context.Entry->SetStringField(TEXT("Backend"),TEXT("EngineCompatibility"));
    }
    if (!Context.Entry->HasField(TEXT("Error"))) { Context.Entry->SetBoolField(TEXT("bSavedToDisk"),true); Context.Entry->SetBoolField(TEXT("bRestartRequired"),bRestart); }
}

void FNexusManageProjectSettingsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name=TEXT("ManageProjectSettings"); Out.Description=TEXT("Set typed registered settings, save or reset a section; reports persistence and restart requirements.");
    auto Operation=FNexusSchema::Object().Required(TEXT("Action"),FNexusSchema::Enum(TEXT("Settings operation"),{TEXT("SetProperties"),TEXT("Save"),TEXT("Reset")})).Prop(TEXT("Properties"),FNexusSchema::AnyObject(TEXT("Exact editable config property names and typed JSON values"))).Build();
    Out.InputSchema=FNexusSchema::Object().Prop(TEXT("Container"),FNexusSchema::Str(TEXT("Settings container"),TEXT("Project"))).Required(TEXT("Category"),FNexusSchema::Str(TEXT("Registered settings category"))).Required(TEXT("Section"),FNexusSchema::Str(TEXT("Registered settings section"))).Required(TEXT("Operations"),FNexusSchema::ArrayOf(TEXT("Settings operations; SetProperties persists values"),Operation.ToSharedRef())).Build();
    Out.Tags={FNexusMcpTags::Write,FNexusMcpTags::Editor,FNexusMcpTags::Dangerous}; Out.RelatedCapabilities={TEXT("GetProjectSettings")};
}
void FNexusManageProjectSettingsCapability::RegisterActions(TMap<FString,FNexusActionHandler>& Handlers) const { for (const FString& Action : {TEXT("SetProperties"),TEXT("Save"),TEXT("Reset")}) Handlers.Add(Action,&NexusEditSettings); }
bool FNexusManageProjectSettingsCapability::PrepareTarget(const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Entry,void*& Target,FString& Error) const
{
    const FNexusArgs Arguments(Args); auto Container=FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings")).GetContainer(FName(*Arguments.Str(TEXT("Container"),TEXT("Project"))));
    auto Category=Container.IsValid()?Container->GetCategory(FName(*Arguments.Str(TEXT("Category")))):nullptr; auto Section=Category.IsValid()?Category->GetSection(FName(*Arguments.Str(TEXT("Section")))):nullptr;
    UObject* Object=Section.IsValid()?Section->GetSettingsObject().Get():nullptr;
    if (!Object) { Error=TEXT("Discover a registered UObject settings section with GetProjectSettings first"); return false; }
    auto* State=new FNexusSettingsEditTarget(); State->Object=Object; State->Section=Section; State->NativeSelector=MakeShared<FJsonObject>();
    State->NativeSelector->SetStringField(TEXT("ContainerName"),Arguments.Str(TEXT("Container"),TEXT("Project"))); State->NativeSelector->SetStringField(TEXT("CategoryName"),Arguments.Str(TEXT("Category"))); State->NativeSelector->SetStringField(TEXT("SectionName"),Arguments.Str(TEXT("Section"))); Target=State; Entry->SetStringField(TEXT("SettingsClass"),Object->GetClass()->GetPathName()); return true;
}
void FNexusManageProjectSettingsCapability::FinalizeTarget(void* Target) const { delete static_cast<FNexusSettingsEditTarget*>(Target); }
REGISTER_MCP_CAPABILITY(FNexusManageProjectSettingsCapability)
