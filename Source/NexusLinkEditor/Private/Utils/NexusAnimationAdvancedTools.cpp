// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAnimationAdvancedTools.h"
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusEditorControlServices.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Editor.h"
#include "EditorSubsystem.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneFolder.h"
#include "Engine/Blueprint.h"
#include "Misc/Guid.h"
#if WITH_CONTROL_RIG
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyDefines.h"
#endif

static FString NexusAnimationFieldKey(FString Name)
{
    Name.ReplaceInline(TEXT("_"),TEXT(""));
    if (Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1])) Name.RightChopInline(1);
    if (Name.Len() > 2 && Name.StartsWith(TEXT("In")) && FChar::IsUpper(Name[2])) Name.RightChopInline(2);
    return Name.ToLower();
}

static FString NexusAnimationControlTypeName(const FString& Name)
{
    FString Result;
    for (int32 Index = 0; Index < Name.Len(); ++Index) {
        if (Index > 0 && FChar::IsUpper(Name[Index]) && (FChar::IsLower(Name[Index - 1]) || (Index + 1 < Name.Len() && FChar::IsLower(Name[Index + 1])))) Result += TEXT('_');
        Result += FChar::ToUpper(Name[Index]);
    }
    return Result;
}

#if WITH_CONTROL_RIG
static ERigControlType NexusAnimationControlType(const URigHierarchy* Hierarchy, const FRigControlElement* Control)
{
#if NX_UE_HAS_RIG_CONTROL_SETTINGS_TOPOLOGY
    return Hierarchy->GetControlSettingsTopology(Control).ControlType;
#else
    return Control->Settings.ControlType;
#endif
}
#endif

// Outliner 的结构包含不可反射的弱视图指针，只回传本进程生成的有界句柄，禁止凭空重建。
static TMap<FString, TSharedPtr<FStructOnScope>> GNexusAnimationViewModels;
static TArray<FString> GNexusAnimationViewModelOrder;
static const TMap<FString,FString>& NexusAnimationScriptRoutes()
{
    static const TMap<FString,FString> Routes = {
        {TEXT("NexusAnimation.Sequencer.get_current_sequence"),TEXT("LevelSequenceEditorBlueprintLibrary.GetCurrentLevelSequence")},
        {TEXT("NexusAnimation.Sequencer.get_focused_sequence"),TEXT("LevelSequenceEditorBlueprintLibrary.GetFocusedLevelSequence")},
        {TEXT("NexusAnimation.Sequencer.open_sequence"),TEXT("LevelSequenceEditorBlueprintLibrary.OpenLevelSequence")},
        {TEXT("NexusAnimation.Sequencer.close_sequence"),TEXT("LevelSequenceEditorBlueprintLibrary.CloseLevelSequence")},
        {TEXT("NexusAnimation.Sequencer.get_tracks_on_binding"),TEXT("MovieSceneBindingExtensions.GetTracks")},
        {TEXT("NexusAnimation.Sequencer.add_track_to_binding"),TEXT("MovieSceneBindingExtensions.AddTrack")},
        {TEXT("NexusAnimation.Sequencer.bake_transform"),TEXT("LevelSequenceEditorSubsystem.BakeTransformWithSettings")},
        {TEXT("NexusAnimation.Outliner.get_outliner_tree"),TEXT("SequencerOutlinerScriptingObject.GetRootNode")},
        {TEXT("NexusAnimation.Outliner.get_outliner_children"),TEXT("SequencerOutlinerScriptingObject.GetChildren")},
        {TEXT("NexusAnimation.Outliner.get_outliner_selection"),TEXT("SequencerOutlinerScriptingObject.GetSelection")},
        {TEXT("NexusAnimation.Outliner.set_outliner_selection"),TEXT("SequencerOutlinerScriptingObject.SetSelection")},
        {TEXT("NexusAnimation.Outliner.get_node_label"),TEXT("SequencerViewModelStructExtensions.GetLabel")},
        {TEXT("NexusAnimation.Outliner.set_node_expanded"),TEXT("SequencerOutlinerScriptingObject.SetExpansion")},
        {TEXT("NexusAnimation.Outliner.is_node_expanded"),TEXT("SequencerOutlinerScriptingObject.IsExpanded")},
        {TEXT("NexusAnimation.Outliner.set_node_muted"),TEXT("SequencerOutlinerScriptingObject.SetMuteNodes")},
        {TEXT("NexusAnimation.Outliner.get_muted_nodes"),TEXT("SequencerOutlinerScriptingObject.GetMuteNodes")},
        {TEXT("NexusAnimation.Outliner.set_node_solo"),TEXT("SequencerOutlinerScriptingObject.SetSoloNodes")},
        {TEXT("NexusAnimation.Outliner.get_soloed_nodes"),TEXT("SequencerOutlinerScriptingObject.GetSoloNodes")},
        {TEXT("NexusAnimation.Outliner.set_node_deactivated"),TEXT("SequencerOutlinerScriptingObject.SetDeactivatedNodes")},
        {TEXT("NexusAnimation.Outliner.get_deactivated_nodes"),TEXT("SequencerOutlinerScriptingObject.GetDeactivatedNodes")},
        {TEXT("NexusAnimation.Outliner.set_node_locked"),TEXT("SequencerOutlinerScriptingObject.SetLockedNodes")},
        {TEXT("NexusAnimation.Outliner.get_locked_nodes"),TEXT("SequencerOutlinerScriptingObject.GetLockedNodes")},
        {TEXT("NexusAnimation.Outliner.set_node_pinned"),TEXT("SequencerOutlinerScriptingObject.SetPinnedNodes")},
        {TEXT("NexusAnimation.Outliner.get_pinned_nodes"),TEXT("SequencerOutlinerScriptingObject.GetPinnedNodes")},
        {TEXT("NexusAnimation.Outliner.get_sections_for_nodes"),TEXT("SequencerModuleOutlinerScriptingObject.GetSections")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_spawnable"),TEXT("LevelSequenceEditorSubsystem.ConvertToSpawnable")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_possessable"),TEXT("LevelSequenceEditorSubsystem.ConvertToPossessable")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_custom_binding"),TEXT("LevelSequenceEditorSubsystem.ConvertToCustomBinding")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_binding_type"),TEXT("LevelSequenceEditorSubsystem.GetCustomBindingType")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_binding_objects"),TEXT("LevelSequenceEditorSubsystem.GetCustomBindingObjects")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_bindings_of_type"),TEXT("LevelSequenceEditorSubsystem.GetCustomBindingsOfType")},
        {TEXT("NexusAnimation.CustomBindings.change_actor_template_class"),TEXT("LevelSequenceEditorSubsystem.ChangeActorTemplateClass")},
        {TEXT("NexusAnimation.CustomBindings.save_default_spawnable_state"),TEXT("LevelSequenceEditorSubsystem.SaveDefaultSpawnableState")},
        {TEXT("NexusAnimation.ImportExport.export_anim_sequence"),TEXT("SequencerToolsFunctionLibrary.ExportAnimSequence")},
        {TEXT("NexusAnimation.ImportExport.link_anim_sequence"),TEXT("SequencerToolsFunctionLibrary.LinkAnimSequence")},
        {TEXT("NexusAnimation.ImportExport.get_linked_anim_sequences"),TEXT("SequencerToolsFunctionLibrary.GetAnimSequenceLinkFromLevelSequence")},
        {TEXT("NexusAnimation.ImportExport.get_linked_level_sequence"),TEXT("SequencerToolsFunctionLibrary.GetLevelSequenceLinkFromAnimSequence")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_control_rigs"),TEXT("ControlRigSequencerEditorLibrary.GetControlRigs")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_controls_info"),TEXT("ControlRig.GetHierarchy")},
        {TEXT("NexusAnimation.ControlRigSequencer.find_or_create_track"),TEXT("ControlRigSequencerEditorLibrary.FindOrCreateControlRigTrack")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_transform"),TEXT("ControlRigSequencerEditorLibrary.GetLocalControlRigTransform")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_transform"),TEXT("ControlRigSequencerEditorLibrary.SetLocalControlRigTransform")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_euler_transform"),TEXT("ControlRigSequencerEditorLibrary.SetLocalControlRigEulerTransform")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_world_transform"),TEXT("ControlRigSequencerEditorLibrary.SetControlRigWorldTransform")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_float"),TEXT("ControlRigSequencerEditorLibrary.GetLocalControlRigFloat")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_float"),TEXT("ControlRigSequencerEditorLibrary.SetLocalControlRigFloat")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_bool"),TEXT("ControlRigSequencerEditorLibrary.GetLocalControlRigBool")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_bool"),TEXT("ControlRigSequencerEditorLibrary.SetLocalControlRigBool")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_int"),TEXT("ControlRigSequencerEditorLibrary.SetLocalControlRigInt")},
        {TEXT("NexusAnimation.ControlRigSequencer.key_controls"),TEXT("ControlRigSequencerEditorLibrary.KeyControls")},
        {TEXT("NexusAnimation.ControlRigSequencer.key_controls_at_frames"),TEXT("ControlRigSequencerEditorLibrary.KeyControlsAtFrames")},
        {TEXT("NexusAnimation.ControlRigSequencer.bake_to_control_rig"),TEXT("ControlRigSequencerEditorLibrary.BakeToControlRig")},
        {TEXT("NexusAnimation.ControlRigSequencer.collapse_anim_layers"),TEXT("ControlRigSequencerEditorLibrary.CollapseControlRigAnimLayers")},
        {TEXT("NexusAnimation.ControlRigSequencer.bake_space"),TEXT("ControlRigSequencerEditorLibrary.BakeControlRigSpace")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_anim_layers"),TEXT("ControlRigSequencerEditorLibrary.GetAnimLayers")},
        {TEXT("NexusAnimation.ControlRigSequencer.add_layer_from_selection"),TEXT("ControlRigSequencerEditorLibrary.AddAnimLayerFromSelection")},
        {TEXT("NexusAnimation.ControlRigSequencer.delete_anim_layer"),TEXT("ControlRigSequencerEditorLibrary.DeleteAnimLayer")},
        {TEXT("NexusAnimation.ControlRigSequencer.duplicate_anim_layer"),TEXT("ControlRigSequencerEditorLibrary.DuplicateAnimLayer")},
        {TEXT("NexusAnimation.ControlRigSequencer.reorder_anim_layers"),TEXT("ControlRigSequencerEditorLibrary.ReorderAnimLayers")},
        {TEXT("NexusAnimation.ControlRigSequencer.merge_anim_layers"),TEXT("ControlRigSequencerEditorLibrary.MergeAnimLayers")},
        {TEXT("NexusAnimation.ControlRig.get_local_transform"),TEXT("RigHierarchy.GetLocalTransform")},
        {TEXT("NexusAnimation.ControlRig.get_global_transform"),TEXT("RigHierarchy.GetGlobalTransform")},
        {TEXT("NexusAnimation.ControlRig.set_local_transform"),TEXT("RigHierarchy.SetLocalTransform")},
        {TEXT("NexusAnimation.ControlRig.set_global_transform"),TEXT("RigHierarchy.SetGlobalTransform")}
    };
    return Routes;
}
static bool NexusAnimationScriptCall(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Result,FString& Error);

// 仅用于下方固定脚本 API 映射，不接受调用者提供类名或函数名。
static bool NexusAnimationImportProperty(FProperty* Property,void* Address,const TSharedPtr<FJsonValue>& Value,FString& Error,int32 Depth=0)
{
    if (!Value || Depth>8) { Error=TEXT("Invalid animation parameter nesting"); return false; }
    if (auto* ObjectProperty=CastField<FObjectPropertyBase>(Property)) {
        if (Value->Type==EJson::Null) { ObjectProperty->SetObjectPropertyValue(Address,nullptr); return true; }
        TSharedPtr<FJsonObject> ObjectArgs = MakeShared<FJsonObject>(); ObjectArgs->SetField(TEXT("object"),Value); UObject* Object=FNexusEditorDomainTools::Resolve(ObjectArgs,TEXT("object"));
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
        if (Struct->Struct == TBaseStructure<FTransform>::Get() && Value->Type == EJson::Object) {
            auto Fields = Value->AsObject();
            const auto ObjectField = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name) -> TSharedPtr<FJsonObject> { for (const auto& Pair : Object->Values) if (FString(*Pair.Key).Equals(Name, ESearchCase::IgnoreCase) && Pair.Value && Pair.Value->Type == EJson::Object) return Pair.Value->AsObject(); return nullptr; };
            const auto Number = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Default) { for (const auto& Pair : Object->Values) if (FString(*Pair.Key).Equals(Name, ESearchCase::IgnoreCase) && Pair.Value && Pair.Value->Type == EJson::Number) return Pair.Value->AsNumber(); return Default; };
            const auto Vector = [&](const TCHAR* Field, const FVector& Default) {
                auto Object = ObjectField(Fields, Field);
                if (!Object) return Default;
                return FVector(Number(Object,TEXT("x"),Default.X), Number(Object,TEXT("y"),Default.Y), Number(Object,TEXT("z"),Default.Z));
            };
            const FVector Translation = ObjectField(Fields,TEXT("location")) ? Vector(TEXT("location"), FVector::ZeroVector) : Vector(TEXT("translation"), FVector::ZeroVector);
            const FVector Scale = ObjectField(Fields,TEXT("scale")) ? Vector(TEXT("scale"), FVector::OneVector) : Vector(TEXT("scale3d"), FVector::OneVector);
            FQuat Rotation = FQuat::Identity; auto RotationFields = ObjectField(Fields,TEXT("rotation"));
            if (RotationFields) {
                if (RotationFields->HasField(TEXT("w")) || RotationFields->HasField(TEXT("W"))) Rotation = FQuat(Number(RotationFields,TEXT("x"),0), Number(RotationFields,TEXT("y"),0), Number(RotationFields,TEXT("z"),0), Number(RotationFields,TEXT("w"),1));
                else Rotation = FRotator(Number(RotationFields,TEXT("pitch"),0), Number(RotationFields,TEXT("yaw"),0), Number(RotationFields,TEXT("roll"),0)).Quaternion();
            }
            if (Translation.ContainsNaN() || Scale.ContainsNaN() || Rotation.ContainsNaN() || Rotation.SizeSquared() < SMALL_NUMBER) { Error = TEXT("Transform must have finite components and a nonzero rotation"); return false; }
            Rotation.Normalize(); *static_cast<FTransform*>(Address) = FTransform(Rotation, Translation, Scale); return true;
        }
        if (Struct->Struct->GetName() == TEXT("FrameNumber") && Value->Type == EJson::Number) {
            const double Frame = Value->AsNumber();
            if (!FMath::IsFinite(Frame) || Frame < MIN_int32 || Frame > MAX_int32 || FMath::FloorToDouble(Frame) != Frame) { Error = TEXT("FrameNumber requires an int32"); return false; }
            *static_cast<FFrameNumber*>(Address) = FFrameNumber(static_cast<int32>(Frame)); return true;
        }
        if (Value->Type == EJson::Object && Value->AsObject()->HasField(TEXT("StructRef"))) {
            const auto* Stored = GNexusAnimationViewModels.Find(Value->AsObject()->GetStringField(TEXT("StructRef")));
            if (!Stored || (*Stored)->GetStruct() != Struct->Struct) { Error = TEXT("Stale or incompatible animation StructRef"); return false; }
            Struct->Struct->CopyScriptStruct(Address, (*Stored)->GetStructMemory()); return true;
        }
        if (Struct->Struct==TBaseStructure<FGuid>::Get() && Value->Type==EJson::String) { FGuid Guid; if (!FGuid::Parse(Value->AsString(),Guid)) { Error=TEXT("Invalid binding GUID"); return false; } *static_cast<FGuid*>(Address)=Guid; return true; }
        if (Value->Type!=EJson::Object) { Error=TEXT("Animation struct parameter requires an object"); return false; }
        if (Struct->Struct->GetName() == TEXT("MovieSceneBindingProxy") || Struct->Struct->GetName() == TEXT("SequencerBindingProxy")) {
            for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member) {
                const FString Key = NexusAnimationFieldKey(Member->GetName()); TSharedPtr<FJsonValue> Field;
                for (const auto& Pair : Value->AsObject()->Values) if (NexusAnimationFieldKey(FString(*Pair.Key)) == Key || (Key == TEXT("bindingid") && NexusAnimationFieldKey(FString(*Pair.Key)) == TEXT("id"))) { Field = Pair.Value; break; }
                if (!Field || !NexusAnimationImportProperty(*Member, Member->ContainerPtrToValuePtr<void>(Address), Field, Error, Depth + 1)) { if (Error.IsEmpty()) Error = TEXT("Binding requires BindingId and Sequence from Bindings inspection"); return false; }
            }
            return true;
        }
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
    if (auto* Object=CastField<FObjectPropertyBase>(Property)) { UObject* Value=Object->GetObjectPropertyValue(Address); if (Value) return MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Value->GetPathName())); return MakeShared<FJsonValueNull>(); }
    if (auto* Array=CastField<FArrayProperty>(Property)) { TArray<TSharedPtr<FJsonValue>> Values; FScriptArrayHelper Helper(Array,const_cast<void*>(Address)); for (int32 Element=0;Element<Helper.Num();++Element) Values.Add(NexusAnimationExportProperty(Array->Inner,Helper.GetRawPtr(Element))); return MakeShared<FJsonValueArray>(Values); }
    if (auto* Struct = CastField<FStructProperty>(Property)) {
        TSharedPtr<FJsonObject> Fields = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member) Fields->SetField(Member->GetName(), NexusAnimationExportProperty(*Member, Member->ContainerPtrToValuePtr<void>(Address)));
        if (Struct->Struct->GetName() == TEXT("SequencerViewModelScriptingStruct")) {
            const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            auto Stored = MakeShared<FStructOnScope>(Struct->Struct); Struct->Struct->CopyScriptStruct(Stored->GetStructMemory(), Address);
            GNexusAnimationViewModels.Add(Token, Stored); GNexusAnimationViewModelOrder.Add(Token);
            while (GNexusAnimationViewModelOrder.Num() > 4096) { GNexusAnimationViewModels.Remove(GNexusAnimationViewModelOrder[0]); GNexusAnimationViewModelOrder.RemoveAt(0); }
            Fields->SetStringField(TEXT("StructRef"), Token);
        }
        return MakeShared<FJsonValueObject>(Fields);
    }
    return FJsonObjectConverter::UPropertyToJsonValue(Property,Address,0,0);
}
static bool NexusAnimationInvoke(UObject* Target,const FString& FunctionName,const TSharedPtr<FJsonObject>& Parameters,TSharedPtr<FJsonObject>& Result,FString& Error)
{
    UFunction* Function=Target?Target->FindFunction(FName(*FunctionName)):nullptr;
    if (!Function) { Error=TEXT("This engine does not expose the required animation API: ")+FunctionName; return false; }
    FStructOnScope Storage(Function);
    for (TFieldIterator<FProperty> Parameter(Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter) {
        const FString Default = Function->GetMetaData(*FString::Printf(TEXT("CPP_Default_%s"), *Parameter->GetName()));
        if (!Default.IsEmpty()) {
#if NX_UE_HAS_IMPORT_TEXT_DIRECT
            Parameter->ImportText_Direct(*Default, Parameter->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()), Target, PPF_None);
#else
            Parameter->ImportText(*Default, Parameter->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()), PPF_None, Target);
#endif
        }
    }
    for (const auto& Pair : Parameters->Values) {
        FProperty* Parameter=nullptr; const FString Key=NexusAnimationFieldKey(FString(*Pair.Key));
        for (TFieldIterator<FProperty> It(Function);It && It->HasAnyPropertyFlags(CPF_Parm);++It) if (!It->HasAnyPropertyFlags(CPF_ReturnParm) && NexusAnimationFieldKey(It->GetName())==Key) { Parameter=*It; break; }
        if (!Parameter || !NexusAnimationImportProperty(Parameter,Parameter->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()),Pair.Value,Error)) { if (Error.IsEmpty()) Error=TEXT("Unsupported animation API argument: ")+FString(*Pair.Key); return false; }
    }
    Target->ProcessEvent(Function,Storage.GetStructMemory());
    for (TFieldIterator<FProperty> It(Function);It && It->HasAnyPropertyFlags(CPF_Parm);++It) if (It->HasAnyPropertyFlags(CPF_ReturnParm)) {
        auto Value=NexusAnimationExportProperty(*It,It->ContainerPtrToValuePtr<void>(Storage.GetStructMemory()));
        // 查询布尔值为 false 是有效结果；写 API 是否失败由各动作解释。
        if (Value && Value->Type==EJson::Object) Result=Value->AsObject(); else if (Value) Result->SetField(Value->Type==EJson::Array?TEXT("Items"):TEXT("Value"),Value);
    }
    return true;
}

static UClass* NexusAnimationScriptClass(const FString& ClassName)
{
    for (const TCHAR* Module : {TEXT("LevelSequenceEditor"), TEXT("SequencerScripting"), TEXT("SequencerScriptingEditor")}) FModuleManager::Get().LoadModulePtr<IModuleInterface>(Module);
    const auto RigPlugin = IPluginManager::Get().FindPlugin(TEXT("ControlRig"));
    if (ClassName.Contains(TEXT("Rig")) && RigPlugin.IsValid() && RigPlugin->IsEnabled()) FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("ControlRigEditor"));
    return FNexusAssetUtils::FindClassWithUPrefix(ClassName);
}

UFunction* FNexusAnimationAdvancedTools::FindFunction(const FString& Domain,const FString& Tool)
{
    if (Domain == TEXT("NexusAnimation.Mixer")) return FindMixerFunction(Tool);
    const auto* Route = NexusAnimationScriptRoutes().Find(Domain + TEXT(".") + Tool);
    FString ClassName, FunctionName;
    if (!Route || !Route->Split(TEXT("."), &ClassName, &FunctionName)) return nullptr;
    UClass* Class = NexusAnimationScriptClass(ClassName);
    return Class ? Class->FindFunctionByName(FName(*FunctionName)) : nullptr;
}

bool FNexusAnimationAdvancedTools::Supports(const FString& Domain,const FString& Tool)
{
    if (!InputSchema(Domain, Tool).IsValid()) return false;
    if (Domain == TEXT("NexusAnimation.Folders") || Domain == TEXT("NexusAnimation.Keyframing")) return true;
    if (Domain == TEXT("NexusAnimation.Conditions")) {
        UClass* Class = NexusAnimationScriptClass(Tool.Contains(TEXT("section")) ? TEXT("MovieSceneSection") : TEXT("MovieSceneTrack"));
        return Class && FindFProperty<FStructProperty>(Class, TEXT("ConditionContainer"));
    }
    if (Domain == TEXT("NexusAnimation.Sequencer") && !NexusAnimationScriptRoutes().Contains(Domain + TEXT(".") + Tool)) return true;
    if (Tool == TEXT("export_fbx") || Tool == TEXT("import_fbx")) {
        UClass* Class = NexusAnimationScriptClass(TEXT("SequencerToolsFunctionLibrary"));
        return Class && (Class->FindFunctionByName(Tool == TEXT("export_fbx") ? TEXT("ExportLevelSequenceFBX") : TEXT("ImportLevelSequenceFBX")) || Class->FindFunctionByName(Tool == TEXT("export_fbx") ? TEXT("ExportFBX") : TEXT("ImportFBX")));
    }
    return FindFunction(Domain, Tool) != nullptr;
}

static UObject* NexusAnimationObjectField(UObject* Target, const TCHAR* Name)
{
    auto* Property = Target ? FindFProperty<FObjectPropertyBase>(Target->GetClass(), Name) : nullptr;
    return Property ? Property->GetObjectPropertyValue_InContainer(Target) : nullptr;
}

static UObject* NexusAnimationOutliner(FString& Error)
{
    UClass* Class = NexusAnimationScriptClass(TEXT("LevelSequenceEditorSubsystem"));
    UObject* Subsystem = GEditor && Class ? GEditor->GetEditorSubsystemBase(Class) : nullptr;
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (!NexusAnimationInvoke(Subsystem, TEXT("GetScriptingLayer"), MakeShared<FJsonObject>(), Data, Error)) return nullptr;
    UObject* Layer = FNexusEditorDomainTools::Resolve(Data, TEXT("refPath"));
    if (!Layer) { TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>(); Wrapped->SetObjectField(TEXT("value"), Data); Layer = FNexusEditorDomainTools::Resolve(Wrapped, TEXT("value")); }
    UObject* Outliner = NexusAnimationObjectField(Layer, TEXT("Outliner"));
    if (!Outliner) Error = TEXT("Open a Level Sequence editor before querying its Outliner");
    return Outliner;
}

static UObject* NexusAnimationRig(const TSharedPtr<FJsonObject>& Args, FString& Error, UObject** OutTrack = nullptr)
{
    if (UObject* Rig = FNexusEditorDomainTools::Resolve(Args, TEXT("rig"))) return Rig;
    UClass* Library = NexusAnimationScriptClass(TEXT("ControlRigSequencerEditorLibrary"));
    TSharedPtr<FJsonObject> Input = MakeShared<FJsonObject>();
    const auto* Sequence = Args->Values.Find(TEXT("sequence"));
    if (!Sequence) { Error = TEXT("Control Rig query requires sequence"); return nullptr; }
    Input->SetField(TEXT("LevelSequence"), *Sequence);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (!NexusAnimationInvoke(Library ? Library->GetDefaultObject() : nullptr, TEXT("GetControlRigs"), Input, Data, Error)) return nullptr;
    const FString RigPath = FNexusArgs(Args).Str(TEXT("control_rig_asset_path"));
    TSharedPtr<FJsonObject> AssetArgs = MakeShared<FJsonObject>(); AssetArgs->SetStringField(TEXT("asset"), RigPath);
    UBlueprint* Blueprint = Cast<UBlueprint>(FNexusEditorDomainTools::Resolve(AssetArgs, TEXT("asset")));
    if (!Blueprint || !Blueprint->GeneratedClass) { Error = TEXT("control_rig_asset_path must identify a compiled Control Rig asset"); return nullptr; }
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    UObject* Found = nullptr;
    if (Data->TryGetArrayField(TEXT("Items"), Items)) for (const auto& Item : *Items) {
        if (!Item || Item->Type != EJson::Object) continue;
        TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
        for (const auto& Pair : Item->AsObject()->Values) if (NexusAnimationFieldKey(FString(*Pair.Key)) == TEXT("controlrig")) Ref->SetField(TEXT("rig"), Pair.Value);
        UObject* Rig = FNexusEditorDomainTools::Resolve(Ref, TEXT("rig"));
        if (Rig && Rig->IsA(Blueprint->GeneratedClass)) {
            if (Found && Found != Rig) { Error = TEXT("Several matching Control Rigs exist; supply a rig reference from ControlRigs"); return nullptr; }
            Found = Rig;
            if (OutTrack) for (const auto& Pair : Item->AsObject()->Values) if (NexusAnimationFieldKey(FString(*Pair.Key)) == TEXT("track")) { TSharedPtr<FJsonObject> TrackArgs = MakeShared<FJsonObject>(); TrackArgs->SetField(TEXT("track"), Pair.Value); *OutTrack = FNexusEditorDomainTools::Resolve(TrackArgs, TEXT("track")); }
        }
    }
    if (!Found) Error = TEXT("No matching Control Rig is bound to this sequence");
    return Found;
}

static FString NexusAnimationAlias(const FString& Name)
{
    const FString Key = NexusAnimationFieldKey(Name);
    static const TMap<FString,FString> Aliases = {
        {TEXT("levelsequence"),TEXT("sequence")}, {TEXT("objectbinding"),TEXT("binding")},
        {TEXT("objectbindings"),TEXT("bindings")}, {TEXT("viewmodel"),TEXT("node")},
        {TEXT("selection"),TEXT("nodes")}, {TEXT("inmuted"),TEXT("muted")},
        {TEXT("insoloed"),TEXT("soloed")}, {TEXT("indeactivated"),TEXT("deactivated")},
        {TEXT("inlocked"),TEXT("locked")}, {TEXT("inpinned"),TEXT("pinned")},
        {TEXT("bindingtype"),TEXT("bindingtypeclass")}, {TEXT("custombindingtype"),TEXT("bindingtypeclass")},
        {TEXT("insettings"),TEXT("settings")}, {TEXT("keyreduce"),TEXT("reducekeys")},
        {TEXT("moviescenesection"),TEXT("section")}, {TEXT("key"),TEXT("item")},
        {TEXT("islayeredcontrolrig"),TEXT("islayered")}, {TEXT("toreducekeys"),TEXT("reducekeys")},
        {TEXT("index1"),TEXT("oldindex")}, {TEXT("index2"),TEXT("newindex")}
    };
    const auto* Alias = Aliases.Find(Key); return Alias ? *Alias : Key;
}

static TSharedPtr<FJsonValue> NexusAnimationArgument(const TSharedPtr<FJsonObject>& Args, const FString& Name)
{
    const FString Key = NexusAnimationAlias(Name);
    for (const auto& Pair : Args->Values) if (NexusAnimationAlias(FString(*Pair.Key)) == Key) return Pair.Value;
    return nullptr;
}

static bool NexusAnimationScriptCall(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Result,FString& Error)
{
    UFunction* Function = FNexusAnimationAdvancedTools::FindFunction(Domain, Tool);
    if (!Function) { Error = TEXT("Required engine animation scripting API is not available: ") + Domain + TEXT(".") + Tool; return false; }
    UObject* Target = Function->GetOuterUClass()->GetDefaultObject();
    UObject* Rig = nullptr;
    UObject* RigTrack = nullptr;
    if (Domain == TEXT("NexusAnimation.Outliner") && Function->GetOuterUClass()->GetName() != TEXT("SequencerViewModelStructExtensions")) Target = NexusAnimationOutliner(Error);
    else if (Function->GetOuterUClass()->GetName() == TEXT("LevelSequenceEditorSubsystem")) Target = GEditor ? GEditor->GetEditorSubsystemBase(Function->GetOuterUClass()) : nullptr;
    else if (Domain == TEXT("NexusAnimation.ControlRig")) {
        UObject* Blueprint = FNexusEditorDomainTools::Resolve(Args, TEXT("control_rig"));
        Target = NexusAnimationObjectField(Blueprint, TEXT("Hierarchy"));
    }
    else if (Domain == TEXT("NexusAnimation.ControlRigSequencer") && Tool != TEXT("get_control_rigs") && Tool != TEXT("find_or_create_track") && Tool != TEXT("bake_to_control_rig") && Tool != TEXT("get_anim_layers") && Tool != TEXT("add_layer_from_selection") && Tool != TEXT("delete_anim_layer") && Tool != TEXT("duplicate_anim_layer") && Tool != TEXT("reorder_anim_layers") && Tool != TEXT("merge_anim_layers") && Tool != TEXT("key_controls") && Tool != TEXT("key_controls_at_frames") && Tool != TEXT("collapse_anim_layers")) {
        Rig = NexusAnimationRig(Args, Error, &RigTrack); if (!Rig) return false;
        if (Tool == TEXT("get_controls_info")) {
#if WITH_CONTROL_RIG
            if (UControlRig* ControlRig = Cast<UControlRig>(Rig)) {
                URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
                if (!Hierarchy) { Error = TEXT("Control Rig hierarchy is unavailable"); return false; }
                TArray<TSharedPtr<FJsonValue>> Controls;
                for (const auto& Key : Hierarchy->GetControlKeys()) if (const auto* Control = Hierarchy->Find<FRigControlElement>(Key)) {
                    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"), Key.Name.ToString());
                    Item->SetStringField(TEXT("type"), NexusAnimationControlTypeName(StaticEnum<ERigControlType>()->GetNameStringByValue(static_cast<int64>(NexusAnimationControlType(Hierarchy, Control)))));
                    Controls.Add(MakeShared<FJsonValueObject>(Item));
                }
                Result->SetArrayField(TEXT("Items"), Controls); return true;
            }
#endif
            UObject* Hierarchy = NexusAnimationObjectField(Rig, TEXT("DynamicHierarchy"));
            if (!Hierarchy) Hierarchy = NexusAnimationObjectField(Rig, TEXT("Hierarchy"));
            return NexusAnimationInvoke(Hierarchy, TEXT("GetControlKeys"), MakeShared<FJsonObject>(), Result, Error);
        }
    }
#if WITH_CONTROL_RIG
    if (Rig && Args->HasTypedField<EJson::String>(TEXT("control_name"))) {
        UControlRig* ControlRig = Cast<UControlRig>(Rig); URigHierarchy* Hierarchy = ControlRig ? ControlRig->GetHierarchy() : nullptr;
        const FRigElementKey Key(FName(*FNexusArgs(Args).Str(TEXT("control_name"))), ERigElementType::Control);
        const auto* Control = Hierarchy ? Hierarchy->Find<FRigControlElement>(Key) : nullptr;
        if (!Control) { Error = TEXT("Named control was not found in the bound Control Rig"); return false; }
        const ERigControlType ControlType = NexusAnimationControlType(Hierarchy, Control);
        if ((Tool == TEXT("get_transform") || Tool == TEXT("set_transform")) && ControlType == ERigControlType::EulerTransform) {
            const FName Name(Tool == TEXT("get_transform") ? TEXT("GetLocalControlRigEulerTransform") : TEXT("SetLocalControlRigEulerTransform"));
            Function = Function->GetOuterUClass()->FindFunctionByName(Name);
            if (!Function) { Error = TEXT("Euler transform control scripting is unavailable"); return false; }
        }
        else if ((Tool == TEXT("get_transform") || Tool == TEXT("set_transform")) && ControlType != ERigControlType::Transform) { Error = TEXT("Named control is not a transform control"); return false; }
        if ((Tool == TEXT("get_bool") || Tool == TEXT("set_bool")) && ControlType != ERigControlType::Bool) { Error = TEXT("Named control is not a Bool control"); return false; }
        if (Tool == TEXT("set_int") && ControlType != ERigControlType::Integer) { Error = TEXT("Named control is not an Integer control"); return false; }
        if (Tool == TEXT("get_float") || Tool == TEXT("set_float")) {
            const FString TypeName = StaticEnum<ERigControlType>()->GetNameStringByValue(static_cast<int64>(ControlType));
            if (TypeName != TEXT("Float") && TypeName != TEXT("ScaleFloat")) { Error = TEXT("Named control is not a Float control"); return false; }
        }
        if (Tool == TEXT("set_euler_transform") && ControlType != ERigControlType::EulerTransform) { Error = TEXT("Named control is not an Euler transform control"); return false; }
    }
#endif
    if (Tool == TEXT("collapse_anim_layers")) { Rig = NexusAnimationRig(Args, Error, &RigTrack); if (!Rig || !RigTrack) return false; }
    if (!Target) { if (Error.IsEmpty()) Error = TEXT("Required animation editor target is unavailable"); return false; }
    if (Domain == TEXT("NexusAnimation.Outliner") && Tool == TEXT("get_outliner_tree")) {
        TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
        if (!NexusAnimationInvoke(Target, TEXT("GetRootNode"), MakeShared<FJsonObject>(), Root, Error)) return false;
        UClass* Labels = NexusAnimationScriptClass(TEXT("SequencerViewModelStructExtensions"));
        int32 Visited = 0; bool bTruncated = false;
        TFunction<bool(const TSharedPtr<FJsonObject>&,int32,TArray<TSharedPtr<FJsonValue>>&)> Children;
        Children = [&](const TSharedPtr<FJsonObject>& Node, int32 Depth, TArray<TSharedPtr<FJsonValue>>& Items) {
            if (Depth >= 32 || Visited >= 2048) { bTruncated = true; return true; }
            TSharedPtr<FJsonObject> Input = MakeShared<FJsonObject>(); Input->SetObjectField(TEXT("Node"), Node);
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            if (!NexusAnimationInvoke(Target, TEXT("GetChildren"), Input, Data, Error)) return false;
            const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
            if (!Data->TryGetArrayField(TEXT("Items"), Nodes)) return true;
            for (const auto& Child : *Nodes) {
                if (!Child || Child->Type != EJson::Object) continue;
                if (++Visited > 2048) { bTruncated = true; break; }
                TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>(); Item->SetObjectField(TEXT("node"), Child->AsObject());
                TSharedPtr<FJsonObject> LabelInput = MakeShared<FJsonObject>(); LabelInput->SetObjectField(TEXT("ViewModel"), Child->AsObject());
                TSharedPtr<FJsonObject> LabelResult = MakeShared<FJsonObject>();
                if (!NexusAnimationInvoke(Labels ? Labels->GetDefaultObject() : nullptr, TEXT("GetLabel"), LabelInput, LabelResult, Error)) return false;
                if (const auto* Label = LabelResult->Values.Find(TEXT("Value"))) Item->SetField(TEXT("label"), *Label);
                TArray<TSharedPtr<FJsonValue>> Descendants;
                if (!Children(Child->AsObject(), Depth + 1, Descendants)) return false;
                Item->SetArrayField(TEXT("children"), Descendants); Items.Add(MakeShared<FJsonValueObject>(Item));
            }
            return true;
        };
        TArray<TSharedPtr<FJsonValue>> Items;
        if (!Children(Root, 0, Items)) return false;
        Result->SetArrayField(TEXT("Items"), Items); Result->SetBoolField(TEXT("bTruncated"), bTruncated); return true;
    }
    TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> Parameter(Function); Parameter && Parameter->HasAnyPropertyFlags(CPF_Parm); ++Parameter) {
        if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm) || (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm | CPF_ReferenceParm))) continue;
        const FString Key = NexusAnimationAlias(Parameter->GetName());
        TSharedPtr<FJsonValue> Value = NexusAnimationArgument(Args, Parameter->GetName());
        if (Tool == TEXT("get_anim_layers") && Key == TEXT("addifdoesnotexist")) Value = MakeShared<FJsonValueBoolean>(false);
        if (Key == TEXT("controlrig") && Rig) Value = MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Rig->GetPathName()));
        if (Key == TEXT("track") && !Value && RigTrack) Value = MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(RigTrack->GetPathName()));
        if (Key == TEXT("world") && !Value && GEditor && GEditor->GetEditorWorldContext().World()) Value = MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(GEditor->GetEditorWorldContext().World()->GetPathName()));
        if (Key == TEXT("controlrigclass")) {
            TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>(); Ref->SetStringField(TEXT("asset"), FNexusArgs(Args).Str(TEXT("control_rig_asset_path")));
            UBlueprint* Blueprint = Cast<UBlueprint>(FNexusEditorDomainTools::Resolve(Ref, TEXT("asset")));
            if (!Blueprint || !Blueprint->GeneratedClass) { Error = TEXT("control_rig_asset_path must identify a compiled Control Rig asset"); return false; }
            Value = MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Blueprint->GeneratedClass->GetPathName()));
        }
        if (auto* ObjectProperty = CastField<FObjectPropertyBase>(*Parameter)) {
            if (ObjectProperty->PropertyClass->IsChildOf(UClass::StaticClass()) && Value && Value->Type == EJson::String) {
                UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(Value->AsString());
                if (!Class) { Error = TEXT("Animation class could not be resolved"); return false; }
                Value = MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Class->GetPathName()));
            }
            if (!Value && (Key == TEXT("exportoptions") || Key == TEXT("exportoption"))) {
                UObject* Options = NewObject<UObject>(GetTransientPackage(), ObjectProperty->PropertyClass);
                Value = MakeShared<FJsonValueObject>(FNexusEditorDomainTools::Reference(Options->GetPathName()));
            }
            if (!Value && !Function->HasMetaData(*FString::Printf(TEXT("CPP_Default_%s"), *Parameter->GetName()))) { Error = TEXT("Missing animation object argument: ") + Parameter->GetName(); return false; }
        }
        if (!Value && Key == TEXT("settings")) {
            TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
            for (const TCHAR* Name : {TEXT("start_frame"), TEXT("end_frame"), TEXT("reduce_keys"), TEXT("tolerance")}) if (const auto* Field = Args->Values.Find(Name)) Settings->SetField(Name, *Field);
            Value = MakeShared<FJsonValueObject>(Settings);
        }
        if (!Value && (Key == TEXT("value") || Key == TEXT("worldtransform")) && (Tool == TEXT("set_transform") || Tool == TEXT("set_euler_transform") || Tool == TEXT("set_world_transform"))) {
            const FNexusArgs Values(Args); TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
            TSharedPtr<FJsonObject> Location = MakeShared<FJsonObject>(); TSharedPtr<FJsonObject> Rotation = MakeShared<FJsonObject>(); TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
            for (const TCHAR* Axis : {TEXT("x"), TEXT("y"), TEXT("z")}) { Location->SetNumberField(Axis, Values.Num(*(TEXT("location_") + FString(Axis)))); Scale->SetNumberField(Axis, Values.Num(*(TEXT("scale_") + FString(Axis)), 1)); }
            for (const TCHAR* Axis : {TEXT("pitch"), TEXT("yaw"), TEXT("roll")}) Rotation->SetNumberField(Axis, Values.Num(*(TEXT("rotation_") + FString(Axis))));
            Transform->SetObjectField(TEXT("location"), Location); Transform->SetObjectField(TEXT("rotation"), Rotation); Transform->SetObjectField(TEXT("scale"), Scale); Value = MakeShared<FJsonValueObject>(Transform);
        }
        if (Domain == TEXT("NexusAnimation.Mixer") && Tool == TEXT("add_child_track_to_layer") && Key == TEXT("binding") && Value && Value->Type == EJson::Object) {
            for (const auto& Pair : Value->AsObject()->Values) if (NexusAnimationFieldKey(FString(*Pair.Key)) == TEXT("bindingid")) { Value = Pair.Value; break; }
        }
        if (Value) Parameters->SetField(Parameter->GetName(), Value);
    }
    const bool bWrite = !(Tool.StartsWith(TEXT("get_")) || Tool.StartsWith(TEXT("is_")));
    if (bWrite) {
        for (const TCHAR* Field : {TEXT("sequence"), TEXT("track"), TEXT("section"), TEXT("control_rig"), TEXT("layer"), TEXT("container")}) if (UObject* Object = FNexusEditorDomainTools::Resolve(Args, Field)) Object->Modify();
        if (Rig) Rig->Modify();
    }
    if (!NexusAnimationInvoke(Target, Function->GetName(), Parameters, Result, Error)) return false;
    if (Domain == TEXT("NexusAnimation.ControlRigSequencer") && Tool == TEXT("get_transform")) {
        const auto ObjectField = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name) -> TSharedPtr<FJsonObject> { for (const auto& Pair : Object->Values) if (FString(*Pair.Key).Equals(Name, ESearchCase::IgnoreCase) && Pair.Value && Pair.Value->Type == EJson::Object) return Pair.Value->AsObject(); return nullptr; };
        const auto Number = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, double Default) { for (const auto& Pair : Object->Values) if (FString(*Pair.Key).Equals(Name, ESearchCase::IgnoreCase) && Pair.Value && Pair.Value->Type == EJson::Number) return Pair.Value->AsNumber(); return Default; };
        auto LocationFields = ObjectField(Result, TEXT("Location")); if (!LocationFields) LocationFields = ObjectField(Result, TEXT("Translation"));
        auto RotationFields = ObjectField(Result, TEXT("Rotation"));
        if (!LocationFields || !RotationFields) { Error = TEXT("Animation API returned an invalid transform"); return false; }
        auto Data = MakeShared<FJsonObject>(); auto Location = MakeShared<FJsonObject>(); auto Rotation = MakeShared<FJsonObject>();
        for (const TCHAR* Axis : {TEXT("x"), TEXT("y"), TEXT("z")}) Location->SetNumberField(Axis, Number(LocationFields, Axis, 0));
        if (RotationFields->HasField(TEXT("W")) || RotationFields->HasField(TEXT("w"))) {
            const FRotator Angles = FQuat(Number(RotationFields,TEXT("x"),0), Number(RotationFields,TEXT("y"),0), Number(RotationFields,TEXT("z"),0), Number(RotationFields,TEXT("w"),1)).Rotator();
            Rotation->SetNumberField(TEXT("pitch"), Angles.Pitch); Rotation->SetNumberField(TEXT("yaw"), Angles.Yaw); Rotation->SetNumberField(TEXT("roll"), Angles.Roll);
        } else for (const TCHAR* Axis : {TEXT("pitch"), TEXT("yaw"), TEXT("roll")}) Rotation->SetNumberField(Axis, Number(RotationFields, Axis, 0));
        Data->SetObjectField(TEXT("location"), Location); Data->SetObjectField(TEXT("rotation"), Rotation); Result = Data;
    }
    if (Tool == TEXT("get_transition_info")) {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        if (const auto* Valid = Result->Values.Find(TEXT("Value"))) Data->SetField(TEXT("is_valid"), *Valid);
        for (const auto& Field : TMap<FString,FString>{{TEXT("name"),TEXT("GetTransitionDisplayName")},{TEXT("from_section"),TEXT("GetFromSection")},{TEXT("to_section"),TEXT("GetToSection")}}) {
            TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
            if (!NexusAnimationInvoke(Target, Field.Value, Parameters, Value, Error)) return false;
            if (const auto* Scalar = Value->Values.Find(TEXT("Value"))) Data->SetField(Field.Key, *Scalar);
            else Data->SetObjectField(Field.Key, Value);
        }
        Result = Data;
    }
    if (Tool == TEXT("get_linked_anim_sequences") || Tool == TEXT("get_linked_level_sequence")) {
        UObject* Link = FNexusEditorDomainTools::Resolve(Result, TEXT("refPath"));
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        const auto PathValue = [](FProperty* Property, const void* Address) {
            const auto* Struct = CastField<FStructProperty>(Property);
            return Struct && Struct->Struct->GetName() == TEXT("SoftObjectPath") ? static_cast<const FSoftObjectPath*>(Address)->ToString() : FString();
        };
        if (Tool == TEXT("get_linked_level_sequence")) {
            FProperty* Property = Link ? FindFProperty<FProperty>(Link->GetClass(), TEXT("PathToLevelSequence")) : nullptr;
            Data->SetStringField(TEXT("Value"), Property ? PathValue(Property, Property->ContainerPtrToValuePtr<void>(Link)) : FString());
        } else {
            TArray<TSharedPtr<FJsonValue>> Paths;
            auto* Array = Link ? FindFProperty<FArrayProperty>(Link->GetClass(), TEXT("AnimSequenceLinks")) : nullptr;
            auto* Entry = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
            FProperty* Path = Entry ? FindFProperty<FProperty>(Entry->Struct, TEXT("PathToAnimSequence")) : nullptr;
            if (Array && Path) { FScriptArrayHelper Entries(Array, Array->ContainerPtrToValuePtr<void>(Link)); for (int32 Index = 0; Index < Entries.Num(); ++Index) Paths.Add(MakeShared<FJsonValueString>(PathValue(Path, Path->ContainerPtrToValuePtr<void>(Entries.GetRawPtr(Index))))); }
            Data->SetArrayField(TEXT("Items"), Paths);
        }
        Result = Data;
    }
    if (bWrite && Result->HasTypedField<EJson::Boolean>(TEXT("Value")) && !Result->GetBoolField(TEXT("Value"))) { Error = TEXT("Engine animation API reported failure"); return false; }
    if (bWrite && Result->HasTypedField<EJson::Null>(TEXT("Value"))) { Error = TEXT("Engine animation API did not produce the requested object"); return false; }
    if (bWrite) for (const TCHAR* Field : {TEXT("sequence"), TEXT("track"), TEXT("section"), TEXT("control_rig"), TEXT("layer"), TEXT("container")}) if (UObject* Object = FNexusEditorDomainTools::Resolve(Args, Field)) Object->MarkPackageDirty();
    return true;
}

bool FNexusAnimationAdvancedTools::ValidateFileIO(const FString& Tool,const TSharedPtr<FJsonObject>& Args,FString& Error)
{
    if (Tool!=TEXT("export_fbx") && Tool!=TEXT("import_fbx")) return true;
    const FString InputPath=FNexusArgs(Args).Str(TEXT("fbx_file_path")); const FString Filename=FPaths::ConvertRelativePathToFull(InputPath);
    if (InputPath.IsEmpty() || !Filename.EndsWith(TEXT(".fbx"),ESearchCase::IgnoreCase) || (Tool==TEXT("export_fbx")?IFileManager::Get().FileExists(*Filename):!IFileManager::Get().FileExists(*Filename))) { Error=TEXT("FBX requires a new export .fbx path or an existing import .fbx file"); return false; }
    return true;
}

UFunction* FNexusAnimationAdvancedTools::FindMixerFunction(const FString& Tool)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("MovieSceneAnimMixer"));
    if (!Plugin || !Plugin->IsEnabled()) return nullptr;
    FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("MovieSceneAnimMixer"));
    FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("MovieSceneAnimMixerScripting"));
    static const TMap<FString,FString> Routes={
        {TEXT("get_mixer_layers"),TEXT("MovieSceneAnimMixerTrackExtensions.GetLayers")}, {TEXT("get_mixer_layer_count"),TEXT("MovieSceneAnimMixerTrackExtensions.GetLayerCount")},
        {TEXT("add_mixer_layer"),TEXT("MovieSceneAnimMixerTrackExtensions.AddLayer")}, {TEXT("insert_mixer_layer"),TEXT("MovieSceneAnimMixerTrackExtensions.InsertLayer")},
        {TEXT("get_layer_name"),TEXT("MovieSceneAnimMixerLayerExtensions.GetDisplayName")}, {TEXT("set_layer_name"),TEXT("MovieSceneAnimMixerLayerExtensions.SetDisplayName")},
        {TEXT("get_layer_sections"),TEXT("MovieSceneAnimMixerLayerExtensions.GetSections")}, {TEXT("get_layer_index"),TEXT("MovieSceneAnimMixerLayerExtensions.GetLayerIndex")}, {TEXT("is_layer_empty"),TEXT("MovieSceneAnimMixerLayerExtensions.IsEmpty")},
        {TEXT("add_animation_to_mixer"),TEXT("MovieSceneAnimMixerTrackExtensions.AddAnimation")},
        {TEXT("add_child_track_to_layer"),TEXT("MovieSceneAnimMixerTrackExtensions.AddChildTrackToLayer")},
        {TEXT("get_transitions_for_section"),TEXT("MovieSceneAnimMixerTrackExtensions.GetTransitionsForSection")},
        {TEXT("get_transition_between"),TEXT("MovieSceneAnimMixerTrackExtensions.GetTransitionBetween")},
        {TEXT("get_transition_info"),TEXT("MovieSceneAnimMixerTransitionExtensions.IsTransitionValid")},
        {TEXT("get_transition_name"),TEXT("MovieSceneAnimMixerTransitionExtensions.GetTransitionDisplayName")},
        {TEXT("change_transition_type"),TEXT("MovieSceneAnimMixerTransitionExtensions.ChangeTransitionType")},
        {TEXT("get_compatible_decorations"),TEXT("MovieSceneDecorationContainerExtensions.GetCompatibleDecorations")},
        {TEXT("get_decorations"),TEXT("MovieSceneDecorationContainerExtensions.GetDecorations")},
        {TEXT("find_decoration"),TEXT("MovieSceneDecorationContainerExtensions.FindDecoration")},
        {TEXT("add_decoration"),TEXT("MovieSceneDecorationContainerExtensions.AddDecoration")},
        {TEXT("remove_decoration"),TEXT("MovieSceneDecorationContainerExtensions.RemoveDecoration")}
    };
    const FString* Route=Routes.Find(Tool); FString ClassName, FunctionName;
    if (!Route || !Route->Split(TEXT("."), &ClassName, &FunctionName)) return nullptr;
    UClass* Class = FNexusAssetUtils::FindClassWithUPrefix(ClassName);
    return Class ? Class->FindFunctionByName(FName(*FunctionName)) : nullptr;
}

bool FNexusAnimationAdvancedTools::Handles(const FString& Domain,const FString& Tool)
{
    return Domain == TEXT("NexusAnimation.Conditions") || Domain == TEXT("NexusAnimation.Mixer") || NexusAnimationScriptRoutes().Contains(Domain + TEXT(".") + Tool) || Tool==TEXT("export_fbx") || Tool==TEXT("import_fbx") || Tool==TEXT("get_sequence_folders") || Tool==TEXT("add_sequence_folder") || Tool==TEXT("remove_sequence_folder");
}
bool FNexusAnimationAdvancedTools::Call(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Result,FString& Error)
{
    const FNexusArgs Arguments(Args);
    if (Tool.EndsWith(TEXT("sequence_folder")) || Tool==TEXT("get_sequence_folders")) {
        ULevelSequence* Sequence=Cast<ULevelSequence>(FNexusEditorDomainTools::Resolve(Args,TEXT("sequence"))); UMovieScene* Scene=Sequence?Sequence->GetMovieScene():nullptr;
        if (!Scene) { Error=TEXT("Sequence is required"); return false; }
        if (Tool==TEXT("get_sequence_folders")) { TArray<TSharedPtr<FJsonValue>> Items; for (UMovieSceneFolder* Folder : Scene->GetRootFolders()) if (Folder) { auto Item=FNexusEditorDomainTools::Reference(Folder->GetPathName()); Item->SetStringField(TEXT("Name"),Folder->GetFolderName().ToString()); Items.Add(MakeShared<FJsonValueObject>(Item)); } Result->SetArrayField(TEXT("Items"),Items); return true; }
        const FName Name(*Arguments.Str(TEXT("folder_name"))); FText Reason;
        if (Name.IsNone() || !Name.IsValidObjectName(Reason)) { Error=TEXT("Valid folder_name is required"); return false; }
        UMovieSceneFolder* Existing=nullptr; for (UMovieSceneFolder* Folder : Scene->GetRootFolders()) if (Folder && Folder->GetFolderName()==Name) Existing=Folder;
        if (Tool==TEXT("add_sequence_folder")) { if (Existing) { Error=TEXT("Folder already exists"); return false; } Scene->Modify(); auto* Folder=NewObject<UMovieSceneFolder>(Scene,NAME_None,RF_Transactional); Folder->SetFolderName(Name);
#if NX_UE_HAS_MOVIESCENE_FOLDER_MUTATORS
            Scene->AddRootFolder(Folder);
#else
            Scene->GetRootFolders().Add(Folder);
#endif
 Result=FNexusEditorDomainTools::Reference(Folder->GetPathName()); }
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
    if (Domain == TEXT("NexusAnimation.Conditions")) {
        UObject* Target=FNexusEditorDomainTools::Resolve(Args,Tool.Contains(TEXT("section"))?TEXT("section"):TEXT("track"));
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
    if (NexusAnimationScriptRoutes().Contains(Domain + TEXT(".") + Tool)) return NexusAnimationScriptCall(Domain, Tool, Args, Result, Error);
    auto Plugin=IPluginManager::Get().FindPlugin(Domain == TEXT("NexusAnimation.Mixer")?TEXT("MovieSceneAnimMixer"):TEXT("SequencerScripting"));
    if (!Plugin.IsValid() || !Plugin->IsEnabled()) { Error=TEXT("Required Sequencer/Mixer framework plugin is not enabled"); return false; }
    if (Domain == TEXT("NexusAnimation.Mixer")) {
        FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("MovieSceneAnimMixer"));
        // 旧版 Mixer 与 5.8 Layer/Transition 对象模型不同，只调用真实存在的脚本扩展。
        UFunction* Function = FindMixerFunction(Tool);
        if (!Function) { Error=TEXT("This engine has the older Mixer framework without the requested Layer/Transition/Decoration scripting API"); return false; }
        return NexusAnimationScriptCall(Domain, Tool, Args, Result, Error);
    }
    FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("SequencerScriptingEditor"));
    if (Tool==TEXT("export_fbx") || Tool==TEXT("import_fbx")) {
        ULevelSequence* Sequence=Cast<ULevelSequence>(FNexusEditorDomainTools::Resolve(Args,TEXT("sequence"))); const FString Filename=FPaths::ConvertRelativePathToFull(Arguments.Str(TEXT("fbx_file_path")));
        const bool bExport=Tool==TEXT("export_fbx");
        if (!Sequence || !Filename.EndsWith(TEXT(".fbx"),ESearchCase::IgnoreCase) || (bExport?IFileManager::Get().FileExists(*Filename):!IFileManager::Get().FileExists(*Filename))) { Error=TEXT("FBX requires a valid sequence, .fbx path and a new export/existing import file"); return false; }
        UObject* World=FNexusEditorDomainTools::Resolve(Args,TEXT("world")); if (!World && GEditor) World=GEditor->GetEditorWorldContext().World();
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>(); Data->SetObjectField(TEXT("World"),FNexusEditorDomainTools::Reference(World?World->GetPathName():TEXT(""))); Data->SetObjectField(TEXT("Sequence"),FNexusEditorDomainTools::Reference(Sequence->GetPathName()));
        const auto* Bindings=Args->Values.Find(TEXT("bindings")); Data->SetField(TEXT("Bindings"),Bindings?*Bindings:MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()));
        TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
        if (bExport) { Data->SetObjectField(TEXT("RootSequence"),FNexusEditorDomainTools::Reference(Sequence->GetPathName())); Data->SetStringField(TEXT("FBXFileName"),Filename); if (Args->HasField(TEXT("override_options"))) Data->SetField(TEXT("OverrideOptions"),Args->Values[TEXT("override_options")]); Parameters->SetObjectField(TEXT("InParams"),Data); }
        else { Parameters->SetField(TEXT("InWorld"),Data->Values[TEXT("World")]); Parameters->SetField(TEXT("InSequence"),Data->Values[TEXT("Sequence")]); Parameters->SetField(TEXT("InBindings"),Data->Values[TEXT("Bindings")]); Parameters->SetStringField(TEXT("InImportFilename"),Filename); if (Args->HasField(TEXT("import_settings"))) Parameters->SetField(TEXT("InImportFBXSettings"),Args->Values[TEXT("import_settings")]); else { UClass* Settings=FNexusAssetUtils::FindClassWithUPrefix(TEXT("MovieSceneUserImportFBXSettings")); if (!Settings) { Error=TEXT("FBX import settings class unavailable"); return false; } auto* Object=NewObject<UObject>(GetTransientPackage(),Settings); Parameters->SetObjectField(TEXT("InImportFBXSettings"),FNexusEditorDomainTools::Reference(Object->GetPathName())); } }
        UClass* Library=FNexusAssetUtils::FindClassWithUPrefix(TEXT("SequencerToolsFunctionLibrary"));
        const FString ExportName=Library && Library->FindFunctionByName(TEXT("ExportLevelSequenceFBX"))?TEXT("ExportLevelSequenceFBX"):TEXT("ExportFBX");
        const FString ImportName=Library && Library->FindFunctionByName(TEXT("ImportLevelSequenceFBX"))?TEXT("ImportLevelSequenceFBX"):TEXT("ImportFBX");
        UFunction* ExportFunction=Library?Library->FindFunctionByName(FName(*ExportName)):nullptr;
        if (bExport && ExportFunction && !FindFProperty<FProperty>(ExportFunction,TEXT("InParams"))) {
            Parameters->Values.Empty(); Parameters->SetField(TEXT("InWorld"),Data->Values[TEXT("World")]); Parameters->SetField(TEXT("InSequence"),Data->Values[TEXT("Sequence")]); Parameters->SetField(TEXT("InBindings"),Data->Values[TEXT("Bindings")]); Parameters->SetStringField(TEXT("InFBXFileName"),Filename);
            if (Args->HasField(TEXT("override_options"))) Parameters->SetField(FindFProperty<FProperty>(ExportFunction,TEXT("InOverrideOptions"))?TEXT("InOverrideOptions"):TEXT("OverrideOptions"),Args->Values[TEXT("override_options")]);
        }
        if (!NexusAnimationInvoke(Library?Library->GetDefaultObject():nullptr,bExport?ExportName:ImportName,Parameters,Result,Error)) return false;
        if (Result->HasTypedField<EJson::Boolean>(TEXT("Value")) && !Result->GetBoolField(TEXT("Value"))) { Error = TEXT("FBX animation API reported failure"); return false; }
        if (bExport && !IFileManager::Get().FileExists(*Filename)) { Error=TEXT("FBX exporter did not create a file"); return false; } Sequence->MarkPackageDirty(); return true;
    }
    // Outliner 视图模型和自定义绑定的执行必须由该版本真实提供的编辑器脚本 API 完成。
    Error=TEXT("This editor version does not expose the requested Outliner/custom-binding scripting operation; sequence folders and standard bindings remain available"); return false;
}


TSharedPtr<FJsonObject> FNexusAnimationAdvancedTools::InputSchema(const FString& Domain,const FString& Tool)
{
    // 参数契约由 NexusLink 自己持有，不读取引擎 Toolset Registry。
    static const TMap<FString,FString> Contracts = {
        {TEXT("NexusAnimation.Conditions.get_section_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["section"]})schema")},
        {TEXT("NexusAnimation.Conditions.get_track_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Conditions.get_track_row_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"row_index":{"type":"integer"}},"required":["track","row_index"]})schema")},
        {TEXT("NexusAnimation.Outliner.get_outliner_tree"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_outliner_children"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"node":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"type_filter":{"anyOf":[{"type":"string"},{"type":"null"}],"default":null}},"required":["node"]})schema")},
        {TEXT("NexusAnimation.Outliner.get_outliner_selection"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_node_label"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"node":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["node"]})schema")},
        {TEXT("NexusAnimation.Outliner.is_node_expanded"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"node":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["node"]})schema")},
        {TEXT("NexusAnimation.Outliner.get_muted_nodes"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_soloed_nodes"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_deactivated_nodes"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_locked_nodes"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_pinned_nodes"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Outliner.get_sections_for_nodes"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128}},"required":["nodes"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_binding_type"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_binding_objects"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_bindings_of_type"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding_type_class":{"type":"string"}},"required":["binding_type_class"]})schema")},
        {TEXT("NexusAnimation.ImportExport.get_linked_anim_sequences"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence"]})schema")},
        {TEXT("NexusAnimation.ImportExport.get_linked_level_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"anim_sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["anim_sequence"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_mixer_layers"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_mixer_layer_count"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_layer_name"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"layer":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["layer"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_layer_sections"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"layer":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["layer"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_layer_index"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"layer":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["layer"]})schema")},
        {TEXT("NexusAnimation.Mixer.is_layer_empty"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"layer":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["layer"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_transitions_for_section"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track","section"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_transition_between"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"from_section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"to_section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track","from_section","to_section"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_transition_info"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"transition":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["transition"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_transition_name"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"transition":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["transition"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_compatible_decorations"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"container":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["container"]})schema")},
        {TEXT("NexusAnimation.Mixer.get_decorations"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"container":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["container"]})schema")},
        {TEXT("NexusAnimation.Mixer.find_decoration"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"container":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"decoration_class":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["container","decoration_class"]})schema")},
        {TEXT("NexusAnimation.Folders.get_sequence_folders"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"]}},"required":["sequence"]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_bindings"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence"]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_tracks_on_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence"]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_tracks_on_binding"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding"]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_current_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_focused_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_sections"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_section_properties"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["section"]})schema")},
        {TEXT("NexusAnimation.Sequencer.get_section_range"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["section"]})schema")},
        {TEXT("NexusAnimation.Keyframing.get_channel_names"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["section"]})schema")},
        {TEXT("NexusAnimation.Keyframing.get_keys"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"}},"required":["section","channel_name"]})schema")},
        {TEXT("NexusAnimation.Keyframing.get_default_value"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"}},"required":["section","channel_name"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_control_rigs"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_controls_info"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_float"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_bool"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_anim_layers"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.ControlRig.get_local_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"control_rig":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"item":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"initial":{"type":"boolean","default":false}},"required":["control_rig","item"]})schema")},
        {TEXT("NexusAnimation.ControlRig.get_global_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"control_rig":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"item":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"initial":{"type":"boolean","default":false}},"required":["control_rig","item"]})schema")},
        {TEXT("NexusAnimation.Conditions.set_section_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"condition_class":{"type":"string"}},"required":["section","condition_class"]})schema")},
        {TEXT("NexusAnimation.Conditions.clear_section_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["section"]})schema")},
        {TEXT("NexusAnimation.Conditions.set_track_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"condition_class":{"type":"string"}},"required":["track","condition_class"]})schema")},
        {TEXT("NexusAnimation.Conditions.clear_track_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Conditions.set_track_row_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"row_index":{"type":"integer"},"condition_class":{"type":"string"}},"required":["track","row_index","condition_class"]})schema")},
        {TEXT("NexusAnimation.Conditions.clear_track_row_condition"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"row_index":{"type":"integer"}},"required":["track","row_index"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_outliner_selection"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128}},"required":["nodes"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_node_expanded"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"expanded":{"type":"boolean"}},"required":["nodes","expanded"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_node_muted"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"muted":{"type":"boolean"}},"required":["nodes","muted"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_node_solo"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"soloed":{"type":"boolean"}},"required":["nodes","soloed"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_node_deactivated"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"deactivated":{"type":"boolean"}},"required":["nodes","deactivated"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_node_locked"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"locked":{"type":"boolean"}},"required":["nodes","locked"]})schema")},
        {TEXT("NexusAnimation.Outliner.set_node_pinned"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"nodes":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"pinned":{"type":"boolean"}},"required":["nodes","pinned"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_spawnable"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_possessable"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_custom_binding"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"binding_type_class":{"type":"string"}},"required":["binding","binding_type_class"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.change_actor_template_class"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"actor_class":{"type":"string"}},"required":["binding","actor_class"]})schema")},
        {TEXT("NexusAnimation.CustomBindings.save_default_spawnable_state"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding"]})schema")},
        {TEXT("NexusAnimation.ImportExport.export_fbx"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"world":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"bindings":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"fbx_file_path":{"type":"string"},"override_options":{"anyOf":[{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},{"type":"null"}],"default":null}},"required":["world","sequence","bindings","fbx_file_path"]})schema")},
        {TEXT("NexusAnimation.ImportExport.import_fbx"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"world":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"bindings":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128},"import_settings":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"fbx_file_path":{"type":"string"}},"required":["world","sequence","bindings","import_settings","fbx_file_path"]})schema")},
        {TEXT("NexusAnimation.ImportExport.link_anim_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"anim_sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence","anim_sequence","binding"]})schema")},
        {TEXT("NexusAnimation.Mixer.add_mixer_layer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Mixer.insert_mixer_layer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"index":{"type":"integer"}},"required":["track","index"]})schema")},
        {TEXT("NexusAnimation.Mixer.add_animation_to_mixer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"layer_index":{"type":"integer"},"start_frame":{"type":"integer"},"anim_sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track","layer_index","start_frame","anim_sequence"]})schema")},
        {TEXT("NexusAnimation.Mixer.add_child_track_to_layer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"track_class":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"layer_index":{"type":"integer"}},"required":["track","binding","track_class","layer_index"]})schema")},
        {TEXT("NexusAnimation.Mixer.set_layer_name"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"layer":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"name":{"type":"string"}},"required":["layer","name"]})schema")},
        {TEXT("NexusAnimation.Mixer.change_transition_type"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"transition":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"new_transition_class":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["transition","new_transition_class"]})schema")},
        {TEXT("NexusAnimation.Mixer.add_decoration"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"container":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"decoration_class":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["container","decoration_class"]})schema")},
        {TEXT("NexusAnimation.Mixer.remove_decoration"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"container":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"decoration_class":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["container","decoration_class"]})schema")},
        {TEXT("NexusAnimation.Folders.add_sequence_folder"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"]},"folder_name":{"type":"string"}},"required":["sequence","folder_name"]})schema")},
        {TEXT("NexusAnimation.Folders.remove_sequence_folder"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"]},"folder_name":{"type":"string"}},"required":["sequence","folder_name"]})schema")},
        {TEXT("NexusAnimation.Sequencer.open_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence"]})schema")},
        {TEXT("NexusAnimation.Sequencer.close_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.Sequencer.add_track_to_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"track_type":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["sequence","track_type"]})schema")},
        {TEXT("NexusAnimation.Sequencer.add_track_to_binding"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"track_type":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["binding","track_type"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.find_or_create_track"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"is_layered":{"type":"boolean","default":false},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","binding","control_rig_asset_path"]})schema")},
        {TEXT("NexusAnimation.Sequencer.add_section"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track"]})schema")},
        {TEXT("NexusAnimation.Sequencer.remove_section"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"track":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."}},"required":["track","section"]})schema")},
        {TEXT("NexusAnimation.Sequencer.set_section_range"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["section","start_frame","end_frame"]})schema")},
        {TEXT("NexusAnimation.Sequencer.set_section_ease_in"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"duration":{"type":"integer"}},"required":["section","duration"]})schema")},
        {TEXT("NexusAnimation.Sequencer.set_section_ease_out"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"duration":{"type":"integer"}},"required":["section","duration"]})schema")},
        {TEXT("NexusAnimation.Sequencer.set_section_blend_type"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"blend_type":{"type":"string"}},"required":["section","blend_type"]})schema")},
        {TEXT("NexusAnimation.Sequencer.set_section_completion_mode"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"completion_mode":{"type":"string"}},"required":["section","completion_mode"]})schema")},
        {TEXT("NexusAnimation.Sequencer.set_section_animation"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"anim_sequence_path":{"type":"string"}},"required":["section","anim_sequence_path"]})schema")},
        {TEXT("NexusAnimation.Sequencer.bake_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"bindings":{"type":"array","items":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"maxItems":128}},"required":["bindings"]})schema")},
        {TEXT("NexusAnimation.Keyframing.add_key_float"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"number"},"interpolation":{"type":"string","default":""}},"required":["section","channel_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.Keyframing.add_key_bool"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"}},"required":["section","channel_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.Keyframing.add_key_integer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"}},"required":["section","channel_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.Keyframing.add_key_string"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"string"}},"required":["section","channel_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.Keyframing.remove_key_at_frame"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"frame":{"type":"integer"}},"required":["section","channel_name","frame"]})schema")},
        {TEXT("NexusAnimation.Keyframing.set_default_value"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"value":{"type":"number"}},"required":["section","channel_name","value"]})schema")},
        {TEXT("NexusAnimation.Keyframing.bake_channel_keys"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"channel_name":{"type":"string"},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"}},"required":["section","channel_name","start_frame","end_frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number","default":0.0},"location_y":{"type":"number","default":0.0},"location_z":{"type":"number","default":0.0},"rotation_pitch":{"type":"number","default":0.0},"rotation_yaw":{"type":"number","default":0.0},"rotation_roll":{"type":"number","default":0.0},"set_key":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_euler_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number","default":0.0},"location_y":{"type":"number","default":0.0},"location_z":{"type":"number","default":0.0},"rotation_pitch":{"type":"number","default":0.0},"rotation_yaw":{"type":"number","default":0.0},"rotation_roll":{"type":"number","default":0.0},"scale_x":{"type":"number","default":1.0},"scale_y":{"type":"number","default":1.0},"scale_z":{"type":"number","default":1.0},"set_key":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_world_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"location_x":{"type":"number","default":0.0},"location_y":{"type":"number","default":0.0},"location_z":{"type":"number","default":0.0},"rotation_pitch":{"type":"number","default":0.0},"rotation_yaw":{"type":"number","default":0.0},"rotation_roll":{"type":"number","default":0.0},"set_key":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_float"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"number"},"set_key":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_bool"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"boolean"},"set_key":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_int"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_name":{"type":"string"},"frame":{"type":"integer"},"value":{"type":"integer"},"set_key":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_name","frame","value"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.key_controls"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_names":{"type":"array","items":{"type":"string"},"maxItems":128}},"required":["section","control_names"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.key_controls_at_frames"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"section":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_names":{"type":"array","items":{"type":"string"},"maxItems":128},"frames":{"type":"array","items":{"type":"integer"},"maxItems":128}},"required":["section","control_names","frames"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.bake_to_control_rig"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"reduce_keys":{"type":"boolean","default":false},"tolerance":{"type":"number","default":0.001},"reset_controls":{"type":"boolean","default":true},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","binding","control_rig_asset_path"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.collapse_anim_layers"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"reduce_keys":{"type":"boolean","default":false},"tolerance":{"type":"number","default":0.001},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.bake_space"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"control_rig_asset_path":{"type":"string"},"control_names":{"type":"array","items":{"type":"string"},"maxItems":128},"start_frame":{"type":"integer"},"end_frame":{"type":"integer"},"reduce_keys":{"type":"boolean","default":false},"tolerance":{"type":"number","default":0.001},"rig":{"type":"object","properties":{"refPath":{"type":"string"}},"required":["refPath"],"description":"Optional explicit runtime rig reference when several matching rigs exist."}},"required":["sequence","control_rig_asset_path","control_names","start_frame","end_frame"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.add_layer_from_selection"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{},"required":[]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.delete_anim_layer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"index":{"type":"integer"}},"required":["index"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.duplicate_anim_layer"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"index":{"type":"integer"}},"required":["index"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.reorder_anim_layers"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"old_index":{"type":"integer"},"new_index":{"type":"integer"}},"required":["old_index","new_index"]})schema")},
        {TEXT("NexusAnimation.ControlRigSequencer.merge_anim_layers"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"indices":{"type":"array","items":{"type":"integer"},"maxItems":128}},"required":["indices"]})schema")},
        {TEXT("NexusAnimation.ControlRig.set_local_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"control_rig":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"item":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"transform":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"initial":{"type":"boolean","default":false}},"required":["control_rig","item","transform"]})schema")},
        {TEXT("NexusAnimation.ControlRig.set_global_transform"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"control_rig":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"item":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"transform":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"initial":{"type":"boolean","default":false}},"required":["control_rig","item","transform"]})schema")},
        {TEXT("NexusAnimation.ImportExport.export_anim_sequence"), TEXT(R"schema({"type":"object","additionalProperties":false,"properties":{"world":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"anim_sequence":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"binding":{"type":"object","description":"Use a returned refPath object for UObject/class, a returned StructRef for Outliner nodes, or reflected struct fields."},"create_link":{"type":"boolean","default":false}},"required":["world","sequence","anim_sequence","binding"]})schema")},
    };
    const FString* Json = Contracts.Find(Domain + TEXT(".") + Tool);
    TSharedPtr<FJsonObject> Schema;
    if (Json) FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(*Json), Schema);
    return Schema;
}


FString FNexusAnimationAdvancedTools::MinimumVersion(const FString& Domain,const FString& Tool)
{
    if (Domain == TEXT("NexusAnimation.Mixer")) return TEXT("5.8");
    if (Domain == TEXT("NexusAnimation.Conditions")) return TEXT("5.5");
    static const TMap<FString,FString> Versions = {
        {TEXT("NexusAnimation.ControlRig.get_global_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRig.get_local_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRig.set_global_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRig.set_local_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.add_layer_from_selection"),TEXT("5.5")},
        {TEXT("NexusAnimation.ControlRigSequencer.bake_space"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.bake_to_control_rig"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.collapse_anim_layers"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.delete_anim_layer"),TEXT("5.5")},
        {TEXT("NexusAnimation.ControlRigSequencer.duplicate_anim_layer"),TEXT("5.5")},
        {TEXT("NexusAnimation.ControlRigSequencer.find_or_create_track"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_anim_layers"),TEXT("5.5")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_bool"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_control_rigs"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_controls_info"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_float"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.get_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.key_controls"),TEXT("5.8")},
        {TEXT("NexusAnimation.ControlRigSequencer.key_controls_at_frames"),TEXT("5.8")},
        {TEXT("NexusAnimation.ControlRigSequencer.merge_anim_layers"),TEXT("5.5")},
        {TEXT("NexusAnimation.ControlRigSequencer.reorder_anim_layers"),TEXT("5.8")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_bool"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_euler_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_float"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_int"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.ControlRigSequencer.set_world_transform"),TEXT("5.0")},
        {TEXT("NexusAnimation.CustomBindings.change_actor_template_class"),TEXT("5.5")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_custom_binding"),TEXT("5.5")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_possessable"),TEXT("5.1")},
        {TEXT("NexusAnimation.CustomBindings.convert_to_spawnable"),TEXT("5.1")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_binding_objects"),TEXT("5.5")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_binding_type"),TEXT("5.5")},
        {TEXT("NexusAnimation.CustomBindings.get_custom_bindings_of_type"),TEXT("5.5")},
        {TEXT("NexusAnimation.CustomBindings.save_default_spawnable_state"),TEXT("5.5")},
        {TEXT("NexusAnimation.ImportExport.get_linked_anim_sequences"),TEXT("5.1")},
        {TEXT("NexusAnimation.ImportExport.get_linked_level_sequence"),TEXT("5.1")},
        {TEXT("NexusAnimation.ImportExport.link_anim_sequence"),TEXT("5.1")},
        {TEXT("NexusAnimation.Outliner.get_deactivated_nodes"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.get_locked_nodes"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.get_muted_nodes"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.get_node_label"),TEXT("5.3")},
        {TEXT("NexusAnimation.Outliner.get_outliner_children"),TEXT("5.3")},
        {TEXT("NexusAnimation.Outliner.get_outliner_selection"),TEXT("5.3")},
        {TEXT("NexusAnimation.Outliner.get_outliner_tree"),TEXT("5.3")},
        {TEXT("NexusAnimation.Outliner.get_pinned_nodes"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.get_sections_for_nodes"),TEXT("5.6")},
        {TEXT("NexusAnimation.Outliner.get_soloed_nodes"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.is_node_expanded"),TEXT("5.8")},
        {TEXT("NexusAnimation.Outliner.set_node_deactivated"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.set_node_expanded"),TEXT("5.8")},
        {TEXT("NexusAnimation.Outliner.set_node_locked"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.set_node_muted"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.set_node_pinned"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.set_node_solo"),TEXT("5.7")},
        {TEXT("NexusAnimation.Outliner.set_outliner_selection"),TEXT("5.3")},
        {TEXT("NexusAnimation.Sequencer.bake_transform"),TEXT("5.3")},
        {TEXT("NexusAnimation.Sequencer.get_focused_sequence"),TEXT("4.27")},
    };
    const FString* Version = Versions.Find(Domain + TEXT(".") + Tool);
    return Version ? *Version : TEXT("4.26");
}

static bool NexusAnimationValidateValue(const TSharedPtr<FJsonValue>& Value,const TSharedPtr<FJsonObject>& Schema,const FString& Field,FString& Error)
{
    if (!Value.IsValid() || !Schema.IsValid()) { Error = TEXT("Missing animation argument: ") + Field; return false; }
    const TArray<TSharedPtr<FJsonValue>>* Alternatives = nullptr;
    if (Schema->TryGetArrayField(TEXT("anyOf"), Alternatives)) {
        for (const auto& Alternative : *Alternatives) if (Alternative && Alternative->Type == EJson::Object) { FString Ignored; if (NexusAnimationValidateValue(Value, Alternative->AsObject(), Field, Ignored)) return true; }
        Error = TEXT("Animation argument has the wrong type: ") + Field; return false;
    }
    const FString Type = Schema->GetStringField(TEXT("type")); bool bValid = true;
    if (Type == TEXT("null")) bValid = Value->Type == EJson::Null;
    else if (Type == TEXT("string")) bValid = Value->Type == EJson::String;
    else if (Type == TEXT("boolean")) bValid = Value->Type == EJson::Boolean;
    else if (Type == TEXT("object")) bValid = Value->Type == EJson::Object;
    else if (Type == TEXT("number") || Type == TEXT("integer")) bValid = Value->Type == EJson::Number && FMath::IsFinite(Value->AsNumber()) && (Type != TEXT("integer") || (Value->AsNumber() >= MIN_int32 && Value->AsNumber() <= MAX_int32 && FMath::FloorToDouble(Value->AsNumber()) == Value->AsNumber()));
    else if (Type == TEXT("array")) {
        bValid = Value->Type == EJson::Array && Value->AsArray().Num() <= 128;
        const TSharedPtr<FJsonObject>* Items = nullptr;
        if (bValid && Schema->TryGetObjectField(TEXT("items"), Items)) for (const auto& Item : Value->AsArray()) if (!NexusAnimationValidateValue(Item, *Items, Field, Error)) return false;
    }
    if (!bValid) Error = TEXT("Animation argument has the wrong type or exceeds its bounds: ") + Field;
    return bValid;
}

bool FNexusAnimationAdvancedTools::PrepareArguments(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Arguments,TSharedPtr<FJsonObject>& Prepared,FString& Error)
{
    const auto Schema = InputSchema(Domain, Tool); const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!Arguments.IsValid() || !Schema || !Schema->TryGetObjectField(TEXT("properties"), Properties)) { Error = TEXT("Unknown independent animation action"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
    if (Schema->TryGetArrayField(TEXT("required"), Required)) for (const auto& Field : *Required) if (!Arguments->HasField(Field->AsString())) { Error = TEXT("Missing animation argument: ") + Field->AsString(); return false; }
    Prepared = MakeShared<FJsonObject>(); Prepared->Values = Arguments->Values;
    for (const auto& Field : Arguments->Values) {
        const auto* Property = (*Properties)->Values.Find(Field.Key);
        if (!Property || !Property->IsValid() || (*Property)->Type != EJson::Object) { Error = TEXT("Unknown animation argument: ") + FString(*Field.Key); return false; }
        if (!NexusAnimationValidateValue(Field.Value, (*Property)->AsObject(), FString(*Field.Key), Error)) return false;
    }
    for (const auto& Property : (*Properties)->Values) if (!Prepared->HasField(Property.Key) && Property.Value && Property.Value->Type == EJson::Object) {
        if (const auto* Default = Property.Value->AsObject()->Values.Find(TEXT("default"))) Prepared->SetField(Property.Key, *Default);
    }
    if ((Tool == TEXT("key_controls") || Tool == TEXT("key_controls_at_frames") || Tool == TEXT("bake_space")) && Prepared->GetArrayField(TEXT("control_names")).Num() == 0) { Error = TEXT("control_names must not be empty"); return false; }
    if (Tool == TEXT("key_controls_at_frames") && Prepared->GetArrayField(TEXT("frames")).Num() == 0) { Error = TEXT("frames must not be empty"); return false; }
    return true;
}
