// Copyright byteyang. All Rights Reserved.
#include "Utils/NexusAnimationRetargetUtils.h"
#include "Utils/NexusArgs.h"
#include "Utils/NexusAssetUtils.h"
#include "Utils/NexusBlueprintEditUtils.h"
#include "Utils/NexusBlueprintGraphUtils.h"
#include "Utils/NexusVersionCompat.h"
#include "NexusMcpSchemaBuilder.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "EditorAnimUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor.h"
#include "Serialization/ArchiveReplaceObjectRef.h"

static TArray<UObject*> RetargetBlueprintObjects(UAnimBlueprint* Blueprint)
{
	TArray<UObject*> Objects = {Blueprint};
	if (Blueprint->GeneratedClass) Objects.Add(Blueprint->GeneratedClass->GetDefaultObject());
	TArray<UEdGraph*> Graphs;
	FNexusBlueprintGraphUtils::CollectAllGraphs(Blueprint, Graphs);
	for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes) if (Node) Objects.Add(Node);
	return Objects;
}

void FNexusAnimationRetargetUtils::ExtendSchema(const TSharedPtr<FJsonObject>& OperationSchema)
{
	const auto Properties = OperationSchema->GetObjectField(TEXT("properties"));
	Properties->SetObjectField(TEXT("TargetSkeleton"), FNexusSchema::Str(TEXT("Retarget: target Skeleton asset path; duplicate the source separately to preserve it")));
	Properties->SetObjectField(TEXT("bConvertSpaces"), FNexusSchema::Bool(TEXT("Retarget animation tracks between reference poses; not UE5 IK retargeting"), true, false));
	Properties->SetObjectField(TEXT("bRetargetReferredAssets"), FNexusSchema::Bool(TEXT("Sequence only: explicitly allow native retarget of referenced animations; all affected assets are returned"), true, false));
	Properties->SetObjectField(TEXT("bAllowPotentialDataLoss"), FNexusSchema::Bool(TEXT("Retarget requires explicit acknowledgment: missing bone tracks and transform curves may be removed"), true, false));
	Properties->SetObjectField(TEXT("bValidateOnly"), FNexusSchema::Bool(TEXT("Retarget preflight only; no asset modifications"), true, false));
	const auto Remap = FNexusSchema::Object()
		.Required(TEXT("SourceAssetPath"), FNexusSchema::Str(TEXT("Referenced source animation")))
		.Required(TEXT("TargetAssetPath"), FNexusSchema::Str(TEXT("Existing animation using TargetSkeleton"))).Build().ToSharedRef();
	Properties->SetObjectField(TEXT("AnimationRemaps"), FNexusSchema::ArrayOf(TEXT("Explicit reference replacements; referenced animations are never retargeted implicitly"), Remap));
}

bool FNexusAnimationRetargetUtils::Apply(UObject* Asset, const TSharedPtr<FJsonObject>& Operation, const TSharedPtr<FJsonObject>& Entry, bool bFinalizeRequested)
{
	const FNexusArgs Args(Operation);
	const auto Fail = [&](const FString& Error) { Entry->SetStringField(TEXT("Error"), Error); return false; };
	if (Args.Bool(TEXT("bValidateOnly")) && bFinalizeRequested) return Fail(TEXT("Retarget bValidateOnly cannot be combined with bCompile or bSaveToDisk"));
	if (!GEditor || GEditor->IsPlayingSessionInEditor()) return Fail(TEXT("Stop PIE before retargeting"));
	UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Asset);
	UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
	USkeleton* Target = FNexusAssetUtils::LoadAssetWithFallback<USkeleton>(Args.Str(TEXT("TargetSkeleton")));
	USkeleton* Source = nullptr;
	if (Blueprint) Source = Blueprint->TargetSkeleton;
	else if (Sequence) Source = Sequence->GetSkeleton();
	if (!Source || !Target) return Fail(TEXT("Retarget requires an animation/AnimBlueprint with a source Skeleton and valid TargetSkeleton"));
	Entry->SetStringField(TEXT("OldSkeleton"), Source->GetPathName());
	Entry->SetStringField(TEXT("TargetSkeleton"), Target->GetPathName());
	Entry->SetBoolField(TEXT("bChanged"), false);
	if (Source == Target) { Entry->SetBoolField(TEXT("bValid"), true); return true; }
	if (Source->GetReferenceSkeleton().GetNum() == 0 || Target->GetReferenceSkeleton().GetNum() == 0)
		return Fail(TEXT("Both Skeletons require a valid reference skeleton"));
	if (Blueprint)
	{
		if (!Blueprint->GeneratedClass) return Fail(TEXT("Compile the AnimBlueprint before retarget preflight"));
		FString Error;
		if (!FNexusBlueprintEditUtils::ValidateLeaf(Blueprint, Error)) return Fail(Error);
		UAnimBlueprint* Parent = Blueprint->ParentClass ? Cast<UAnimBlueprint>(Blueprint->ParentClass->ClassGeneratedBy) : nullptr;
		if (Parent && Parent->TargetSkeleton != Target) return Fail(TEXT("Retarget the parent AnimBlueprint explicitly first; parent assets are never modified implicitly"));
	}
	TArray<UAnimationAsset*> References;
	if (Blueprint)
	{
		// UE4 的 EditorAnimUtils 蓝图引用函数未导出；用公共序列化接口读取相同的 CDO/图节点。
		for (UObject* Object : RetargetBlueprintObjects(Blueprint))
		{
			TArray<UObject*> Found;
			FReferenceFinder Finder(Found, nullptr, false, true, false, false);
			Finder.FindReferences(Object);
			for (UObject* Reference : Found) if (UAnimationAsset* Animation = Cast<UAnimationAsset>(Reference)) References.AddUnique(Animation);
		}
	}
	else Sequence->GetAllAnimationSequencesReferred(References);
	TMap<UAnimationAsset*, UAnimationAsset*> Remaps;
	const TArray<TSharedPtr<FJsonValue>>* RequestedRemaps = nullptr;
	if (Operation->TryGetArrayField(TEXT("AnimationRemaps"), RequestedRemaps))
	{
		if (RequestedRemaps->Num() > 128) return Fail(TEXT("AnimationRemaps is limited to 128 entries"));
		for (const auto& Value : *RequestedRemaps)
		{
			const FNexusArgs Remap(Value->AsObject());
			UAnimationAsset* From = FNexusAssetUtils::LoadAssetWithFallback<UAnimationAsset>(Remap.Str(TEXT("SourceAssetPath")));
			UAnimationAsset* To = FNexusAssetUtils::LoadAssetWithFallback<UAnimationAsset>(Remap.Str(TEXT("TargetAssetPath")));
			if (!From || !To || From == To || !References.Contains(From) || Remaps.Contains(From) || To->GetSkeleton() != Target || From->GetClass() != To->GetClass())
				return Fail(TEXT("AnimationRemaps require unique referenced sources and matching animation classes using TargetSkeleton"));
			Remaps.Add(From, To);
		}
	}
	TArray<TSharedPtr<FJsonValue>> Unmapped;
	for (UAnimationAsset* Reference : References)
		if (Reference && Reference != Sequence && Reference->GetSkeleton() != Target && !Remaps.Contains(Reference)
			&& (Blueprint || !Args.Bool(TEXT("bRetargetReferredAssets"))))
			Unmapped.Add(MakeShared<FJsonValueString>(Reference->GetPathName()));
	Entry->SetArrayField(TEXT("UnmappedAnimations"), Unmapped);
	if (Unmapped.Num() > 0) return Fail(TEXT("Retarget referenced animations separately and supply AnimationRemaps; no source references were changed"));
	if (Blueprint && Args.Bool(TEXT("bRetargetReferredAssets"))) return Fail(TEXT("AnimBlueprint retarget uses explicit AnimationRemaps; recursive retarget is supported only for sequences"));
	if (Sequence && !Args.Bool(TEXT("bRetargetReferredAssets")))
		for (UAnimationAsset* Reference : References)
			if (Reference && Reference != Sequence) return Fail(TEXT("Native sequence retarget visits referenced animations; set bRetargetReferredAssets=true explicitly or retarget a sequence without references"));
	TArray<TSharedPtr<FJsonValue>> MissingBones;
	for (const FMeshBoneInfo& Bone : Source->GetReferenceSkeleton().GetRefBoneInfo())
		if (Target->GetReferenceSkeleton().FindBoneIndex(Bone.Name) == INDEX_NONE) MissingBones.Add(MakeShared<FJsonValueString>(Bone.Name.ToString()));
	Entry->SetArrayField(TEXT("MissingBones"), MissingBones);
	TArray<TSharedPtr<FJsonValue>> AffectedPaths = {MakeShared<FJsonValueString>(Asset->GetPathName())};
	if (Sequence)
	{
		AffectedPaths.Add(MakeShared<FJsonValueString>(Target->GetPathName()));
		if (Args.Bool(TEXT("bConvertSpaces"))) AffectedPaths.Add(MakeShared<FJsonValueString>(Source->GetPathName()));
		for (UAnimationAsset* Reference : References) if (Reference && Reference != Asset)
			AffectedPaths.Add(MakeShared<FJsonValueString>(Remaps.Contains(Reference) ? Remaps[Reference]->GetPathName() : Reference->GetPathName()));
	}
	Entry->SetArrayField(TEXT("AffectedAssets"), AffectedPaths);
	Entry->SetBoolField(TEXT("bValid"), true);
	Entry->SetBoolField(TEXT("bValidateOnly"), Args.Bool(TEXT("bValidateOnly")));
	if (Args.Bool(TEXT("bValidateOnly"))) return true;
	if (!Args.Bool(TEXT("bAllowPotentialDataLoss"))) return Fail(TEXT("Inspect bValidateOnly first, then set bAllowPotentialDataLoss=true"));
	Asset->SetFlags(RF_Transactional); Asset->Modify();
	if (Blueprint)
	{
		if (Remaps.Num() > 0) for (UObject* Object : RetargetBlueprintObjects(Blueprint))
		{
			Object->SetFlags(RF_Transactional); Object->Modify();
#if NX_UE_HAS_ARCHIVE_REPLACE_OBJECT_FLAGS
			FArchiveReplaceObjectRef<UAnimationAsset> Replace(Object, Remaps, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
#else
			FArchiveReplaceObjectRef<UAnimationAsset> Replace(Object, Remaps, false, true, true);
#endif
		}
		Blueprint->TargetSkeleton = Target;
		FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		Sequence->ReplaceReferredAnimations(Remaps);
		// 原生流程会访问引用序列和 Skeleton 元数据；上方要求显式允许，并完整回报影响范围。
		EditorAnimUtils::FAnimationRetargetContext Context(TArray<TWeakObjectPtr<UObject>>{Sequence}, Args.Bool(TEXT("bRetargetReferredAssets")), Args.Bool(TEXT("bConvertSpaces")));
		TArray<UObject*> Affected = Context.GetAllDuplicates();
		if (Args.Bool(TEXT("bConvertSpaces"))) Affected.AddUnique(Source);
		Affected.AddUnique(Target);
		for (UObject* Object : Affected) if (Object) { Object->SetFlags(RF_Transactional); Object->Modify(); }
		Context.RetargetAnimations(Source, Target);
		TArray<TSharedPtr<FJsonValue>> Dirty;
		for (UObject* Object : Affected)
			if (Object && Object != Asset && Object->GetOutermost()->IsDirty()) Dirty.Add(MakeShared<FJsonValueString>(Object->GetPathName()));
		Entry->SetArrayField(TEXT("AdditionalDirtyAssets"), Dirty);
	}
	Asset->MarkPackageDirty();
	Entry->SetBoolField(TEXT("bChanged"), true);
	Entry->SetStringField(TEXT("Note"), TEXT("In-place legacy Skeleton retarget. Compile/read back the target; bSaveToDisk also saves AdditionalDirtyAssets. UE5 IK retargeting is separate."));
	return true;
}
