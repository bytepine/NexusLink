// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusEditorDomainTools.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusJsonUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "Utils/NexusPropertyUtils.h"
#include "Utils/NexusEditorControlServices.h"
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
#include "PCGEdge.h"
#include "PCGSubgraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utils/NexusAssetUtils.h"
#include "UObject/UObjectIterator.h"
#include "Data/PCGPointData.h"
#if NX_UE_HAS_PCG_DATA_VIEW_JSON
#include "Data/DataView/PCGDataView.h"
#include "Data/DataView/PCGDataViewData.h"
#include "Data/DataView/PCGDataViewNativeJsonConverters.h"
#include "Helpers/PCGHelpers.h"
#endif
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

void FNexusEditorDomainTools::ExecutePCG(const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FNexusEditorOperation>& Operation)
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
	FNexusEditorControlServices::AddTicker(FTickerDelegate::CreateLambda([Weak, Operation, Deadline](float)
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
    // 完整验证参数包后统一写入，所有反射属性类型都保留实例覆盖标记和传播通知。
    Instance->Modify();
    for (const auto& Pair : Values->Values) {
        const FName Name(*Pair.Key); const auto* Desc = Candidate.FindPropertyDescByName(Name); const FProperty* Property = Desc->CachedProperty;
        Instance->UpdatePropertyOverride(Property, true);
        FInstancedPropertyBag* Target = Instance->GetMutableUserParametersStruct_Unsafe();
        const auto* TargetDesc = Target->FindPropertyDescByName(Name);
        if (!TargetDesc || !TargetDesc->CachedProperty) { Error = TEXT("PCG instance parameter disappeared during update"); return false; }
        TargetDesc->CachedProperty->CopyCompleteValue(
            TargetDesc->CachedProperty->ContainerPtrToValuePtr<void>(Target->GetMutableValue().GetMemory()),
            Property->ContainerPtrToValuePtr<void>(Candidate.GetValue().GetMemory()));
        Instance->OnGraphParametersChanged(EPCGGraphParameterEvent::ValueModifiedLocally, Name);
    }
    Instance->MarkPackageDirty(); return true;
}

static TArray<TSharedPtr<FJsonValue>> NexusPCGPinDescriptions(const TArray<FPCGPinProperties>& Pins)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto& Pin : Pins)
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("Label"), Pin.Label.ToString());
        Item->SetBoolField(TEXT("bAllowMultipleConnections"), Pin.AllowsMultipleConnections());
        Item->SetBoolField(TEXT("bAllowMultipleData"), Pin.bAllowMultipleData);
        Result.Add(MakeShared<FJsonValueObject>(Item));
    }
    return Result;
}

static TMap<FString, UPCGSettings*> NexusPCGNativeSettings()
{
    TMap<FString, UPCGSettings*> Result;
    for (TObjectIterator<UClass> Class; Class; ++Class)
    {
        if (!Class->IsChildOf(UPCGSettings::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || Class->ClassGeneratedBy) continue;
        auto* Settings = Cast<UPCGSettings>(Class->GetDefaultObject());
        if (Settings && !Settings->GetDefaultNodeName().IsNone()) Result.Add(Settings->GetDefaultNodeName().ToString(), Settings);
    }
    return Result;
}
#endif

bool FNexusEditorDomainTools::PCG(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonObject>& Result, FString& Error)
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
    if (Tool == TEXT("ListNativeNodes"))
    {
        const TSet<FString> Common = {TEXT("SurfaceSampler"), TEXT("VolumeSampler"), TEXT("TransformPoints"), TEXT("StaticMeshSpawner"), TEXT("DensityFilter"), TEXT("PointFilter"), TEXT("GetActorData"), TEXT("GetLandscapeData"), TEXT("Projection"), TEXT("Union"), TEXT("Difference"), TEXT("Intersection"), TEXT("MergePoints"), TEXT("CopyPoints"), TEXT("SplineSampler"), TEXT("BoundsModifier")};
        TArray<FString> Names;
        for (const auto& Pair : NexusPCGNativeSettings()) if (!A.Bool(TEXT("bCommonOnly"), true) || Common.Contains(Pair.Key)) Names.Add(Pair.Key);
        Names.Sort(); TArray<TSharedPtr<FJsonValue>> Items;
        for (const auto& Name : Names) Items.Add(MakeShared<FJsonValueString>(Name));
        Result->SetArrayField(TEXT("Items"), Items); return true;
    }
    if (Tool == TEXT("GetNativeNodeSchema"))
    {
        const auto Settings = NexusPCGNativeSettings(); UPCGSettings* const* Found = Settings.Find(A.Str(TEXT("nodeName")));
        if (!Found || !*Found) { Error = TEXT("NodeName must come from NativeNodes discovery"); return false; }
        Result->SetStringField(TEXT("Name"), A.Str(TEXT("nodeName"))); Result->SetStringField(TEXT("ClassPath"), (*Found)->GetClass()->GetPathName());
        Result->SetStringField(TEXT("Description"), (*Found)->GetNodeTooltipText().ToString());
        TArray<TSharedPtr<FJsonValue>> Properties; FNexusPropertyUtils::CollectEditableProperties(*Found, Properties); Result->SetArrayField(TEXT("Properties"), Properties);
        Result->SetArrayField(TEXT("InputPins"), NexusPCGPinDescriptions((*Found)->InputPinProperties())); Result->SetArrayField(TEXT("OutputPins"), NexusPCGPinDescriptions((*Found)->OutputPinProperties()));
        return true;
    }
    if (Tool == TEXT("ListAvailableSubgraphs"))
    {
        FARFilter Filter; Filter.ClassPaths.Add(UPCGGraph::StaticClass()->GetClassPathName()); Filter.bRecursiveClasses = true;
        TArray<FAssetData> Assets; FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
        TArray<FString> Paths; for (const auto& Asset : Assets) Paths.Add(Asset.GetSoftObjectPath().ToString()); Paths.Sort();
        TArray<TSharedPtr<FJsonValue>> Items; for (const auto& Path : Paths) Items.Add(MakeShared<FJsonValueString>(Path));
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
#if !NX_UE_HAS_PCG_DATA_VIEW_JSON
        if (!Attribute.IsEmpty() && Attribute != TEXT("Transform") && Attribute != TEXT("Density") && Attribute != TEXT("Seed")) { Error = TEXT("This engine node-data API exposes Transform, Density and Seed only"); return false; }
#endif
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
#if NX_UE_HAS_PCG_DATA_VIEW_JSON
        FInstancedStruct ConverterParameters; ConverterParameters.InitializeAs<FPCGDataViewJsonParameters>();
        TArray<TSharedPtr<FJsonValue>> Datasets; int32 TotalElements = 0;
        const UPCGDataViewJsonConverter* Converter = GetDefault<UPCGDataViewJsonConverter>();
        for (const auto& Tagged : Collection->TaggedData)
        {
            const UPCGData* Data = Tagged.Data;
            if (!Data) continue;
            FPCGDataView View;
            if (const auto* ExistingView = Cast<UPCGDataViewData>(Data)) View = ExistingView->GetDataView();
            else View.ViewedData = Data;
            View.Selection.bAllAttributes = Attribute.IsEmpty();
            if (!Attribute.IsEmpty()) { View.Selection.Attributes.Reset(); View.Selection.Attributes.Add(FPCGAttributePropertySelector::CreateSelectorFromString<FPCGAttributePropertySelector>(Attribute)); }
            auto Encoded = Converter->SerializeToString(View, ConverterParameters);
            if (Encoded.HasError()) { Error = Encoded.GetError().ToString(); return false; }
            const FString DatasetJson = Encoded.StealValue();
            if (DatasetJson.Len() > 16 * 1024 * 1024) { Error = TEXT("PCG dataset exceeds the inspection limit; select an attribute or reduce generated data"); return false; }
            TSharedPtr<FJsonObject> Dataset;
            if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DatasetJson), Dataset)) { Error = TEXT("PCG data converter returned invalid JSON"); return false; }
            const int32 Count = PCGHelpers::GetNumberOfElements(Data);
            for (int32 ElementIndex = 0; ElementIndex < Count; ++ElementIndex)
                if (TotalElements + ElementIndex < Offset || TotalElements + ElementIndex >= End) Dataset->RemoveField(FString::Printf(TEXT("element_%d"), ElementIndex));
            Dataset->SetNumberField(TEXT("TotalElements"), Count); Dataset->SetNumberField(TEXT("DatasetOffset"), TotalElements);
            Datasets.Add(MakeShared<FJsonValueObject>(Dataset)); TotalElements += Count;
        }
        Result->SetArrayField(TEXT("Datasets"), Datasets); Result->SetNumberField(TEXT("Total"), TotalElements);
        Result->SetNumberField(TEXT("Returned"), FMath::Max(0, FMath::Min(End, TotalElements) - Offset)); return true;
#endif
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
        TArray<TSharedPtr<FJsonValue>> Properties;
        for (TFieldIterator<FProperty> Property(Node->GetSettings()->GetClass()); Property; ++Property)
        {
            if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Name"), Property->GetName()); Item->SetStringField(TEXT("Type"), Property->GetCPPType());
            const void* ValueAddress = Property->ContainerPtrToValuePtr<void>(Node->GetSettings());
            TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(*Property, ValueAddress);
            if (!Value.IsValid())
            {
                FString ValueText; FNexusPropertyUtils::ExportText(*Property, ValueText, ValueAddress); Value = MakeShared<FJsonValueString>(ValueText);
            }
            Item->SetField(TEXT("Value"), Value); Properties.Add(MakeShared<FJsonValueObject>(Item));
        }
        Result->SetArrayField(TEXT("Properties"), Properties); return true;
    }
    if (!Graph) { Error = TEXT("Graph reference is required"); return false; }
    if (Tool == TEXT("GetGraphStructure"))
    {
        TArray<TSharedPtr<FJsonValue>> Nodes, Edges;
        TArray<UPCGNode*> AllNodes = Graph->GetNodes(); AllNodes.Insert(Graph->GetInputNode(), 0); AllNodes.Add(Graph->GetOutputNode());
        for (UPCGNode* Node : AllNodes)
        {
            if (!Node) continue;
            auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("Path"), Node->GetPathName()); Item->SetStringField(TEXT("Name"), Node->GetName());
            Item->SetStringField(TEXT("Title"), Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());
            if (Node->GetSettings()) Item->SetStringField(TEXT("SettingsClass"), Node->GetSettings()->GetClass()->GetPathName());
            Item->SetArrayField(TEXT("InputPins"), NexusPCGPinDescriptions(Node->InputPinProperties())); Item->SetArrayField(TEXT("OutputPins"), NexusPCGPinDescriptions(Node->OutputPinProperties())); Nodes.Add(MakeShared<FJsonValueObject>(Item));
            for (const UPCGPin* Pin : Node->GetOutputPins()) if (Pin) for (const UPCGEdge* Edge : Pin->Edges)
            {
                const UPCGPin* Other = Edge ? Edge->GetOtherPin(Pin) : nullptr;
                if (!Other || !Other->Node) continue;
                auto Link = MakeShared<FJsonObject>(); Link->SetStringField(TEXT("FromNode"), Node->GetPathName()); Link->SetStringField(TEXT("FromPin"), Pin->Properties.Label.ToString());
                Link->SetStringField(TEXT("ToNode"), Other->Node->GetPathName()); Link->SetStringField(TEXT("ToPin"), Other->Properties.Label.ToString()); Edges.Add(MakeShared<FJsonValueObject>(Link));
            }
        }
        Result->SetStringField(TEXT("Name"), Graph->GetName()); Result->SetArrayField(TEXT("Nodes"), Nodes); Result->SetArrayField(TEXT("Edges"), Edges); return true;
    }
    if (Tool == TEXT("AddSubgraphNode"))
    {
        UPCGGraph* Subgraph = Cast<UPCGGraph>(Resolve(Args, TEXT("subGraphForNode")));
        const FString Name = A.Str(TEXT("nodeName"));
        if (!Subgraph || Subgraph == Graph || Subgraph->Contains(Graph) || Name.IsEmpty() || StaticFindObject(UObject::StaticClass(), Graph, *Name)) { Error = TEXT("Invalid subgraph, cycle or duplicate NodeName"); return false; }
        Graph->Modify(); UPCGSubgraphSettings* Settings = nullptr; UPCGNode* Node = Graph->AddNodeOfType(Settings);
        if (!Node || !Settings) { if (Node) Graph->RemoveNode(Node); Error = TEXT("Could not create PCG subgraph node"); return false; }
        if (!Node->Rename(*Name, nullptr, REN_Test) || !Node->Rename(*Name)) { Graph->RemoveNode(Node); Error = TEXT("Could not assign the requested NodeName"); return false; }
        Settings->SetSubgraph(Subgraph);
        auto* SubgraphInstance = Cast<UPCGGraphInstance>(Settings->GetSubgraphInterface());
        if (Values->Values.Num() && !NexusPCGSetInstance(SubgraphInstance, Values, Error)) { Graph->RemoveNode(Node); return false; }
        Node->NodeTitle = FName(*A.Str(TEXT("nodeTitle"))); Graph->MarkPackageDirty();
        Result->SetStringField(TEXT("NodePath"), Node->GetPathName()); Result->SetStringField(TEXT("Name"), Name); return true;
    }
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
                EPropertyBagPropertyType Kind = EPropertyBagPropertyType::None; UObject* TypeObject = nullptr;
                if (Type == TEXT("Float")) Kind = EPropertyBagPropertyType::Float;
                else if (Type == TEXT("Double")) Kind = EPropertyBagPropertyType::Double;
                else if (Type == TEXT("Integer32")) Kind = EPropertyBagPropertyType::Int32;
                else if (Type == TEXT("Integer64")) Kind = EPropertyBagPropertyType::Int64;
                else if (Type == TEXT("Boolean")) Kind = EPropertyBagPropertyType::Bool;
                else if (Type == TEXT("String")) Kind = EPropertyBagPropertyType::String;
                else if (Type == TEXT("Name")) Kind = EPropertyBagPropertyType::Name;
                else if (Type == TEXT("Vector2")) { Kind = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FVector2D>::Get(); }
                else if (Type == TEXT("Vector")) { Kind = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FVector>::Get(); }
                else if (Type == TEXT("Vector4")) { Kind = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FVector4>::Get(); }
                else if (Type == TEXT("Quaternion")) { Kind = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FQuat>::Get(); }
                else if (Type == TEXT("Transform")) { Kind = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FTransform>::Get(); }
                else if (Type == TEXT("Rotator")) { Kind = EPropertyBagPropertyType::Struct; TypeObject = TBaseStructure<FRotator>::Get(); }
                if (Kind == EPropertyBagPropertyType::None) { Error = TEXT("Unknown PCG parameter type"); return false; }
                const bool bArray = D.Str(TEXT("containerType")) == TEXT("Array");
                FPropertyBagPropertyDesc Description(Name, bArray ? EPropertyBagContainerType::Array : EPropertyBagContainerType::None, Kind, TypeObject);
#if WITH_EDITORONLY_DATA
                Description.MetaData.Emplace(TEXT("ToolTip"), D.Str(TEXT("description")));
#endif
                const auto* Existing = Candidate.FindPropertyDescByName(Name);
                if (Existing && !Existing->CompatibleType(Description)) { Error = TEXT("Changing an existing parameter type is not supported"); return false; }
                if (!Existing) Candidate.AddProperties({Description});
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
    Error = TEXT("This PCG action is unavailable in the current engine domain API"); return false;
#else
    Error = TEXT("PCG compatibility requires the PCG plugin and supported engine APIs"); return false;
#endif
}
