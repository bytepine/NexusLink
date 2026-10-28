// Copyright byteyang. All Rights Reserved.
#include "Capabilities/Editor/NexusGetProjectSettingsCapability.h"
#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "NexusMcpTool.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusStringMatchUtils.h"
#include "ISettingsModule.h"
#include "ISettingsContainer.h"
#include "ISettingsCategory.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"

void FNexusGetProjectSettingsCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("GetProjectSettings");
	Out.Description = TEXT("Discover registered project/editor settings, inspect property types, and read explicitly named values.");
	Out.InputSchema = BuildSchemaWithSections();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Editor};
	Out.ExtraSearchKeywords = {TEXT("configuration"), TEXT("preferences"), TEXT("ini")};
}
TSharedPtr<FJsonObject> FNexusGetProjectSettingsCapability::BuildCapabilitySchema() const
{
	return FNexusSchema::Object()
		.Prop(TEXT("Container"), FNexusSchema::Str(TEXT("Registered container; discover Containers first"), TEXT("Project")))
		.Prop(TEXT("Category"), FNexusSchema::Str(TEXT("Category name; required for Sections/Properties/Values")))
		.Prop(TEXT("Section"), FNexusSchema::Str(TEXT("Section name; required for Properties/Values")))
		.Prop(TEXT("PropertyNames"), FNexusSchema::StrArr(TEXT("Exact editable property names; required for Values, maximum 100")))
		.Prop(TEXT("NameFilter"), FNexusSchema::Str(TEXT("Name substring or wildcard for discovery")))
		.Prop(TEXT("Offset"), FNexusSchema::Int(TEXT("Pagination offset"), 0, 0))
		.Prop(TEXT("Limit"), FNexusSchema::Int(TEXT("Page size"), 100, 1, 500)).Build();
}
TArray<FString> FNexusGetProjectSettingsCapability::GetSectionNames() const { return {TEXT("Containers"), TEXT("Categories"), TEXT("Sections"), TEXT("Properties"), TEXT("Values")}; }
TArray<FString> FNexusGetProjectSettingsCapability::GetDefaultSectionNames() const { return {TEXT("Containers")}; }

void FNexusGetProjectSettingsCapability::ExecuteSection(const FString& SectionName, const TSharedPtr<FJsonObject>& Args,
	void*, TSharedPtr<FJsonObject>& Out, FString& Error) const
{
	const FNexusArgs A(Args);
	ISettingsModule& Settings = FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings"));
	TArray<TSharedPtr<FJsonValue>> Items;
	auto Add = [&](const FString& Name, const FString& Label)
	{
		const FString Filter = A.Str(TEXT("NameFilter"));
		if (!Filter.IsEmpty() && !FNexusStringMatchUtils::Matches(Name, Filter)) return;
		auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Name);
		if (!Label.IsEmpty()) Item->SetStringField(TEXT("DisplayName"), Label);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	};
	if (SectionName == TEXT("Containers"))
	{
		TArray<FName> Names; Settings.GetContainerNames(Names);
		for (FName Name : Names) Add(Name.ToString(), TEXT(""));
	}
	else
	{
		auto Container = Settings.GetContainer(FName(*A.Str(TEXT("Container"), TEXT("Project"))));
		if (!Container.IsValid()) { Error = TEXT("Settings container not found; query Containers first"); return; }
		if (SectionName == TEXT("Categories"))
		{
			TArray<TSharedPtr<ISettingsCategory>> Categories; Container->GetCategories(Categories);
			for (const auto& Category : Categories) Add(Category->GetName().ToString(), Category->GetDisplayName().ToString());
		}
		else
		{
			auto Category = Container->GetCategory(FName(*A.Str(TEXT("Category"))));
			if (!Category.IsValid()) { Error = TEXT("Category is required; query Categories first"); return; }
			if (SectionName == TEXT("Sections"))
			{
				TArray<TSharedPtr<ISettingsSection>> Sections; Category->GetSections(Sections);
				for (const auto& Section : Sections) Add(Section->GetName().ToString(), Section->GetDisplayName().ToString());
			}
			else
			{
				auto Section = Category->GetSection(FName(*A.Str(TEXT("Section"))));
				UObject* Object = Section.IsValid() ? Section->GetSettingsObject().Get() : nullptr;
				if (!Object) { Error = TEXT("Section has no readable settings UObject; query Sections first"); return; }
				Out->SetStringField(TEXT("SettingsClass"), Object->GetClass()->GetPathName());
				const TArray<FString> Names = A.StrArr(TEXT("PropertyNames"));
				if (SectionName == TEXT("Values") && (Names.Num() == 0 || Names.Num() > 100)) { Error = TEXT("Values requires 1-100 explicit PropertyNames"); return; }
				for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
				{
					if (!It->HasAnyPropertyFlags(CPF_Edit) || (Names.Num() && !Names.Contains(It->GetName()))) continue;
					const int32 Before = Items.Num(); Add(It->GetName(), TEXT(""));
					if (Items.Num() == Before) continue;
					auto Item = Items.Last()->AsObject(); Item->SetStringField(TEXT("Type"), It->GetCPPType());
					if (SectionName == TEXT("Values"))
					{
						FString Value; FNexusPropertyUtils::ExportText(*It, Value, It->ContainerPtrToValuePtr<void>(Object));
						Item->SetStringField(TEXT("Value"), Value.Left(8192));
						if (Value.Len() > 8192) Item->SetBoolField(TEXT("bTruncated"), true);
					}
				}
				if (SectionName == TEXT("Values")) for (const auto& Name : Names)
					if (!FindFProperty<FProperty>(Object->GetClass(), *Name) || !FindFProperty<FProperty>(Object->GetClass(), *Name)->HasAnyPropertyFlags(CPF_Edit)) { Error = FString::Printf(TEXT("Editable settings property not found: %s"), *Name); return; }
			}
		}
	}
	Items.Sort([](const TSharedPtr<FJsonValue>& L, const TSharedPtr<FJsonValue>& R) { return L->AsObject()->GetStringField(TEXT("Name")) < R->AsObject()->GetStringField(TEXT("Name")); });
	Out->SetNumberField(SectionName + TEXT("TotalCount"), Items.Num());
	const int32 Start = FMath::Min(Items.Num(), static_cast<int32>(A.Num(TEXT("Offset"))));
	const int32 Count = FMath::Min(Items.Num() - Start, static_cast<int32>(A.Num(TEXT("Limit"), 100)));
	TArray<TSharedPtr<FJsonValue>> Page; for (int32 I = Start; I < Start + Count; ++I) Page.Add(Items[I]);
	Out->SetArrayField(SectionName, Page);
}
REGISTER_MCP_CAPABILITY(FNexusGetProjectSettingsCapability)
