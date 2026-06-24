// Copyright byteyang. All Rights Reserved.

#include "Capabilities/Lua/Asset/NexusGetAssetLuaBindingCapability.h"

#if WITH_UNLUA

#include "NexusCapabilityRegistry.h"
#include "NexusMcpSchemaBuilder.h"
#include "Utils/NexusLuaUtils.h"
#include "Utils/NexusAssetUtils.h"
#include "Engine/Blueprint.h"
#include "Misc/Paths.h"

void FGetAssetLuaBindingCapability::BuildDefinition(FNexusCapabilityDefinition& Out) const
{
	Out.Name = TEXT("get_asset_lua_binding");
	Out.Description = TEXT("解析 BP 绑定的 UnLua 模块。返回 bound/fileExists；需 UnLua。");
	Out.InputSchema = FNexusSchema::Object()
		.Required(TEXT("assetPath"), FNexusSchema::Str(TEXT("蓝图资产路径")))
		.Build();
	Out.Tags = {FNexusMcpTags::Readonly, FNexusMcpTags::Blueprint };
	Out.ExtraSearchKeywords = { TEXT("unlua"), TEXT("module"), TEXT("mapping"), TEXT("filepath"), TEXT("script") };
	Out.RelatedCapabilities = { TEXT("get_runtime_lua_object"), TEXT("get_runtime_lua_env") };
	Out.Prerequisites = { TEXT("unlua"), TEXT("editor_only") };
	Out.WhenToUse = TEXT("读/编 Lua 前先找绑定文件路径");
}

FCapabilityResult FGetAssetLuaBindingCapability::Execute(const TSharedPtr<FJsonObject>& Arguments) const
{
	FCapabilityResult R;

	FString AssetPath;
	if (!RequireString(Arguments, TEXT("assetPath"), AssetPath, R.Entries))
		return R;

	UObject* Obj = FNexusAssetUtils::LoadAssetWithFallback<UObject>(AssetPath);
	if (!Obj)
		return FCapabilityResult::MakeFatal(FString::Printf(TEXT("资产未找到: %s"), *AssetPath));

	UBlueprint* BP = Cast<UBlueprint>(Obj);
	if (!BP || !BP->GeneratedClass)
		return FCapabilityResult::MakeFatal(TEXT("assetPath 须指向 Blueprint 资产"));

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("assetPath"), AssetPath);
	if (BP->ParentClass)
		Entry->SetStringField(TEXT("parentClass"), BP->ParentClass->GetName());

	const FString ModuleName = FNexusLuaUtils::GetUnLuaModuleName(BP->GeneratedClass);
	if (ModuleName.IsEmpty())
	{
		Entry->SetBoolField(TEXT("bound"), false);
		Entry->SetStringField(TEXT("hint"),
			TEXT("Blueprint has no UnLua binding; do not guess Lua paths. Use get_asset_blueprint or runtime Lua caps instead."));
		R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		return R;
	}

	const FString LuaAbsPath = FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("Script"), ModuleName.Replace(TEXT("."), TEXT("/"))) + TEXT(".lua");

	Entry->SetBoolField(TEXT("bound"), true);
	Entry->SetStringField(TEXT("moduleName"), ModuleName);
	Entry->SetStringField(TEXT("expectedLuaPath"), LuaAbsPath);

	if (FPaths::FileExists(LuaAbsPath))
	{
		Entry->SetBoolField(TEXT("fileExists"), true);
		Entry->SetStringField(TEXT("luaFilePath"), LuaAbsPath);
	}
	else
	{
		Entry->SetBoolField(TEXT("fileExists"), false);
		Entry->SetStringField(TEXT("hint"),
			TEXT("UnLua 模块已注册但磁盘缺少 .lua 文件；请检查 Content/Script 路径或创建文件。"));
	}

	R.Entries.Add(MakeShared<FJsonValueObject>(Entry));
	return R;
}

REGISTER_MCP_CAPABILITY(FGetAssetLuaBindingCapability)

#endif // WITH_UNLUA
