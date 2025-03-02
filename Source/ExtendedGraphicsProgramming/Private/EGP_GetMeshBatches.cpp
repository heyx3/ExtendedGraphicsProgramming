#include "EGP_GetMeshBatches.h"


uint64 EGP::GetStaticMeshElements(const FSceneView& view, const FPrimitiveSceneProxy* proxy,
                                  TArray<FMeshBatch>& output)
{
	
	uint64 lod = proxy->GetLOD(&view);
	proxy->GetMeshDescription(lod, output);

	return uint64{1} << lod;
}

FInt32Range EGP::GetDynamicMeshElementRange(const FViewInfo& info, uint32 primitiveIndex)
{
	//To head off bugs in our render passes, check for garbage primitive indices.
	if (primitiveIndex >= static_cast<uint32>(info.PrimitiveViewRelevanceMap.Num()))
		return FInt32Range::Empty();

	// DynamicMeshEndIndices contains valid values only for visible primitives with bDynamicRelevance.
	if (info.PrimitiveVisibilityMap[primitiveIndex])
	{
		const FPrimitiveViewRelevance& ViewRelevance = info.PrimitiveViewRelevanceMap[primitiveIndex];
		if (ViewRelevance.bDynamicRelevance)
		{
			return FInt32Range(info.DynamicMeshElementRanges[primitiveIndex].X, info.DynamicMeshElementRanges[primitiveIndex].Y);
		}
	}

	return FInt32Range::Empty();
}
