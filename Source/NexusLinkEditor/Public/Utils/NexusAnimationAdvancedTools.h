// Copyright byteyang. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
struct NEXUSLINKEDITOR_API FNexusAnimationAdvancedTools final
{
    using FReadRigControls = TFunction<TOptional<bool>(UObject*, const TSharedPtr<FJsonObject>&, FString&)>;
    using FValidateRigControl = TFunction<bool(UObject*, const TSharedPtr<FJsonObject>&, const FString&, UFunction*&, FString&)>;
    static FReadRigControls ReadRigControls;
    static FValidateRigControl ValidateRigControl;
    static bool ValidateFileIO(const FString& Tool,const TSharedPtr<FJsonObject>& Args,FString& Error);
    static bool Handles(const FString& Domain,const FString& Tool);
    static UFunction* FindMixerFunction(const FString& Tool);
    static TSharedPtr<FJsonObject> InputSchema(const FString& Domain,const FString& Tool);
    static FString MinimumVersion(const FString& Domain,const FString& Tool);
    static bool RequiresRigControlValidation(const FString& Domain, const TSharedPtr<FJsonObject>& Schema);
    static bool Supports(const FString& Domain,const FString& Tool);
    static bool PrepareArguments(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Arguments,TSharedPtr<FJsonObject>& Prepared,FString& Error);
    static UFunction* FindFunction(const FString& Domain,const FString& Tool);
    static bool Call(const FString& Domain,const FString& Tool,const TSharedPtr<FJsonObject>& Args,TSharedPtr<FJsonObject>& Result,FString& Error);
};
