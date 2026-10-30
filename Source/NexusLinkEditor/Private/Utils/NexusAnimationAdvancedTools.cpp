// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAnimationAdvancedTools.h"
#include "Utils/NexusLegacyToolsets.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusEditorToolsets.h"
#include "JsonObjectConverter.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneFolder.h"

static FString NexusAnimationFieldKey(FString Name) { Name.ReplaceInline(TEXT("_"),TEXT("")); return Name.ToLower(); }

// 仅用于下方固定脚本 API 映射，不接受调用者提供类名或函数名。
static bool NexusAnimationImportProperty(FProperty* Property,void* Address,const TSharedPtr<FJsonValue>& Value,FString& Error,int32 Depth=0)
{
    if (!Value || Depth>8) { Error=TEXT("Invalid animation parameter nesting"); return false; }
    if (auto* ObjectProperty=CastField<FObjectPropertyBase>(Property)) {
        if (Value->Type==EJson::Null) { ObjectProperty->SetObjectPropertyValue(Address,nullptr); return true; }
        auto ObjectArgs=MakeShared<FJsonObject>(); ObjectArgs->SetField(TEXT("object"),Value); UObject* Object=FNexusLegacyToolsets::Resolve(ObjectArgs,TEXT("object"));
        const UClass* Required=ObjectProperty->PropertyClass;
        if (!Object || !Object->IsA(Required)) { Error=TEXT("Animation object reference has the wrong class"); return false; }
        if (auto* ClassProperty=CastField<FClassProperty>(Property)) if (!Cast<UClass>(Object) || !CastChecked<UClass>(Object)->IsChildOf(ClassProperty->MetaClass)) { Error=TEXT("Animation class does not satisfy the required base class"); return false; }
        ObjectProperty->SetObjectPropertyValue(Address,Object); return true;
    }
    if (auto* Array=CastField<FArrayProperty>(Property)) {
        if (Value->Type!=EJson::Array || Value->AsArray().Num()>128) { Error=TEXT("Animation arrays are limited to 128 entries"); return false; }
        FScriptArrayHelper Helper(Array,Address); Helper.Resize(Value->AsArray().Num()); for (int32 Element=0;Element<Helper.Num();++Element) if (!NexusAnimationImportProperty(Array->Inner,Helper.GetRawPtr(Element),Value->AsArray()[Element],Error,Depth+1)) return false; return true;
    }
    if (auto* Struct=CastField<FStructProperty>(Property)) {
        if (Struct->Struct==TBaseStructure<FGuid>::Get() && Value->Type==EJson::String) { FGuid Guid; if (!FGuid::Parse(Value->AsString(),Guid)) { Error=TEXT("Invalid binding GUID"); return false; } *static_cast<FGuid*>(Address)=Guid; return true; }
        if (Value->Type!=EJson::Object) { Error=TEXT("Animation struct parameter requires an object"); return false; }
        for (const auto& Pair : Value->AsObject()->Values) {
            FProperty* Member=nullptr; const FString Key=NexusAnimationFieldKey(FString(*Pair.Key));
            for (TFieldIterator<FProperty> It(Struct->Struct);It;++It) if (NexusAnimationFieldKey(It->GetName())==Key) { Member=*It; break; }
            if (!Member || !NexusAnimationImportProperty(Member,Member->ContainerPtrToValuePtr<void>(Address),Pair.Value,Error,Depth+1)) { if (Error.IsEmpty()) Error=TEXT("Unknown animation struct field"); return false; }
        }
        return true;
    }
    if (!FJsonObjectConverter::JsonValueToUProperty(Value,Property,Address,0,0)) { Error=TEXT("Invalid typed animation parameter"); return false; } return true;
}
static TSharedPtr<FJsonValue> NexusAnimationExportProperty(FProperty* Property,const void* Address)
{
    if (auto* Object=CastField<FObjectPropertyBase>(Property)) { UObject* Value=Object->GetObjectPropertyValue(Address); if (Value) return MakeShared<FJsonValueObject>(FNexusNativeEditorTools::Reference(Value->GetPathName())); return MakeShared<FJsonValueNull>(); }
    if (auto* Array=CastField<FArrayProperty>(Property)) { TArray<TSharedPtr<FJsonValue>> Values; FScriptArrayHelper Helper(Array,const_cast<void*>(Address)); for (int32 Element=0;Element<Helper.Num();++Element) Values.Add(NexusAnimationExportProperty(Array->Inner,Helper.GetRawPtr(Element))); return MakeShared<FJsonValueArray>(Values); }
    return FJsonObjectConverter::UPropertyToJsonValue(Property,Address,0,0);
}
static bool NexusAnimationInvoke(UObject* Target,const FString& FunctionName,const TSharedPtr<FJsonObject>& Parameters,TSharedPtr<FJsonObject>& Result,FString& Error)
{
    UFunction* Function=Target?Target->FindFunction(FName(*FunctionName)):nullptr;
    if (!Function) { Error=TEXT("This engine does not expose the required animation API: ")+FunctionName; return false; }
    FStructOnScope Storage(Function);
    for (const auto& Pair : Parameters->Values) {
        FProperty* Parameter=nullptr; const FString Key=NexusAnimationFieldKey(FString(*Pair.Key));
        for (TFieldIterator<FProperty> It(Function);It && It->HasAnyPropertyFlags(CPF_Parm);++It) if (!It->HasAnyPropertyFlags(CPF_ReturnParm) && NexusAnimationFieldKey(It->GetName())==Key) { Parameter=*It; break; }
        if (!Parameter || !NexusAnimationImportProperty(Parameter,Parameter->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()),Pair.Value,Error)) { if (Error.IsEmpty()) Error=TEXT("Unsupported animation API argument: ")+FString(*Pair.Key); return false; }
    }
    Target->ProcessEvent(Function,Storage.GetStructMemory());
    for (TFieldIterator<FProperty> It(Function);It && It->HasAnyPropertyFlags(CPF_Parm);++It) if (It->HasAnyPropertyFlags(CPF_ReturnParm)) {
        auto Value=NexusAnimationExportProperty(*It,It->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()));
        if (Value && Value->Type==EJson::Boolean && !Value->AsBool()) { Error=TEXT("Animation API reported failure"); return false; }
        if (Value && Value->Type==EJson::Object) Result=Value->AsObject(); else if (Value) Result->SetField(Value->Type==EJson::Array?TEXT("Items"):TEXT("Value"),Value);
    }
    return true;
}

bool FNexusAnimationAdvancedTools::ValidateFileIO(const FString& Tool,const TSharedPtr<FJsonObject>& Args,FString& Error)
{
    if (Tool!=TEXT("export_fbx") && Tool!=TEXT("import_fbx")) return true;
    const FString InputPath=FNexusArgs(Args).Str(TEXT("fbx_file_path")); const FString Filename=FPaths::ConvertRelativePathToFull(InputPath);
    if (InputPath.IsEmpty() || !Filename.EndsWith(TEXT(".fbx"),ESearchCase::IgnoreCase) || (Tool==TEXT("export_fbx")?IFileManager::Get().FileExists(*Filename):!IFileManager::Get().FileExists(*Filename))) { Error=TEXT("FBX requires a new export .fbx path or an existing import .fbx file"); return false; }
    return true;
}

bool FNexusAnimationAdvancedTools::Handles(const FString& Domain,const FString& Tool)
{
    return Domain.Contains(TEXT(".conditions.")) || Domain.Contains(TEXT(".outliner.")) || Domain.Contains(TEXT(".custom_bindings.")) || Domain.StartsWith(TEXT("sequencer_animmixer_toolset.")) || Tool==TEXT("export_fbx") || Tool==TEXT("import_fbx") || Tool==TEXT("get_sequence_folders") || Tool==TEXT("add_sequence_folder") || Tool==TEXT("remove_sequence_folder");
}
bool FNexusAnimationAdvancedTools::Call(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Result,FString& Error)
{
    const FNexusArgs Arguments(Args);
    if (Tool.EndsWith(TEXT("sequence_folder")) || Tool==TEXT("get_sequence_folders")) {
        ULevelSequence* Sequence=Cast<ULevelSequence>(FNexusLegacyToolsets::Resolve(Args,TEXT("sequence"))); UMovieScene* Scene=Sequence?Sequence->GetMovieScene():nullptr;
        if (!Scene) { Error=TEXT("Sequence is required"); return false; }
        if (Tool==TEXT("get_sequence_folders")) { TArray<TSharedPtr<FJsonValue>> Items; for (UMovieSceneFolder* Folder : Scene->GetRootFolders()) if (Folder) { auto Item=FNexusNativeEditorTools::Reference(Folder->GetPathName()); Item->SetStringField(TEXT("Name"),Folder->GetFolderName().ToString()); Items.Add(MakeShared<FJsonValueObject>(Item)); } Result->SetArrayField(TEXT("Items"),Items); return true; }
        const FName Name(*Arguments.Str(TEXT("folder_name"))); FText Reason;
        if (Name.IsNone() || !Name.IsValidObjectName(Reason)) { Error=TEXT("Valid folder_name is required"); return false; }
        UMovieSceneFolder* Existing=nullptr; for (UMovieSceneFolder* Folder : Scene->GetRootFolders()) if (Folder && Folder->GetFolderName()==Name) Existing=Folder;
        if (Tool==TEXT("add_sequence_folder")) { if (Existing) { Error=TEXT("Folder already exists"); return false; } Scene->Modify(); auto* Folder=NewObject<UMovieSceneFolder>(Scene,NAME_None,RF_Transactional); Folder->SetFolderName(Name);
#if NX_UE_HAS_MOVIESCENE_FOLDER_MUTATORS
            Scene->AddRootFolder(Folder);
#else
            Scene->GetRootFolders().Add(Folder);
#endif
 Result=FNexusNativeEditorTools::Reference(Folder->GetPathName()); }
        else { if (!Existing || Existing->GetChildFolders().Num() ||
#if NX_UE_HAS_MOVIE_SCENE_MASTER_TRACKS
            Existing->GetChildMasterTracks().Num()
#else
            Existing->GetChildTracks().Num()
#endif
 || Existing->GetChildObjectBindings().Num()) { Error=TEXT("Only existing empty folders can be removed"); return false; } Scene->Modify();
#if NX_UE_HAS_MOVIESCENE_FOLDER_MUTATORS
            Scene->RemoveRootFolder(Existing);
#else
            Scene->GetRootFolders().Remove(Existing);
#endif
 }
        Scene->MarkPackageDirty(); return true;
    }
    if (Domain.Contains(TEXT(".conditions."))) {
        UObject* Target=FNexusLegacyToolsets::Resolve(Args,Tool.Contains(TEXT("section"))?TEXT("section"):TEXT("track"));
        if (!Target || (!Target->IsA(UMovieSceneSection::StaticClass()) && !Target->IsA(UMovieSceneTrack::StaticClass()))) { Error=TEXT("Condition requires a valid section or track"); return false; }
        auto* Container=FindFProperty<FStructProperty>(Target->GetClass(),TEXT("ConditionContainer")); void* Data=Container?Container->ContainerPtrToValuePtr<void>(Target):nullptr;
        if (Tool.Contains(TEXT("track_row"))) {
            auto* Rows=FindFProperty<FMapProperty>(Target->GetClass(),TEXT("TrackRowMetadata"));
            const double Row=Arguments.Num(TEXT("row_index"),-1); if (!Rows || Row<0 || Row>10000 || Row!=static_cast<int32>(Row)) { Error=TEXT("Track row conditions are unavailable or row_index is invalid"); return false; }
            FScriptMapHelper Map(Rows,Rows->ContainerPtrToValuePtr<void>(Target)); const int32 RowIndex=static_cast<int32>(Row); int32 Found=Map.FindMapIndexWithKey(&RowIndex);
            if (Found==INDEX_NONE) { Error=TEXT("Track row metadata must exist before attaching a row condition"); return false; }
            auto* Metadata=CastField<FStructProperty>(Rows->ValueProp); Container=Metadata?FindFProperty<FStructProperty>(Metadata->Struct,TEXT("ConditionContainer")):nullptr; Data=Container?Container->ContainerPtrToValuePtr<void>(Map.GetValuePtr(Found)):nullptr;
        }
        auto* Condition=Container?FindFProperty<FObjectPropertyBase>(Container->Struct,TEXT("Condition")):nullptr;
        if (!Condition || !Data) { Error=TEXT("This engine does not support this MovieScene condition scope"); return false; }
        if (Tool.StartsWith(TEXT("get_"))) { UObject* Value=Condition->GetObjectPropertyValue_InContainer(Data); Result->SetStringField(TEXT("Value"),Value?Value->GetClass()->GetPathName():TEXT("")); return true; }
        UClass* Class=nullptr; if (Tool.StartsWith(TEXT("set_"))) { Class=FNexusAssetUtils::FindClassWithUPrefix(Arguments.Str(TEXT("condition_class"))); if (!Class || !Class->IsChildOf(Condition->PropertyClass) || Class->HasAnyClassFlags(CLASS_Abstract)) { Error=TEXT("condition_class must be a concrete MovieScene condition"); return false; } }
        Target->Modify(); Condition->SetObjectPropertyValue_InContainer(Data,Class?NewObject<UObject>(Target,Class,NAME_None,RF_Transactional):nullptr); Target->PostEditChange(); Target->MarkPackageDirty(); return true;
    }
    auto Plugin=IPluginManager::Get().FindPlugin(Domain.StartsWith(TEXT("sequencer_animmixer_toolset."))?TEXT("MovieSceneAnimMixer"):TEXT("SequencerScripting"));
    if (!Plugin.IsValid() || !Plugin->IsEnabled()) { Error=TEXT("Required Sequencer/Mixer framework plugin is not enabled"); return false; }
    if (Domain.StartsWith(TEXT("sequencer_animmixer_toolset."))) {
        FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("MovieSceneAnimMixer"));
        // 旧版 Mixer 与 5.8 Layer/Transition 对象模型不同，只调用真实存在的脚本扩展。
        static const TMap<FString,FString> Routes={
            {TEXT("get_mixer_layers"),TEXT("MovieSceneAnimMixerTrackExtensions.GetLayers")}, {TEXT("get_mixer_layer_count"),TEXT("MovieSceneAnimMixerTrackExtensions.GetLayerCount")},
            {TEXT("add_mixer_layer"),TEXT("MovieSceneAnimMixerTrackExtensions.AddLayer")}, {TEXT("insert_mixer_layer"),TEXT("MovieSceneAnimMixerTrackExtensions.InsertLayer")},
            {TEXT("get_layer_name"),TEXT("MovieSceneAnimMixerLayerExtensions.GetLayerName")}, {TEXT("set_layer_name"),TEXT("MovieSceneAnimMixerLayerExtensions.SetLayerName")},
            {TEXT("get_layer_sections"),TEXT("MovieSceneAnimMixerLayerExtensions.GetSections")}, {TEXT("get_layer_index"),TEXT("MovieSceneAnimMixerLayerExtensions.GetLayerIndex")}, {TEXT("is_layer_empty"),TEXT("MovieSceneAnimMixerLayerExtensions.IsEmpty")},
            {TEXT("get_decorations"),TEXT("MovieSceneDecorationContainerExtensions.GetDecorations")}, {TEXT("add_decoration"),TEXT("MovieSceneDecorationContainerExtensions.AddDecoration")}, {TEXT("remove_decoration"),TEXT("MovieSceneDecorationContainerExtensions.RemoveDecoration")}
        };
        const FString* Route=Routes.Find(Tool); FString ClassName,FunctionName; if (Route) Route->Split(TEXT("."),&ClassName,&FunctionName); UClass* Class=Route?FNexusAssetUtils::FindClassWithUPrefix(ClassName):nullptr;
        if (!Class) { Error=TEXT("This engine has the older Mixer framework without the requested Layer/Transition/Decoration scripting API"); return false; }
        return NexusAnimationInvoke(Class->GetDefaultObject(),FunctionName,Args,Result,Error);
    }
    FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("SequencerScriptingEditor"));
    if (Tool==TEXT("export_fbx") || Tool==TEXT("import_fbx")) {
        ULevelSequence* Sequence=Cast<ULevelSequence>(FNexusLegacyToolsets::Resolve(Args,TEXT("sequence"))); const FString Filename=FPaths::ConvertRelativePathToFull(Arguments.Str(TEXT("fbx_file_path")));
        const bool bExport=Tool==TEXT("export_fbx");
        if (!Sequence || !Filename.EndsWith(TEXT(".fbx"),ESearchCase::IgnoreCase) || (bExport?IFileManager::Get().FileExists(*Filename):!IFileManager::Get().FileExists(*Filename))) { Error=TEXT("FBX requires a valid sequence, .fbx path and a new export/existing import file"); return false; }
        UObject* World=FNexusLegacyToolsets::Resolve(Args,TEXT("world")); if (!World && GEditor) World=GEditor->GetEditorWorldContext().World();
        auto Data=MakeShared<FJsonObject>(); Data->SetObjectField(TEXT("World"),FNexusNativeEditorTools::Reference(World?World->GetPathName():TEXT(""))); Data->SetObjectField(TEXT("Sequence"),FNexusNativeEditorTools::Reference(Sequence->GetPathName()));
        const auto* Bindings=Args->Values.Find(TEXT("bindings")); Data->SetField(TEXT("Bindings"),Bindings?*Bindings:MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()));
        auto Parameters=MakeShared<FJsonObject>();
        if (bExport) { Data->SetObjectField(TEXT("RootSequence"),FNexusNativeEditorTools::Reference(Sequence->GetPathName())); Data->SetStringField(TEXT("FBXFileName"),Filename); if (Args->HasField(TEXT("override_options"))) Data->SetField(TEXT("OverrideOptions"),Args->Values[TEXT("override_options")]); Parameters->SetObjectField(TEXT("InParams"),Data); }
        else { Parameters->SetField(TEXT("InWorld"),Data->Values[TEXT("World")]); Parameters->SetField(TEXT("InSequence"),Data->Values[TEXT("Sequence")]); Parameters->SetField(TEXT("InBindings"),Data->Values[TEXT("Bindings")]); Parameters->SetStringField(TEXT("InImportFilename"),Filename); if (Args->HasField(TEXT("import_settings"))) Parameters->SetField(TEXT("InImportFBXSettings"),Args->Values[TEXT("import_settings")]); else { UClass* Settings=FNexusAssetUtils::FindClassWithUPrefix(TEXT("MovieSceneUserImportFBXSettings")); if (!Settings) { Error=TEXT("FBX import settings class unavailable"); return false; } auto* Object=NewObject<UObject>(GetTransientPackage(),Settings); Parameters->SetObjectField(TEXT("InImportFBXSettings"),FNexusNativeEditorTools::Reference(Object->GetPathName())); } }
        UClass* Library=FNexusAssetUtils::FindClassWithUPrefix(TEXT("SequencerToolsFunctionLibrary"));
        const FString ExportName=Library && Library->FindFunctionByName(TEXT("ExportLevelSequenceFBX"))?TEXT("ExportLevelSequenceFBX"):TEXT("ExportFBX");
        const FString ImportName=Library && Library->FindFunctionByName(TEXT("ImportLevelSequenceFBX"))?TEXT("ImportLevelSequenceFBX"):TEXT("ImportFBX");
        UFunction* ExportFunction=Library?Library->FindFunctionByName(FName(*ExportName)):nullptr;
        if (bExport && ExportFunction && !FindFProperty<FProperty>(ExportFunction,TEXT("InParams"))) {
            Parameters->Values.Empty(); Parameters->SetField(TEXT("InWorld"),Data->Values[TEXT("World")]); Parameters->SetField(TEXT("InSequence"),Data->Values[TEXT("Sequence")]); Parameters->SetField(TEXT("InBindings"),Data->Values[TEXT("Bindings")]); Parameters->SetStringField(TEXT("InFBXFileName"),Filename);
            if (Args->HasField(TEXT("override_options"))) Parameters->SetField(FindFProperty<FProperty>(ExportFunction,TEXT("InOverrideOptions"))?TEXT("InOverrideOptions"):TEXT("OverrideOptions"),Args->Values[TEXT("override_options")]);
        }
        if (!NexusAnimationInvoke(Library?Library->GetDefaultObject():nullptr,bExport?ExportName:ImportName,Parameters,Result,Error)) return false;
        if (bExport && !IFileManager::Get().FileExists(*Filename)) { Error=TEXT("FBX exporter did not create a file"); return false; } Sequence->MarkPackageDirty(); return true;
    }
    // Outliner 视图模型和自定义绑定的执行必须由该版本真实提供的编辑器脚本 API 完成。
    Error=TEXT("This editor version does not expose the requested Outliner/custom-binding scripting operation; sequence folders and standard bindings remain available"); return false;
}
