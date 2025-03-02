#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialRenderProxy.h"


namespace EGP
{
	//A potential fallback material that could render your shaders.
	struct FShaderMapFindCandidate
	{
		const FMaterial* Material = nullptr;
		const FMaterialRenderProxy* MaterialProxy = nullptr;
	};
	
	//A chosen fallback material to render your shaders.
	struct FShaderMapFindResult : FShaderMapFindCandidate
	{
		const FMaterialShaderMap* Map = nullptr;
		FMaterialShaders Shaders;
	};

	//Parameters to compiling a Material against a shader.
	struct FShaderMapFindSettings
	{
		//Required if you want to be able to fall back to the default Material;
		//    otherwise the operation may fail to find any compiled shaders.
		TOptional<EMaterialDomain> Domain;

		//You can always pick GMaxRHIFeatureLevel, but try to pass your view's current feature level.
		ERHIFeatureLevel::Type FeatureLevel;

		//Leave null if your shaders are not mesh-material shaders.
		FVertexFactoryType* VertexFactory = nullptr;
	};

	//Tries to compile the given material shader(s) against a Material graph,
	//    iterating through fallback Materials until we find an applicable one.
	EXTENDEDGRAPHICSPROGRAMMING_API TOptional<FShaderMapFindResult> FindMaterialShaders_RenderThread(const UMaterialInterface* uMaterial,
																					 		         const FMaterialShaderTypes& shaderTypes,
																							         FShaderMapFindSettings settings);
	//Tries to compile the given material shader(s) against a Material graph,
	//    iterating through fallback Materials until we find an applicable one.
	//
	//The predicate lambda should look like '(const EGP::FShaderMapFindCandidate&) -> bool'.
	template<typename MaterialPredicate>
	TOptional<FShaderMapFindResult> FindMaterialShaders_RenderThread(const UMaterialInterface* uMaterial,
																	 const FMaterialShaderTypes& shaderTypes,
												   			         FShaderMapFindSettings settings,
												   			         MaterialPredicate predicate)
	{
		check(IsInRenderingThread());
		
		//If the input Material is null, grab the engine default for this domain.
		if (uMaterial == nullptr)
			if (settings.Domain.IsSet())
 				uMaterial = UMaterial::GetDefaultMaterial(*settings.Domain);
			else
				return NullOpt;
		
		const FMaterialRenderProxy* proxy = uMaterial->GetRenderProxy();
		check(proxy);
		
		//Define the logic for trying to extract shaders from a material.
		FMaterialShaders foundShaders;
		auto tryMaterial = [&](const FMaterial* mat) -> bool
		{
			return (mat != nullptr) &&
				   (!settings.Domain.IsSet() || settings.Domain == mat->GetMaterialDomain()) &&
				   predicate(FShaderMapFindCandidate{ mat, proxy }) &&
				   mat->TryGetShaders(shaderTypes, settings.VertexFactory, foundShaders);
		};

		//Run through Material fallbacks until we find one that works.
		const FMaterial* mat = nullptr;
		while (proxy != nullptr)
		{
			mat = proxy->GetMaterialNoFallback(settings.FeatureLevel);
			if (tryMaterial(mat))
				return FShaderMapFindResult{ mat, proxy, mat->GetRenderingThreadShaderMap(), foundShaders };
			else
				proxy = proxy->GetFallback(settings.FeatureLevel);
		}

		return NullOpt;
	}
}