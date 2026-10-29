// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetProjectDataRegistryCapability.h"
#if WITH_DATA_REGISTRY
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusStringMatchUtils.h"
#include "DataRegistry.h"
#include "DataRegistrySubsystem.h"
#include "DataRegistrySource.h"
#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"
void FNexusGetProjectDataRegistryCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
    Out.Name = TEXT("GetProjectDataRegistry"); Out.Description = TEXT("Inspect registered Data Registries, row schema, item IDs, configured/runtime sources and explicitly named cached values. Cache misses are reported per item; this read does not acquire or load data.");
    Out.InputSchema = BuildSchemaWithSections(); Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor}; Out.bCacheable = false;
    Out.Prerequisites = {TEXT("DataRegistry")}; Out.ExtraSearchKeywords = {TEXT("registry"), TEXT("dataregistry"), TEXT("source"), TEXT("cache")};
}
TSharedPtr<FJsonObject> FNexusGetProjectDataRegistryCapability::BuildCapabilitySchema() const
{
    return FNexusSchema::Object().Prop(TEXT("RegistryName"), FNexusSchema::Str(TEXT("Registered type name from Registries; required except for Registries")))
        .Prop(TEXT("ItemNames"), FNexusSchema::StrArr(TEXT("Values requires 1-100 exact item names")))
        .Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Name substring/wildcard for discovery")))
        .Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Discovery offset"), 0, 0))
        .Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Discovery page size"), 100, 1, 500)).Build();
}
TArray<FString> FNexusGetProjectDataRegistryCapability::GetSectionNames() const { return {TEXT("Registries"), TEXT("Info"), TEXT("Schema"), TEXT("Items"), TEXT("DataSources"), TEXT("RuntimeSources"), TEXT("Values")}; }
TArray<FString> FNexusGetProjectDataRegistryCapability::GetDefaultSectionNames() const { return {TEXT("Registries")}; }
void FNexusGetProjectDataRegistryCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args, void*, TSharedPtr<FJsonObject>& Out, FString& Error) const
{
    const FNexusArgs Arguments(Args); UDataRegistrySubsystem* Subsystem = UDataRegistrySubsystem::Get();
    if (!Subsystem) { Error = TEXT("DataRegistry subsystem is unavailable"); return; }
    TArray<TSharedPtr<FJsonValue>> Items;
    auto Add = [&](const FString& Name) { auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Name); Items.Add(MakeShared<FJsonValueObject>(Item)); return Item; };
    if (SectionName == TEXT("Registries")) {
        TArray<UDataRegistry*> Registries; Subsystem->GetAllRegistries(Registries);
        for (const UDataRegistry* Registry : Registries) if (Registry) { auto Item = Add(Registry->GetRegistryType().ToString()); Item->SetStringField(TEXT("AssetPath"), Registry->GetPathName()); if (Registry->GetItemStruct()) Item->SetStringField(TEXT("ItemStruct"), Registry->GetItemStruct()->GetPathName()); }
    } else {
        UDataRegistry* Registry = Subsystem->GetRegistryForType(FName(*Arguments.Str(TEXT("RegistryName"))));
        if (!Registry) { Error = TEXT("RegistryName not registered; discover Registries first"); return; }
        const UScriptStruct* Struct = Registry->GetItemStruct();
        Out->SetStringField(TEXT("RegistryName"), Registry->GetRegistryType().ToString());
        if (SectionName == TEXT("Info")) {
            auto Info = MakeShared<FJsonObject>(); Info->SetStringField(TEXT("AssetPath"), Registry->GetPathName()); Info->SetStringField(TEXT("Description"), Registry->GetRegistryDescription().ToString());
            Info->SetStringField(TEXT("ItemStruct"), Struct ? Struct->GetPathName() : TEXT("")); TArray<FDataRegistryId> Ids; Registry->GetPossibleRegistryIds(Ids); Info->SetNumberField(TEXT("ItemCount"), Ids.Num());
            Info->SetStringField(TEXT("Availability"), StaticEnum<EDataRegistryAvailability>()->GetNameStringByValue(static_cast<int64>(Registry->GetLowestAvailability()))); Out->SetObjectField(TEXT("Info"), Info); return;
        } else if (SectionName == TEXT("Schema")) {
            if (!Struct) { Error = TEXT("Registry has no item struct"); return; }
            for (TFieldIterator<FProperty> Property(Struct); Property; ++Property) Add(Property->GetName())->SetStringField(TEXT("Type"), Property->GetCPPType());
        } else if (SectionName == TEXT("Items")) {
            TArray<FDataRegistryId> Ids; Registry->GetPossibleRegistryIds(Ids); for (const auto& Id : Ids) Add(Id.ItemName.ToString());
        } else if (SectionName == TEXT("Values")) {
            const TArray<FString> Names = Arguments.StrArr(TEXT("ItemNames"));
            if (Names.Num() == 0 || Names.Num() > 100) { Error = TEXT("Values requires 1-100 ItemNames"); return; }
            for (const FString& Name : Names) {
                const uint8* Memory = nullptr; const UScriptStruct* ItemStruct = nullptr;
                const auto CacheResult = Subsystem->GetCachedItemRaw(Memory, ItemStruct, FDataRegistryId(Registry->GetRegistryType(), FName(*Name)));
                auto Item = Add(Name); const bool bCached = CacheResult && Memory && ItemStruct; Item->SetBoolField(TEXT("bCached"), bCached);
                if (bCached) { auto Value = MakeShared<FJsonObject>(); if (!FJsonObjectConverter::UStructToJsonObject(ItemStruct, Memory, Value, 0, 0)) { Error = TEXT("Could not serialize cached row"); return; } Item->SetObjectField(TEXT("Value"), Value); }
                else Item->SetStringField(TEXT("Status"), TEXT("NotCached; acquire through the consuming game system before reading"));
            }
        } else {
            // 通过反射读取引擎已有 UPROPERTY，兼容尚无公开数组 getter 的版本。
            const FArrayProperty* Property = FindFProperty<FArrayProperty>(Registry->GetClass(), *SectionName);
            const FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
            if (!Inner) { Error = TEXT("Source list is unavailable on this engine version"); return; }
            FScriptArrayHelper Sources(Property, Property->ContainerPtrToValuePtr<void>(Registry));
            for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex) {
                UDataRegistrySource* Source = Cast<UDataRegistrySource>(Inner->GetObjectPropertyValue(Sources.GetRawPtr(SourceIndex))); if (!Source) continue;
                auto Item = Add(Source->GetName()); Item->SetStringField(TEXT("ClassPath"), Source->GetClass()->GetPathName()); Item->SetStringField(TEXT("AssetPath"), Source->GetSourceAssetPath().ToString());
                Item->SetStringField(TEXT("Description"), Source->GetDebugString()); Item->SetBoolField(TEXT("bInitialized"), Source->IsInitialized()); Item->SetBoolField(TEXT("bTransient"), Source->IsTransientSource());
                Item->SetStringField(TEXT("Availability"), StaticEnum<EDataRegistryAvailability>()->GetNameStringByValue(static_cast<int64>(Source->GetSourceAvailability())));
            }
        }
    }
    if (SectionName != TEXT("Values")) { const FString Filter = Arguments.Str(TEXT("NameFilter")); Items.RemoveAll([&](const auto& Item) { return !Filter.IsEmpty() && !FNexusStringMatchUtils::Matches(Item->AsObject()->GetStringField(TEXT("Name")), Filter); }); }
    Items.Sort([](const auto& Left, const auto& Right) { return Left->AsObject()->GetStringField(TEXT("Name")) < Right->AsObject()->GetStringField(TEXT("Name")); });
    Out->SetNumberField(SectionName + TEXT("TotalCount"), Items.Num());
    if (SectionName != TEXT("Values")) { const int32 Start = FMath::Min(Items.Num(), static_cast<int32>(Arguments.Num(TEXT("Offset")))); const int32 Count = FMath::Min(Items.Num() - Start, static_cast<int32>(Arguments.Num(TEXT("Limit"), 100))); TArray<TSharedPtr<FJsonValue>> Page; for (int32 ItemIndex = Start; ItemIndex < Start + Count; ++ItemIndex) Page.Add(Items[ItemIndex]); Items = MoveTemp(Page); }
    Out->SetArrayField(SectionName, Items);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectDataRegistryCapability)
#endif
