// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusLegacyToolsets.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusEditorToolsets.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_PCG
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "PCGVolume.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#if NX_UE_HAS_PCG_BASE_POINT_DATA
#include "Data/PCGBasePointData.h"
#endif
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/NoExportTypes.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#endif

void FNexusLegacyToolsets::ExecutePCG(const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation)
{
#if WITH_PCG
	APCGVolume* Volume = Cast<APCGVolume>(Resolve(Args, TEXT("pCGVolume")));
	if (!Volume) Volume = Cast<APCGVolume>(Resolve(Args, TEXT("pcgVolume")));
	UPCGComponent* Component = Volume ? Volume->FindComponentByClass<UPCGComponent>() : nullptr;
	if (!Component || !Component->GetGraph() || Component->IsGenerating())
	{ FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("PCG component/graph is unavailable or already generating")); return; }
#if NX_UE_HAS_PCG_EXECUTION_STATE
	auto& Inspection = Component->GetExecutionState().GetInspection();
#else
	auto& Inspection = *Component;
#endif
	if (!Inspection.IsInspecting()) Inspection.EnableInspection();
	const FPCGTaskId Task = Component->GenerateLocalGetTaskId(true);
	if (Task == InvalidPCGTaskId) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("PCG did not schedule generation")); return; }
	const TWeakObjectPtr<UPCGComponent> Weak(Component);
	Operation->Stop = [Weak, Operation]() { if (!Weak.IsValid()) return false; Weak->CancelGeneration(); FNexusEditorOperations::Finish(Operation, TEXT("Cancelled"), nullptr); return true; };
	const double Deadline = FPlatformTime::Seconds() + 120;
	FNexusEditorToolsets::AddTicker(FTickerDelegate::CreateLambda([Weak, Operation, Deadline](float)
	{
		if (!Operation->IsRunning()) return false;
		if (!Weak.IsValid()) { FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("PCG component was destroyed")); return false; }
		if (FPlatformTime::Seconds() > Deadline) { Weak->CancelGeneration(); FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("PCG generation timed out")); return false; }
		if (Weak->IsGenerating()) return true;
		auto Result = MakeShared<FJsonObject>(); auto Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("bGenerationFinished"), true); Data->SetStringField(TEXT("ActorPath"), Weak->GetOwner()->GetPathName());
		Data->SetStringField(TEXT("Note"), TEXT("Execution finished; inspect NodeData and GetOutputLog for graph business errors"));
		Result->SetObjectField(TEXT("CompatibilityResult"), Data); FNexusEditorOperations::Finish(Operation, TEXT("Completed"), Result); return false;
	}), 0.1f);
#else
	FNexusEditorOperations::Finish(Operation, TEXT("Failed"), nullptr, TEXT("PCG plugin is unavailable"));
#endif
}

#if WITH_PCG
static bool NexusPCGImportBag(FInstancedPropertyBag& Bag, const TSharedPtr<FJsonObject>& Values, FString& Error)
{
    if (!Values.IsValid()) { Error = TEXT("Parameter object is required"); return false; }
    for (const auto& Pair : Values->Values) {
        const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(FName(*Pair.Key));
        if (!Desc || !Desc->CachedProperty || !FJsonObjectConverter::JsonValueToUProperty(Pair.Value, const_cast<FProperty*>(Desc->CachedProperty),
            Desc->CachedProperty->ContainerPtrToValuePtr<void>(Bag.GetMutableValue().GetMemory()), 0, 0)) {
            Error = TEXT("Unknown parameter or incompatible value: ") + FString(*Pair.Key); return false;
        }
    }
    return true;
}

static TSharedRef<FJsonObject> NexusPCGBagJson(const FInstancedPropertyBag* Bag)
{
    auto Result = MakeShared<FJsonObject>();
    if (Bag && Bag->GetPropertyBagStruct()) FJsonObjectConverter::UStructToJsonObject(Bag->GetPropertyBagStruct(), Bag->GetValue().GetMemory(), Result, 0, 0);
    return Result;
}

static bool NexusPCGSetInstance(UPCGGraphInstance* Instance, const TSharedPtr<FJsonObject>& Values, FString& Error)
{
    if (!Instance || !Instance->GetUserParametersStruct()) { Error = TEXT("PCG instance has no parameters"); return false; }
    FInstancedPropertyBag Candidate = *Instance->GetUserParametersStruct();
    if (!NexusPCGImportBag(Candidate, Values, Error)) return false;
    // 实例 setter 负责覆盖标志和传播通知；不直接写引擎的只读参数指针。
    for (const auto& Pair : Values->Values) {
        const auto* Desc = Candidate.FindPropertyDescByName(FName(*Pair.Key));
        const FProperty* Property = Desc->CachedProperty;
        if (!CastField<FNumericProperty>(Property) && !CastField<FBoolProperty>(Property) && !CastField<FStrProperty>(Property) && !CastField<FNameProperty>(Property)) {
            Error = TEXT("This engine compatibility backend currently supports scalar instance overrides: ") + FString(*Pair.Key); return false;
        }
    }
    Instance->Modify();
    for (const auto& Pair : Values->Values) {
        const FName Name(*Pair.Key); const auto* Desc = Candidate.FindPropertyDescByName(Name); const FProperty* Property = Desc->CachedProperty;
        const void* Data = Property->ContainerPtrToValuePtr<void>(Candidate.GetValue().GetMemory());
        if (const auto* Number = CastField<FNumericProperty>(Property)) {
            if (Number->IsFloatingPoint()) {
                if (CastField<FFloatProperty>(Property)) Instance->SetGraphParameter<float>(Name, static_cast<float>(Number->GetFloatingPointPropertyValue(Data)));
                else Instance->SetGraphParameter<double>(Name, Number->GetFloatingPointPropertyValue(Data));
            } else if (Property->GetSize() == sizeof(int32)) Instance->SetGraphParameter<int32>(Name, static_cast<int32>(Number->GetSignedIntPropertyValue(Data)));
            else Instance->SetGraphParameter<int64>(Name, Number->GetSignedIntPropertyValue(Data));
        } else if (const auto* Bool = CastField<FBoolProperty>(Property)) Instance->SetGraphParameter<bool>(Name, Bool->GetPropertyValue(Data));
        else if (const auto* String = CastField<FStrProperty>(Property)) Instance->SetGraphParameter<FString>(Name, String->GetPropertyValue(Data));
        else if (const auto* NameProperty = CastField<FNameProperty>(Property)) Instance->SetGraphParameter<FName>(Name, NameProperty->GetPropertyValue(Data));
    }
    Instance->MarkPackageDirty(); return true;
}
#endif

bool FNexusLegacyToolsets::PCG(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
{
#if WITH_PCG
    const FNexusArgs A(Args);
    UPCGGraph* Graph = Cast<UPCGGraph>(Resolve(Args, TEXT("graph")));
    APCGVolume* Volume = Cast<APCGVolume>(Resolve(Args, TEXT("pCGVolume")));
    if (!Volume) Volume = Cast<APCGVolume>(Resolve(Args, TEXT("pcgVolume")));
    UPCGComponent* Component = Volume ? Volume->FindComponentByClass<UPCGComponent>() : nullptr;
    UPCGGraphInstance* Instance = Component ? Component->GetGraphInstance() : nullptr;
    TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
    const FString Json = A.Str(TEXT("jsonParams"));
    if (!Json.IsEmpty() && !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Values)) { Error = TEXT("JsonParams must be an object"); return false; }
    if (Tool == TEXT("ListGraphInstances")) {
        TArray<TSharedPtr<FJsonValue>> Items;
        if (GEditor && GEditor->GetEditorWorldContext().World()) for (TActorIterator<APCGVolume> It(GEditor->GetEditorWorldContext().World()); It; ++It) {
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Path"), It->GetPathName()); Item->SetStringField(TEXT("Name"), It->GetName()); Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("Items"), Items); return true;
    }
    if (Tool == TEXT("SpawnGraphInstance"))
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!Graph || !World) { Error = TEXT("Graph and editor world are required"); return false; }
        const TSharedPtr<FJsonObject>* Transform = nullptr; const TSharedPtr<FJsonObject>* Location = nullptr;
        if (Args->TryGetObjectField(TEXT("transform"), Transform)) (*Transform)->TryGetObjectField(TEXT("location"), Location);
        const FNexusArgs Position(Location ? *Location : nullptr);
        const FVector Point(Position.Num(TEXT("x")), Position.Num(TEXT("y")), Position.Num(TEXT("z")));
        FActorSpawnParameters Spawn; Spawn.Name = MakeUniqueObjectName(World->PersistentLevel, APCGVolume::StaticClass(), FName(*A.Str(TEXT("name"), Graph->GetName())));
        APCGVolume* Added = World->SpawnActor<APCGVolume>(Point, FRotator::ZeroRotator, Spawn);
        if (!Added) { Error = TEXT("Could not spawn PCG volume"); return false; }
        Added->SetActorLabel(A.Str(TEXT("name"), Graph->GetName())); Added->SetActorScale3D(FVector(25, 25, 10));
        UCubeBuilder* Builder = NewObject<UCubeBuilder>(); Builder->X = Builder->Y = Builder->Z = 200; Builder->Hollow = false;
        UActorFactory::CreateBrushForVolumeActor(Added, Builder);
        if (UBrushComponent* Brush = Added->GetBrushComponent()) { Brush->ReregisterComponent(); Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision); Brush->SetCanEverAffectNavigation(false); }
        UPCGComponent* AddedComponent = Added->FindComponentByClass<UPCGComponent>();
        if (!AddedComponent) { World->DestroyActor(Added); Error = TEXT("Spawned volume has no PCG component"); return false; }
        AddedComponent->SetGraph(Graph);
        if (Values->Values.Num() && !NexusPCGSetInstance(AddedComponent->GetGraphInstance(), Values, Error)) { World->DestroyActor(Added); return false; }
        Result->SetStringField(TEXT("ActorPath"), Added->GetPathName()); Result->SetStringField(TEXT("Name"), Added->GetName()); return true;
    }
    if (Tool == TEXT("GetGraphInstanceParams")) { if (!Instance) { Error = TEXT("PCG instance not found"); return false; } Result = NexusPCGBagJson(Instance->GetUserParametersStruct()); return true; }
    if (Tool == TEXT("GetNodeDataView"))
    {
        UPCGNode* Node = Cast<UPCGNode>(Resolve(Args, TEXT("node")));
        if (!Component || !Node || Node->GetGraph() != Component->GetGraph()) { Error = TEXT("Node must belong to the selected PCG instance graph"); return false; }
        if (Component->IsGenerating()) { Error = TEXT("Wait for graph execution to finish before reading node data"); return false; }
        const FString Attribute = A.Str(TEXT("attributeName"));
        if (!Attribute.IsEmpty() && Attribute != TEXT("Transform") && Attribute != TEXT("Density") && Attribute != TEXT("Seed")) { Error = TEXT("Compatibility node data currently exposes Transform, Density and Seed only"); return false; }
        const UPCGPin* Pin = Node->GetOutputPin(FName(*A.Str(TEXT("pinLabel"), TEXT("Out"))));
        if (!Pin) { Error = TEXT("Output pin not found"); return false; }
#if NX_UE_HAS_PCG_EXECUTION_STATE
        auto& Inspection = Component->GetExecutionState().GetInspection();
#else
        auto& Inspection = *Component;
#endif
#if NX_UE_HAS_PCG_SHARED_INSPECTION_DATA
        const auto Stacks = Inspection.GetExecutedNodeStacks(Node);
#else
        const auto AllStacks = Inspection.GetExecutedNodeStacks();
        const auto* Found = AllStacks.Find(TObjectKey<const UPCGNode>(Node));
        // 各版本使用不同栈容器，缺失节点时保留映射的原生值类型。
        const auto Stacks = Found ? *Found : decltype(AllStacks)::ValueType();
#endif
        if (!Inspection.IsInspecting()) Inspection.EnableInspection();
        if (Stacks.Num() != 1) { Error = TEXT("Node has no inspection data or multiple execution stacks; execute the instance and use a single-stack node"); return false; }
#if NX_UE_HAS_PCG_INSPECTION_OBJECT
        FPCGStack Stack = Stacks.CreateConstIterator()->Stack;
#else
        FPCGStack Stack = *Stacks.CreateConstIterator();
#endif
        Stack.PushFrame(Node); Stack.PushFrame(Pin);
#if NX_UE_HAS_PCG_SHARED_INSPECTION_DATA
        const auto InspectionData = Inspection.GetInspectionDataPtr(Stack);
        const FPCGDataCollection* Collection = InspectionData ? InspectionData->Data.Get() : nullptr;
#else
        const FPCGDataCollection* Collection = Inspection.GetInspectionData(Stack);
#endif
        if (!Collection) { Error = TEXT("No captured output data; execute after enabling inspection"); return false; }
        const int32 Offset = FMath::Max(0, static_cast<int32>(A.Num(TEXT("startIndex")))), End = FMath::Clamp(static_cast<int32>(A.Num(TEXT("endIndex"), 100)), Offset, Offset + 1000);
        TArray<TSharedPtr<FJsonValue>> Items; int32 Total = 0;
        for (const auto& Tagged : Collection->TaggedData)
        {
#if NX_UE_HAS_PCG_BASE_POINT_DATA
            const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Tagged.Data);
#else
            const UPCGPointData* Points = Cast<UPCGPointData>(Tagged.Data);
#endif
            if (!Points) { Error = TEXT("Compatibility node data currently supports point datasets only"); return false; }
#if NX_UE_HAS_PCG_BASE_POINT_DATA
            const int32 Count = Points->GetNumPoints();
#else
            const int32 Count = Points->GetPoints().Num();
#endif
            const int32 BeginPoint = FMath::Clamp(Offset - Total, 0, Count), EndPoint = FMath::Clamp(End - Total, 0, Count);
            for (int32 PointIndex = BeginPoint; PointIndex < EndPoint; ++PointIndex)
            {
#if NX_UE_HAS_PCG_BASE_POINT_DATA
                const FTransform Transform = Points->GetTransform(PointIndex); const float Density = Points->GetDensity(PointIndex); const int32 Seed = Points->GetSeed(PointIndex);
#else
                const FPCGPoint& Point = Points->GetPoints()[PointIndex]; const FTransform Transform = Point.Transform; const float Density = Point.Density; const int32 Seed = Point.Seed;
#endif
                auto Item = MakeShared<FJsonObject>(); Item->SetNumberField(TEXT("Index"), Total + PointIndex);
                if (Attribute.IsEmpty() || Attribute == TEXT("Transform")) Item->SetStringField(TEXT("Transform"), Transform.ToString());
                if (Attribute.IsEmpty() || Attribute == TEXT("Density")) Item->SetNumberField(TEXT("Density"), Density);
                if (Attribute.IsEmpty() || Attribute == TEXT("Seed")) Item->SetNumberField(TEXT("Seed"), Seed);
                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
            Total += Count;
        }
        Result->SetArrayField(TEXT("Items"), Items); Result->SetNumberField(TEXT("Total"), Total); Result->SetNumberField(TEXT("Returned"), Items.Num()); return true;
    }
    if (Tool == TEXT("SetGraphInstanceParams")) return NexusPCGSetInstance(Instance, Values, Error);
    if (Tool == TEXT("ResetGraphInstanceParams")) {
        if (!Instance || !Instance->GetUserParametersStruct()) { Error = TEXT("PCG instance not found"); return false; }
        const auto Names = A.StrArr(TEXT("paramNames"));
        for (const auto& Name : Names) if (!Instance->GetUserParametersStruct()->FindPropertyDescByName(FName(*Name))) { Error = TEXT("Unknown parameter: ") + Name; return false; }
        Instance->Modify(); for (const auto& Name : Names) Instance->ResetPropertyToDefault(Instance->GetUserParametersStruct()->FindPropertyDescByName(FName(*Name))->CachedProperty);
        Instance->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("UpdateNode")) {
        UPCGNode* Node = Cast<UPCGNode>(Resolve(Args, TEXT("node")));
        if (!Node || !Node->GetSettings()) { Error = TEXT("PCG node/settings not found"); return false; }
        if (!SetProperties(Node->GetSettings(), Values, Error)) return false;
        if (!A.Str(TEXT("nodeTitle")).IsEmpty()) { Node->Modify(); Node->NodeTitle = FName(*A.Str(TEXT("nodeTitle"))); }
        Node->MarkPackageDirty(); return true;
    }
    if (Tool == TEXT("GetNodeInfo")) {
        UPCGNode* Node = Cast<UPCGNode>(Resolve(Args, TEXT("node")));
        if (!Node || !Node->GetSettings()) { Error = TEXT("PCG node/settings not found"); return false; }
        Result->SetStringField(TEXT("Path"), Node->GetPathName()); Result->SetStringField(TEXT("ClassPath"), Node->GetSettings()->GetClass()->GetPathName());
        TArray<TSharedPtr<FJsonValue>> Properties; FNexusPropertyUtils::CollectEditableProperties(Node->GetSettings(), Properties); Result->SetArrayField(TEXT("Properties"), Properties); return true;
    }
    if (!Graph) { Error = TEXT("Graph reference is required"); return false; }
    if (Tool == TEXT("GetGraphSchema")) {
        Result=NexusPCGBagJson(Graph->GetUserParametersStruct()); TArray<TSharedPtr<FJsonValue>> Parameters;
        if (const auto* Bag=Graph->GetUserParametersStruct()) if (Bag->IsValid()) for (const auto& Desc : Bag->GetPropertyBagStruct()->GetPropertyDescs()) {
            auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"),Desc.Name.ToString()); Item->SetStringField(TEXT("Type"),Desc.CachedProperty?Desc.CachedProperty->GetCPPType():TEXT("Unknown")); Parameters.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("Parameters"),Parameters); return true;
    }
    if (Tool == TEXT("SetGraphParams") || Tool == TEXT("RemoveGraphParams")) {
        FInstancedPropertyBag Candidate = *Graph->GetUserParametersStruct();
        if (Tool == TEXT("RemoveGraphParams")) {
            for (const auto& Name : A.StrArr(TEXT("paramNames"))) if (!Candidate.FindPropertyDescByName(FName(*Name))) { Error = TEXT("Unknown parameter: ") + Name; return false; }
            for (const auto& Name : A.StrArr(TEXT("paramNames"))) Candidate.RemovePropertyByName(FName(*Name));
        } else {
            const TArray<TSharedPtr<FJsonValue>>* Definitions = nullptr;
            if (!Args->TryGetArrayField(TEXT("params"), Definitions)) { Error = TEXT("Parameter definitions required"); return false; }
            for (const auto& Item : *Definitions) {
                const FNexusArgs D(Item->AsObject()); const FName Name(*D.Str(TEXT("name"))); const FString Type = D.Str(TEXT("type"));
                EPropertyBagPropertyType Kind = EPropertyBagPropertyType::None;
                if (Type == TEXT("Float")) Kind = EPropertyBagPropertyType::Float;
                else if (Type == TEXT("Double")) Kind = EPropertyBagPropertyType::Double;
                else if (Type == TEXT("Integer32")) Kind = EPropertyBagPropertyType::Int32;
                else if (Type == TEXT("Integer64")) Kind = EPropertyBagPropertyType::Int64;
                else if (Type == TEXT("Boolean")) Kind = EPropertyBagPropertyType::Bool;
                else if (Type == TEXT("String")) Kind = EPropertyBagPropertyType::String;
                else if (Type == TEXT("Name")) Kind = EPropertyBagPropertyType::Name;
                if (Kind == EPropertyBagPropertyType::None || D.Str(TEXT("containerType")) == TEXT("Array")) { Error = TEXT("Compatibility graph definitions currently require scalar numeric/bool/string/name types"); return false; }
                const auto* Existing = Candidate.FindPropertyDescByName(Name);
                if (Existing && Existing->ValueType != Kind) { Error = TEXT("Changing an existing parameter type is not supported"); return false; }
                if (!Existing) Candidate.AddProperty(Name, Kind);
                const FString Default = D.Str(TEXT("defaultValueJson"));
                if (!Default.IsEmpty()) {
                    TArray<TSharedPtr<FJsonValue>> Parsed;
                    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("[") + Default + TEXT("]")), Parsed) || Parsed.Num() != 1) { Error = TEXT("Invalid default JSON"); return false; }
                    auto Input = MakeShared<FJsonObject>(); Input->SetField(Name.ToString(), Parsed[0]); if (!NexusPCGImportBag(Candidate, Input, Error)) return false;
                }
            }
        }
        Graph->Modify();
#if NX_UE_HAS_PCG_PARAMETER_UPDATE_HELPER
        Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag) { Bag = Candidate; });
#else
        // 通过公开基类接口刷新完整参数包，与新版 helper 一样传播默认值并保留实例覆盖。
        // 删除事件还会清理已失去参数的 Getter 节点，不依赖各版本不同的属性编辑回调。
        *Graph->GetMutableUserParametersStruct_Unsafe() = Candidate;
        static_cast<UPCGGraphInterface*>(Graph)->OnGraphParametersChanged(
            Tool == TEXT("RemoveGraphParams") ? EPCGGraphParameterEvent::RemovedUnused : EPCGGraphParameterEvent::GraphPostLoad, NAME_None);
#endif
        Graph->MarkPackageDirty(); return true;
    }
    Error = TEXT("This PCG action is not supported by this engine compatibility backend"); return false;
#else
    Error = TEXT("PCG compatibility requires the PCG plugin and supported engine APIs"); return false;
#endif
}
