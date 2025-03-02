#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneInfo.h"
#include "Runtime/Renderer/Private/SceneRendering.h"

namespace EGP
{
	//Gathers mesh draw calls for a static mesh.
    //Returns the proxy's 'batch element mask' (used when queueing up these draw calls).
    //
    //You can pass in scene proxies for other things like skeletal meshes,
    //    but in my experience they do not generate any calls from this
    //    (use GetDynamicMeshElementRange() instead).
    EXTENDEDGRAPHICSPROGRAMMING_API uint64 GetStaticMeshElements(const FSceneView& view, const FPrimitiveSceneProxy* proxy,
				                                                 TArray<FMeshBatch>& output);
    
    //A port of the unexported engine function 'GetDynamicMeshElementRange()',
    //    which helps draw dynamic geometry within a view for a custom render pass.
    //In my experience it always ouptuts an empty range for static mesh components, even Movable ones.
    EXTENDEDGRAPHICSPROGRAMMING_API FInt32Range GetDynamicMeshElementRange(const FViewInfo& info, uint32 primitiveIndex);

	
    //Generates mesh batches for a custom Mesh Pass Processor, on the given primitive.
	//
	//The lambda signature should be
	//    (const FMeshBatch&, uint64 elementMask, const FPrimitiveSceneProxy*, int staticMeshIDIfApplicable) -> void
    template<typename Lambda>
    void ForEachBatch(const FViewInfo& viewInfo, const FPrimitiveSceneProxy* proxy,
					  Lambda batchProcessor)
    {
    	if (!proxy)
    		return;

    	//Get metadata about the proxy's presence in its scene.
    	auto* sceneInfo = proxy->GetPrimitiveSceneInfo();
    	if (!sceneInfo || !sceneInfo->IsIndexValid())
    		return;

    	//Taken from FProjectedShadowInfo::SetupMeshDrawCommandsForProjectionStenciling()
    	int primitiveIdx = sceneInfo->GetIndex();
    	if (!viewInfo.PrimitiveVisibilityMap[primitiveIdx])
    		return;
    	const auto& primitiveRelevance = viewInfo.PrimitiveViewRelevanceMap[primitiveIdx];
    	if (primitiveRelevance.bStaticRelevance)
    	{
    		for (int staticMeshIdx = 0; staticMeshIdx < sceneInfo->StaticMeshes.Num(); ++staticMeshIdx)
    		{
    			const auto& staticMesh = sceneInfo->StaticMeshes[staticMeshIdx];
    			if (viewInfo.StaticMeshVisibilityMap[staticMesh.Id])
    			{
    				uint64 defaultBatchElementMask = ~0ull; //TODO: Can we get this somehow from LOD data?
    				batchProcessor(staticMesh, defaultBatchElementMask,
    							   staticMesh.PrimitiveSceneInfo->Proxy,
    							   staticMeshIdx);
    			}
    		}
    	}
    	if (primitiveRelevance.bDynamicRelevance)
    	{
    		auto dynamicElementRange = FInt32Range::Empty();
    		
    		////  Copy-paste of FViewInfo::GetDynamicMeshElementRange(), which isn't exported:
    		int32 primI = sceneInfo->GetIndex();
    		if (sceneInfo->IsIndexValid() && viewInfo.PrimitiveVisibilityMap[primI])
    			dynamicElementRange = FInt32Range{
    				viewInfo.DynamicMeshElementRanges[primI].X,
    				viewInfo.DynamicMeshElementRanges[primI].Y
    			};
    		////

    		for (int32 batchI = dynamicElementRange.GetLowerBoundValue(); batchI < dynamicElementRange.GetUpperBoundValue(); ++batchI)
    		{
    			const FMeshBatchAndRelevance& data = viewInfo.DynamicMeshElements[batchI];
    			uint64 batchElementMask = ~0ull;
    			batchProcessor(*data.Mesh, batchElementMask, data.PrimitiveSceneProxy, -1);
    		}
    	}
    }
}
