// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetProjectToolsetsCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusLinkSettings.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusHostUtils.h"
#include "Utils/NexusEditorActionSchema.h"

static FString NexusIndependentGroup(const FCapRecord& Record)
{
    return TEXT("NexusLink.") + (Record.SourceRelDir.IsEmpty() ? TEXT("Capabilities") : Record.SourceRelDir.Replace(TEXT("/"), TEXT(".")));
}

static bool NexusIndependentVisible(const FCapRecord& Record)
{
    const auto* Settings = UNexusLinkSettings::Get();
    return Settings && Settings->IsCapabilityEnabled(Record.Def.Name) && FNexusHostUtils::IsCapabilityVisibleOnHost(Record);
}

void FNexusGetProjectToolsetsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("GetProjectToolsets");
    Out.Description = TEXT("Discover independent capability groups, tools and action schemas. Reads NexusLink metadata; no external registry.");
    Out.InputSchema = BuildSchemaWithSections();
    Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
    Out.bCacheable = false;
    Out.Prerequisites = {TEXT("EditorOnly")};
    Out.ExtraSearchKeywords = {TEXT("toolset"), TEXT("actions"), TEXT("schema"), TEXT("discover")};
    Out.RelatedCapabilities = {TEXT("GetProjectPlugins"), TEXT("GetEditorAnimation"), TEXT("ControlEditorAnimation"), TEXT("ControlEditorNiagara")};
}

TSharedPtr<FJsonObject> FNexusGetProjectToolsetsCapability::BuildCapabilitySchema() const
{
    return FNexusSchema::Object()
        .Prop(TEXT("ToolsetName"), FNexusSchema::Str(TEXT("Exact independent group from Toolsets; required for Tools/Schema")))
        .Prop(TEXT("ToolName"), FNexusSchema::Str(TEXT("Exact capability name from Tools; required for Schema")))
        .Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Case-insensitive name substring for listing")))
        .Prop(TEXT("Capability"), FNexusSchema::Str(TEXT("Exact editor capability for Actions")))
        .Prop(TEXT("Action"), FNexusSchema::Str(TEXT("Exact action for Actions; omit to list")))
        .Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
        .Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Maximum rows"), 100, 1, 500)).Build();
}

TArray<FString> FNexusGetProjectToolsetsCapability::GetSectionNames() const
{
    return {TEXT("Toolsets"), TEXT("Tools"), TEXT("Schema"), TEXT("AnimationRoutes"), TEXT("Actions")};
}

TArray<FString> FNexusGetProjectToolsetsCapability::GetDefaultSectionNames() const
{
    return {TEXT("Actions")};
}

void FNexusGetProjectToolsetsCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args,
    void*, TSharedPtr<FJsonObject>& Out, FString& Error) const
{
    const FNexusArgs Arguments(Args);
    TArray<TSharedPtr<FJsonValue>> Items;
    if (SectionName == TEXT("Actions") || SectionName == TEXT("AnimationRoutes"))
    {
        Items = FNexusEditorActionSchema::List(Arguments.Str(TEXT("Capability")), Arguments.Str(TEXT("Action")));
        Items.RemoveAll([&](const TSharedPtr<FJsonValue>& Value)
        {
            const auto Entry = Value->AsObject();
            const auto* Record = FNexusCapabilityRegistry::Get().FindRecordByName(Entry->GetStringField(TEXT("Capability")));
            return !Record || !NexusIndependentVisible(*Record)
                || (SectionName == TEXT("AnimationRoutes") && !Record->Def.Name.EndsWith(TEXT("EditorAnimation")));
        });
    }
    else
    {
        const FString GroupName = Arguments.Str(TEXT("ToolsetName"));
        if (SectionName != TEXT("Toolsets") && GroupName.IsEmpty())
        {
            Error = TEXT("ToolsetName must identify an independent group returned by Toolsets");
            return;
        }
        TMap<FString, int32> Groups;
        bool bFoundGroup = false;
        for (const auto& Record : FNexusCapabilityRegistry::Get().GetAllRecords())
        {
            if (!NexusIndependentVisible(Record)) continue;
            const FString RecordGroup = NexusIndependentGroup(Record);
            if (SectionName == TEXT("Toolsets"))
            {
                Groups.FindOrAdd(RecordGroup)++;
                continue;
            }
            if (GroupName != RecordGroup) continue;
            bFoundGroup = true;
            if (SectionName == TEXT("Schema"))
            {
                if (Arguments.Str(TEXT("ToolName")) != Record.Def.Name) continue;
                auto Schema = MakeShared<FJsonObject>();
                Schema->SetStringField(TEXT("Name"), Record.Def.Name);
                Schema->SetStringField(TEXT("Description"), Record.Def.Description);
                Schema->SetStringField(TEXT("Backend"), TEXT("Independent"));
                if (Record.Def.InputSchema.IsValid()) Schema->SetObjectField(TEXT("InputSchema"), Record.Def.InputSchema);
                if (Record.Def.OutputSchema.IsValid()) Schema->SetObjectField(TEXT("OutputSchema"), Record.Def.OutputSchema);
                Out->SetObjectField(TEXT("Schema"), Schema);
                return;
            }
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("Name"), Record.Def.Name);
            Item->SetStringField(TEXT("Description"), Record.Def.Description);
            Item->SetStringField(TEXT("Backend"), TEXT("Independent"));
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        if (SectionName == TEXT("Toolsets"))
        {
            for (const auto& Group : Groups)
            {
                auto Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("Name"), Group.Key);
                Item->SetStringField(TEXT("Description"), TEXT("Independent NexusLink capabilities"));
                Item->SetStringField(TEXT("Backend"), TEXT("Independent"));
                Item->SetNumberField(TEXT("ToolCount"), Group.Value);
                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
        else if (!bFoundGroup || SectionName == TEXT("Schema"))
        {
            Error = bFoundGroup ? TEXT("ToolName must identify an enabled capability from Tools")
                : TEXT("Unknown independent group; query Toolsets first");
            return;
        }
    }
    const FString NameFilter = Arguments.Str(TEXT("NameFilter"));
    if (!NameFilter.IsEmpty()) Items.RemoveAll([&](const TSharedPtr<FJsonValue>& Value)
    {
        return !Value->AsObject()->GetStringField(TEXT("Name")).Contains(NameFilter);
    });
    Items.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
    {
        return Left->AsObject()->GetStringField(TEXT("Name")) < Right->AsObject()->GetStringField(TEXT("Name"));
    });
    Out->SetStringField(TEXT("Backend"), TEXT("Independent"));
    Out->SetNumberField(TEXT("Total"), Items.Num());
    const int32 Offset = FMath::Clamp(static_cast<int32>(Arguments.Num(TEXT("Offset"))), 0, Items.Num());
    const int32 Limit = FMath::Clamp(static_cast<int32>(Arguments.Num(TEXT("Limit"), 100)), 1, 500);
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < Items.Num() && Page.Num() < Limit; ++Index) Page.Add(Items[Index]);
    Out->SetNumberField(TEXT("Returned"), Page.Num());
    Out->SetArrayField(SectionName, Page);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectToolsetsCapability)
