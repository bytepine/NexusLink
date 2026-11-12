// Copyright byteyang. All Rights Reserved.
#pragma once

#include "Utils/NexusVersionCompat.h"
#if NX_UE_HAS_BLEND_SPACE_BASE
#include "Animation/BlendSpaceBase.h"
using FNexusBlendSpaceAsset = UBlendSpaceBase;
#else
#include "Animation/BlendSpace.h"
using FNexusBlendSpaceAsset = UBlendSpace;
#endif
