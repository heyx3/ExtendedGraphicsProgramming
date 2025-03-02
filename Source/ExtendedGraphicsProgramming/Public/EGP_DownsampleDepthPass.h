#pragma once

#include "CoreMinimal.h"

#include "ScreenPass.h"


namespace EGP
{
	//A straightforward copy of Unreal's AddDownsampleDepthPass(),
	//    which is sadly not exported from its DLL.
	EXTENDEDGRAPHICSPROGRAMMING_API void AddDownsampleDepthPass(
		FRDGBuilder& builder, const FViewInfo& view,
		FScreenPassTexture input, FScreenPassRenderTarget output,
		EDownsampleDepthFilter sampling
	);
}