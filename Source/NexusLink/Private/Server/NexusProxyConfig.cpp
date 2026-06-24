// Copyright byteyang. All Rights Reserved.

#include "Server/NexusProxyConfig.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

TSharedPtr<FJsonObject> FNexusProxyConfig::BuildConfigObject()
{
	TSharedPtr<FJsonObject> ConfigObj;

	FString JsonText;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NexusLink"));
	if (Plugin.IsValid())
	{
		const FString Path = FPaths::Combine(
			Plugin->GetBaseDir(), TEXT("Resources"), TEXT("ProxyConfig.json"));
		FFileHelper::LoadFileToString(JsonText, *Path);
	}

	if (!JsonText.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		FJsonSerializer::Deserialize(Reader, ConfigObj);
	}

	if (!ConfigObj.IsValid())
	{
		ConfigObj = MakeShared<FJsonObject>();
		ConfigObj->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
		ConfigObj->SetStringField(TEXT("minProxyVersion"), TEXT("1.3.3"));
		ConfigObj->SetStringField(
			TEXT("initializePrefix"),
			TEXT("NexusLink MCP Proxy for Unreal Engine."));
	}

	// 运行时注入插件版本，供代理做兼容性提示（打包脚本会注入真实版本号）
	FString LinkVersion = TEXT("0.0.0");
	if (Plugin.IsValid())
	{
		LinkVersion = Plugin->GetDescriptor().VersionName;
	}
	ConfigObj->SetStringField(TEXT("nexusLinkVersion"), LinkVersion);

	return ConfigObj;
}
