// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusManageProjectPluginCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpTool.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusArgs.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "PluginDescriptor.h"
#include "PluginReferenceDescriptor.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

static bool NexusPluginNameValid(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len()>64 || !FChar::IsAlpha(Name[0])) return false;
    for (TCHAR Character : Name) if (!FChar::IsAlnum(Character) && Character!=TEXT('_')) return false;
    return true;
}
static bool NexusPluginDependsOn(const FString& Name,const FString& Target,TSet<FString>& Visited)
{
    if (Name==Target) return true;
    if (Visited.Contains(Name)) return false; Visited.Add(Name);
    auto Plugin=IPluginManager::Get().FindPlugin(Name);
    if (Plugin.IsValid()) for (const auto& Dependency : Plugin->GetDescriptor().Plugins) if (Dependency.bEnabled && NexusPluginDependsOn(Dependency.Name,Target,Visited)) return true;
    return false;
}
static void NexusEditProjectPlugin(const TSharedPtr<FJsonObject>& Operation,FNexusActionContext& Context)
{
    const FNexusArgs Arguments(Operation); const FString Name=FNexusArgs(Context.Args).Str(TEXT("PluginName")); const auto Fail=[&](const FString& Error){Context.Entry->SetStringField(TEXT("Error"),Error);};
    auto Plugin=IPluginManager::Get().FindPlugin(Name); const FString ProjectPlugins=FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
    if (Context.Action==TEXT("Create")) {
        const FString Directory=FPaths::Combine(ProjectPlugins,Name),Filename=FPaths::Combine(Directory,Name+TEXT(".uplugin"));
        if (Plugin.IsValid() || IFileManager::Get().DirectoryExists(*Directory)) { Fail(TEXT("Plugin name or target directory already exists")); return; }

            FPluginDescriptor Descriptor; Descriptor.FriendlyName=Name; Descriptor.Description=Arguments.Str(TEXT("Description")); Descriptor.Version=1; Descriptor.VersionName=TEXT("1.0"); Descriptor.Category=TEXT("Other"); Descriptor.bCanContainContent=true;
            FText Reason;
            if (!IFileManager::Get().MakeDirectory(*Directory,true) || !Descriptor.Save(Filename,Reason)) { Fail(Reason.IsEmpty()?TEXT("Could not create plugin descriptor"):Reason.ToString()); return; }
            IPluginManager::Get().RefreshPluginsList();
            if (!IProjectManager::Get().SetPluginEnabled(Name,true,Reason)) { Fail(Reason.ToString()); return; }
            if (!IProjectManager::Get().SaveCurrentProjectToDisk(Reason)) { Fail(Reason.ToString()); return; }
            IPluginManager::Get().MountNewlyCreatedPlugin(Name); Context.Entry->SetStringField(TEXT("Backend"),TEXT("Independent"));

        if (!IFileManager::Get().FileExists(*Filename)) { Fail(TEXT("Plugin creation did not produce the requested descriptor")); return; }
        Context.Entry->SetStringField(TEXT("DescriptorPath"),Filename); Context.Entry->SetBoolField(TEXT("bSavedToDisk"),true); Context.Entry->SetBoolField(TEXT("bRestartRequired"),true); return;
    }
    if (!Plugin.IsValid()) { Fail(TEXT("Discover PluginName with GetProjectPlugins first")); return; }
    // 描述符只允许写当前工程下的插件，避免修改引擎安装或外部插件。
    if (Context.Action!=TEXT("SetEnabled") && !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()),ProjectPlugins)) { Fail(TEXT("Descriptor changes are limited to project-local plugins")); return; }
    FPluginDescriptor Descriptor=Plugin->GetDescriptor();
    if (Context.Action==TEXT("SetEnabled")) {
        if (!Operation->HasTypedField<EJson::Boolean>(TEXT("bEnabled"))) { Fail(TEXT("SetEnabled requires bEnabled")); return; }
    } else if (Context.Action==TEXT("UpdateDescriptor")) {
        const TSharedPtr<FJsonObject>* Values=nullptr;
        if (!Operation->TryGetObjectField(TEXT("Properties"),Values) || (*Values)->Values.Num()==0) { Fail(TEXT("UpdateDescriptor requires Properties")); return; }
        TMap<FString,FString*> Strings={{TEXT("FriendlyName"),&Descriptor.FriendlyName},{TEXT("Description"),&Descriptor.Description},{TEXT("Category"),&Descriptor.Category},{TEXT("VersionName"),&Descriptor.VersionName},{TEXT("CreatedBy"),&Descriptor.CreatedBy},{TEXT("CreatedByURL"),&Descriptor.CreatedByURL},{TEXT("DocsURL"),&Descriptor.DocsURL},{TEXT("MarketplaceURL"),&Descriptor.MarketplaceURL},{TEXT("SupportURL"),&Descriptor.SupportURL}};
        for (const auto& Pair : (*Values)->Values) { FString* const* Field=Strings.Find(FString(*Pair.Key)); if (!Field || Pair.Value->Type!=EJson::String || Pair.Value->AsString().Len()>8192) { Fail(TEXT("Only documented string descriptor fields can be updated")); return; } **Field=Pair.Value->AsString(); }
    } else {
        const FString Dependency=Arguments.Str(TEXT("DependencyName"));
        if (!NexusPluginNameValid(Dependency) || Dependency==Name || !IPluginManager::Get().FindPlugin(Dependency).IsValid()) { Fail(TEXT("Dependency must be a different discovered plugin")); return; }
        const int32 Existing=Descriptor.Plugins.IndexOfByPredicate([&](const FPluginReferenceDescriptor& Reference){return Reference.Name==Dependency;});
        if (Context.Action==TEXT("AddDependency")) {
            TSet<FString> Visited; if (Arguments.Bool(TEXT("bEnabled"),true) && NexusPluginDependsOn(Dependency,Name,Visited)) { Fail(TEXT("Dependency would create a plugin cycle")); return; }
            FPluginReferenceDescriptor Reference(Dependency,Arguments.Bool(TEXT("bEnabled"),true)); Reference.bOptional=Arguments.Bool(TEXT("bOptional")); if (Existing==INDEX_NONE) Descriptor.Plugins.Add(Reference); else Descriptor.Plugins[Existing]=Reference;
        } else { if (Existing==INDEX_NONE) { Fail(TEXT("Dependency not present")); return; } Descriptor.Plugins.RemoveAt(Existing); }
    }
    {
        FText Reason; const bool bSaved=Context.Action==TEXT("SetEnabled")?IProjectManager::Get().SetPluginEnabled(Name,Arguments.Bool(TEXT("bEnabled")),Reason):Plugin->UpdateDescriptor(Descriptor,Reason);
        if (!bSaved) { Fail(Reason.ToString()); return; } Context.Entry->SetStringField(TEXT("Backend"),TEXT("Independent"));
        if (Context.Action==TEXT("SetEnabled") && !IProjectManager::Get().SaveCurrentProjectToDisk(Reason)) { Fail(Reason.ToString()); return; }
    }
    if (!Context.Entry->HasField(TEXT("Error"))) { IPluginManager::Get().RefreshPluginsList(); Context.Entry->SetBoolField(TEXT("bSavedToDisk"),true); Context.Entry->SetBoolField(TEXT("bRestartRequired"),true); }
}

void FNexusManageProjectPluginCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name=TEXT("ManageProjectPlugin"); Out.Description=TEXT("Create content-only project plugins, edit descriptor metadata/dependencies or set next-start enable state.");
    auto Operation=FNexusSchema::Object().Required(TEXT("Action"),FNexusSchema::Enum(TEXT("Plugin operation"),{TEXT("Create"),TEXT("SetEnabled"),TEXT("UpdateDescriptor"),TEXT("AddDependency"),TEXT("RemoveDependency")})).Prop(TEXT("Description"),FNexusSchema::Str(TEXT("Content-only plugin description"))).Prop(TEXT("bEnabled"),FNexusSchema::Bool(TEXT("Requested enabled state"))).Prop(TEXT("bOptional"),FNexusSchema::Bool(TEXT("Optional dependency"))).Prop(TEXT("DependencyName"),FNexusSchema::Str(TEXT("Different discovered plugin"))).Prop(TEXT("Properties"),FNexusSchema::AnyObject(TEXT("String descriptor fields: FriendlyName, Description, Category, VersionName, CreatedBy, CreatedByURL, DocsURL, MarketplaceURL, SupportURL"))).Build();
    Out.InputSchema=FNexusSchema::Object().Required(TEXT("PluginName"),FNexusSchema::Str(TEXT("Plugin identifier; creation only under project Plugins"))).Required(TEXT("Operations"),FNexusSchema::ArrayOf(TEXT("Plugin operations; persistent and restart-aware"),Operation.ToSharedRef())).Build();
    Out.Tags={FNexusMcpTags::Write,FNexusMcpTags::Editor}; Out.RelatedCapabilities={TEXT("GetProjectPlugins")};
}
void FNexusManageProjectPluginCapability::RegisterActions(TMap<FString,FNexusActionHandler>& Handlers) const { for (const FString& Action : {TEXT("Create"),TEXT("SetEnabled"),TEXT("UpdateDescriptor"),TEXT("AddDependency"),TEXT("RemoveDependency")}) Handlers.Add(Action,&NexusEditProjectPlugin); }
bool FNexusManageProjectPluginCapability::PrepareTarget(const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Entry,void*& Target,FString& Error) const { const FString Name=FNexusArgs(Args).Str(TEXT("PluginName")); if (!NexusPluginNameValid(Name)) { Error=TEXT("PluginName must be a valid plugin identifier"); return false; } Entry->SetStringField(TEXT("PluginName"),Name); Target=nullptr; return true; }
REGISTER_MCP_CAPABILITY(FNexusManageProjectPluginCapability)
