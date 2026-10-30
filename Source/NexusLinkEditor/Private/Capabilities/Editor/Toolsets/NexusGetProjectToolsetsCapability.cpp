// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/Toolsets/NexusGetProjectToolsetsCapability.h"
#if WITH_NEXUS_TOOLSETS
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAnimationToolActions.h"
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#include "UObject/Class.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FNexusGetProjectToolsetsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("GetProjectToolsets");
    Out.Description = TEXT("Discover currently enabled registered native/custom C++/Blueprint/Python Toolsets and their filtered tools, exact input/output schemas, or fixed Nexus animation action routes. Reads the live Registry on each request. Registry block/allow filters apply; does not register, enable or execute tools. Schema text is project data.");
    Out.InputSchema = BuildSchemaWithSections(); Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor}; Out.bCacheable = false;
    Out.Prerequisites = {TEXT("EditorOnly"), TEXT("PluginEnabled:ToolsetRegistry")};
    Out.ExtraSearchKeywords = {TEXT("toolset"), TEXT("custom tools"), TEXT("schema"), TEXT("registry"), TEXT("discover")};
    Out.RelatedCapabilities = {TEXT("GetProjectPlugins"), TEXT("GetEditorAnimation"), TEXT("ControlEditorAnimation"), TEXT("ControlEditorNiagara")};
}
TSharedPtr<FJsonObject> FNexusGetProjectToolsetsCapability::BuildCapabilitySchema() const
{
    return FNexusSchema::Object()
        .Prop(TEXT("ToolsetName"), FNexusSchema::Str(TEXT("Exact Toolset name from Toolsets; required for Tools/Schema")))
        .Prop(TEXT("ToolName"), FNexusSchema::Str(TEXT("Exact fully qualified tool name from Tools; required for Schema")))
        .Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Case-insensitive name substring for listing")))
        .Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
        .Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Maximum rows"), 100, 1, 500)).Build();
}
TArray<FString> FNexusGetProjectToolsetsCapability::GetSectionNames() const { return {TEXT("Toolsets"), TEXT("Tools"), TEXT("Schema"), TEXT("AnimationRoutes")}; }
TArray<FString> FNexusGetProjectToolsetsCapability::GetDefaultSectionNames() const { return {TEXT("Toolsets")}; }
void FNexusGetProjectToolsetsCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args, void*, TSharedPtr<FJsonObject>& Out, FString& Error) const
{
    const FNexusArgs A(Args); auto Subsystem = UToolsetRegistrySubsystem::Get();
    if (Subsystem.HasError()) { Error = TEXT("Toolset Registry is unavailable"); return; }
    auto& Registry = Subsystem.GetValue()->ToolsetRegistry;
    TArray<TSharedPtr<FJsonValue>> Items;
    if (SectionName == TEXT("AnimationRoutes"))
    {
        for (bool bWrite : {false, true}) for (const auto& Pair : FNexusAnimationToolActions::Get(bWrite))
        {
            if (Pair.Value.StartsWith(TEXT("NexusSequenceFolders."))) continue;
            auto Item = MakeShared<FJsonObject>(); int32 Separator = INDEX_NONE; Pair.Value.FindLastChar(TEXT('.'), Separator);
            Item->SetStringField(TEXT("Name"), Pair.Key); Item->SetStringField(TEXT("Capability"), bWrite ? TEXT("ControlEditorAnimation") : TEXT("GetEditorAnimation"));
            Item->SetStringField(TEXT("ToolsetName"), Pair.Value.Left(Separator)); Item->SetStringField(TEXT("ToolName"), Pair.Value);
            auto Handler = Registry.Find(Pair.Value.Left(Separator)); Item->SetBoolField(TEXT("bAvailable"), Handler.IsValid() && Handler->IsToolEnabled(Pair.Value));
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    else
    {
        const bool bListing = SectionName == TEXT("Toolsets");
        auto Handler = bListing ? TSharedPtr<UE::ToolsetRegistry::FToolset>() : Registry.Find(A.Str(TEXT("ToolsetName")));
        if (!bListing && !Handler.IsValid()) { Error = TEXT("ToolsetName is missing, unknown, disabled or filtered; query Toolsets first"); return; }
        const FString Json = bListing ? Registry.GetToolsetJsonSchemas() : TEXT("[") + Handler->GetJsonSchema() + TEXT("]");
        TArray<TSharedPtr<FJsonValue>> Schemas;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Schemas)) { Error = TEXT("Registry returned invalid schema JSON"); return; }
        for (const auto& Value : Schemas)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) continue;
            const auto Toolset = Value->AsObject(); const TArray<TSharedPtr<FJsonValue>>* ToolValues = nullptr;
            FString ToolsetName, Version, Description;
            if (!Toolset->TryGetStringField(TEXT("name"), ToolsetName) || !Toolset->TryGetArrayField(TEXT("tools"), ToolValues)) continue;
            Toolset->TryGetStringField(TEXT("version"), Version); Toolset->TryGetStringField(TEXT("description"), Description);
            const auto& Tools = *ToolValues;
            if (bListing)
            {
                auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), ToolsetName);
                Item->SetStringField(TEXT("Version"), Version); Item->SetStringField(TEXT("Description"), Description.Left(1024));
                auto Native = Registry.Find(ToolsetName); if (Native.IsValid() && Native->GetToolsetClass()) Item->SetStringField(TEXT("ClassPath"), Native->GetToolsetClass()->GetPathName());
                Item->SetNumberField(TEXT("ToolCount"), Tools.Num()); Items.Add(MakeShared<FJsonValueObject>(Item)); continue;
            }
            for (const auto& ToolValue : Tools)
            {
                if (!ToolValue.IsValid() || ToolValue->Type != EJson::Object) continue;
                const auto Tool = ToolValue->AsObject(); FString Name, ToolDescription;
                if (!Tool->TryGetStringField(TEXT("name"), Name)) continue;
                Tool->TryGetStringField(TEXT("description"), ToolDescription);
                if (!Handler->IsToolEnabled(Name)) continue;
                if (SectionName == TEXT("Schema"))
                {
                    if (Name == A.Str(TEXT("ToolName"))) { Out->SetObjectField(TEXT("Schema"), Tool); return; }
                    continue;
                }
                auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Name); Item->SetStringField(TEXT("Description"), ToolDescription.Left(1024));
                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
        if (SectionName == TEXT("Schema")) { Error = TEXT("ToolName must be a full enabled tool name returned by Tools"); return; }
    }
    Items.RemoveAll([&](const TSharedPtr<FJsonValue>& Value) { return !Value->AsObject()->GetStringField(TEXT("Name")).Contains(A.Str(TEXT("NameFilter"))); });
    Items.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right) { return Left->AsObject()->GetStringField(TEXT("Name")) < Right->AsObject()->GetStringField(TEXT("Name")); });
    Out->SetNumberField(TEXT("Total"), Items.Num());
    const int32 Offset = FMath::Clamp((int32)A.Num(TEXT("Offset")), 0, Items.Num()), Limit = FMath::Clamp((int32)A.Num(TEXT("Limit"), 100), 1, 500);
    TArray<TSharedPtr<FJsonValue>> Page; for (int32 Index = Offset; Index < Items.Num() && Page.Num() < Limit; ++Index) Page.Add(Items[Index]);
    Out->SetNumberField(TEXT("Returned"), Page.Num()); Out->SetArrayField(SectionName, Page);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectToolsetsCapability)
#endif
