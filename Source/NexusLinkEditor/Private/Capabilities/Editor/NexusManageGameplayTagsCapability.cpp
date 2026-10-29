// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusManageGameplayTagsCapability.h"
#if WITH_NEXUS_TAGS_EDITOR
#include "NexusCapabilityRegistry.h"
#include "Utils/NexusEditorToolsets.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusGameplayTagReferencerUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
void FNexusManageGameplayTagsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("ManageGameplayTags");
    Out.Description = TEXT("Add, rename or remove one explicit project GameplayTag using the engine INI editor. Writes config immediately; no transaction rollback or batch. Remove rejects descendants and asset references. Rename uses engine redirects.");
    auto Operation = FNexusSchema::Object().Required(TEXT("Action"), FNexusSchema::Enum(TEXT("Tag edit"), {TEXT("Add"), TEXT("Rename"), TEXT("Remove")}))
        .Prop(TEXT("NewTag"), FNexusSchema::Str(TEXT("Rename destination; must not already exist")))
        .Prop(TEXT("TagSource"), FNexusSchema::Str(TEXT("Add target source filename, e.g. ProjectTags.ini; no directory paths")))
        .Prop(TEXT("Comment"), FNexusSchema::Str(TEXT("Add developer comment"))).Build();
    Out.InputSchema = FNexusSchema::Object().Required(TEXT("Tag"), FNexusSchema::Str(TEXT("Single target tag; subsequent operations follow the renamed name")))
        .Required(TEXT("Operations"), FNexusSchema::ArrayOf(TEXT("Ordered edits; prior successful config writes are retained after failure"), Operation.ToSharedRef())).Build();
    Out.Tags = {FNexusMcpTags::Editor, FNexusMcpTags::Write, FNexusMcpTags::Dangerous};
    Out.bAutoTransaction = false; Out.bAllowBatch = false; Out.bCacheable = false;
    Out.RelatedCapabilities = {TEXT("GetGameplayTags")}; Out.ExtraSearchKeywords = {TEXT("tag"), TEXT("gameplaytags"), TEXT("rename"), TEXT("source")};
}
bool FNexusManageGameplayTagsCapability::PrepareTarget(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Entry, void*& OutTarget, FString& Error) const
{
    const FString Tag = FNexusArgs(Args).Str(TEXT("Tag")); FText Validation;
    if (!UGameplayTagsManager::Get().IsValidGameplayTagString(Tag, &Validation)) { Error = TEXT("Invalid Tag: ") + Validation.ToString(); return false; }
    Entry->SetStringField(TEXT("Tag"), Tag); OutTarget = new FString(Tag); return true;
}
void FNexusManageGameplayTagsCapability::FinalizeTarget(void* Target) const { delete static_cast<FString*>(Target); }
static void NexusEditGameplayTag(const TSharedPtr<FJsonObject>& Operation, FNexusActionContext& Context)
{
    FString* Tag = static_cast<FString*>(Context.Target);
    if (!Tag) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Tag target is missing")); return; }
    UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
    IGameplayTagsEditorModule& Editor = FModuleManager::LoadModuleChecked<IGameplayTagsEditorModule>(TEXT("GameplayTagsEditor"));
    const FNexusArgs Args(Operation); auto Node = Manager.FindTagNode(FName(**Tag)); bool bApplied = false;
    if (Context.Action == TEXT("Add")) {
        if (Node.IsValid()) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Tag already exists, including implicit parent tags")); return; }
        const FString Source = Args.Str(TEXT("TagSource"));
        if (Source.IsEmpty() || Source != FPaths::GetCleanFilename(Source) || !Source.EndsWith(TEXT(".ini")) || Source.Contains(TEXT("..")) || Source.Contains(TEXT(":"))) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Add requires a simple TagSource .ini filename")); return; }
        if (FNexusNativeEditorTools::UseNative(TEXT("GameplayTagsToolset")) && !FNexusNativeEditorTools::CanCall(TEXT("GameplayTagsToolset"), TEXT("AddTag"))) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Official AddTag is unavailable, disabled or filtered")); return; }
#if NX_UE_HAS_GAMEPLAY_TAG_SOURCE_CREATION
        if (!Manager.FindTagSource(FName(*Source)) && !Editor.AddNewGameplayTagSource(Source)) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Could not create TagSource")); return; }
#endif
        // UE 4.26 的 AddNewGameplayTagToINI 会同时创建缺失来源并落盘。
        auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("TagName"), *Tag); Input->SetStringField(TEXT("Comment"), Args.Str(TEXT("Comment"))); Input->SetStringField(TEXT("TagSource"), Source);
        if (FNexusNativeEditorTools::EditSync(TEXT("GameplayTagsToolset"), TEXT("AddTag"), Input, Context.Entry)) { if (Context.Entry->HasField(TEXT("Error"))) return; bApplied = Manager.FindTagNode(FName(**Tag)).IsValid(); }
        else bApplied = Editor.AddNewGameplayTagToINI(*Tag, Args.Str(TEXT("Comment")), FName(*Source));
    } else {
        if (!Node.IsValid() || !Node->IsExplicitTag()) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Tag must exist explicitly in a writable INI source")); return; }
        FString Comment; FName SourceName; bool bExplicit = false, bRestricted = false, bAllowChildren = false;
        Manager.GetTagEditorData(FName(**Tag), Comment, SourceName, bExplicit, bRestricted, bAllowChildren);
        const FGameplayTagSource* Source = Manager.FindTagSource(SourceName);
        if (!Source || Source->SourceType != EGameplayTagSourceType::TagList) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Only explicit tags in ordinary INI TagList sources can be edited")); return; }
        if (Context.Action == TEXT("Rename")) {
            const FString NewTag = Args.Str(TEXT("NewTag")); FText Validation;
            if (!UGameplayTagsManager::Get().IsValidGameplayTagString(NewTag, &Validation) || Manager.FindTagNode(FName(*NewTag)).IsValid() || NewTag.StartsWith(*Tag + TEXT("."))) { Context.Entry->SetStringField(TEXT("Error"), TEXT("NewTag is invalid, already exists or is a descendant of Tag")); return; }
            auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("OldTagName"), *Tag); Input->SetStringField(TEXT("NewTagName"), NewTag);
            if (FNexusNativeEditorTools::EditSync(TEXT("GameplayTagsToolset"), TEXT("RenameTag"), Input, Context.Entry)) { if (Context.Entry->HasField(TEXT("Error"))) return; bApplied = Manager.FindTagNode(FName(*NewTag)).IsValid(); }
            else bApplied = Editor.RenameTagInINI(*Tag, NewTag);
            if (bApplied) { Context.Entry->SetStringField(TEXT("PreviousTag"), *Tag); *Tag = NewTag; }
        } else {
            if (Node->GetChildTagNodes().Num()) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Remove child tags first")); return; }
            TArray<FString> References; FString Failure;
            if (!FNexusGameplayTagReferencerUtils::FindReferencerPackagePaths(*Tag, References, Failure)) { Context.Entry->SetStringField(TEXT("Error"), Failure); return; }
            if (References.Num()) { Context.Entry->SetStringField(TEXT("Error"), TEXT("Tag has asset references; remove/rewrite references first")); return; }
            auto Input = MakeShared<FJsonObject>(); Input->SetStringField(TEXT("TagName"), *Tag);
            if (FNexusNativeEditorTools::EditSync(TEXT("GameplayTagsToolset"), TEXT("RemoveTag"), Input, Context.Entry)) { if (Context.Entry->HasField(TEXT("Error"))) return; bApplied = !Manager.FindTagNode(FName(**Tag)).IsValid(); }
            else bApplied = Editor.DeleteTagFromINI(Node);
        }
    }
    Context.Entry->SetStringField(TEXT("Tag"), *Tag); Context.Entry->SetBoolField(TEXT("bApplied"), bApplied);
    Context.Entry->SetBoolField(TEXT("bWritesConfigImmediately"), true);
    if (!bApplied) Context.Entry->SetStringField(TEXT("Error"), TEXT("Engine INI edit failed; inspect source permissions and tag import settings"));
}
void FNexusManageGameplayTagsCapability::RegisterActions(TMap<FString, FNexusActionHandler>& OutHandlers) const
{
    OutHandlers.Add(TEXT("Add"), &NexusEditGameplayTag); OutHandlers.Add(TEXT("Rename"), &NexusEditGameplayTag); OutHandlers.Add(TEXT("Remove"), &NexusEditGameplayTag);
}
REGISTER_MCP_CAPABILITY(FNexusManageGameplayTagsCapability)
#endif
