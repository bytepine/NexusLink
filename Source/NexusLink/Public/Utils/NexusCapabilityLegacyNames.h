// Copyright byteyang. All Rights Reserved.



#pragma once



#include "CoreMinimal.h"



/**

 * v1.8–v1.10 旧 Capability 名 → 当前规范名。

 * 权威表：Resources/legacy_capability_names.json（生成 Private/Utils/NexusCapabilityLegacyNames.generated.inl）。

 * call_capability 在查找注册表前自动解析别名。

 */

class NEXUSLINK_API FNexusCapabilityLegacyNames

{

public:

	/** 若 Name 为旧名则返回规范名，否则返回 Trim 后的原名。 */

	static FString Resolve(const FString& Name);



	/** 旧名存在时写入规范名并返回 true。 */

	static bool TryLookup(const FString& Name, FString& OutCanonical);

};


