#include "EGP_GetMaterialShader.h"


TOptional<EGP::FShaderMapFindResult> EGP::FindMaterialShaders_RenderThread(const UMaterialInterface* uMaterial,
										  								   const FMaterialShaderTypes& shaderTypes,
																		   FShaderMapFindSettings settings)
{
	return FindMaterialShaders_RenderThread(uMaterial, shaderTypes, settings,
											[&](const FShaderMapFindCandidate&) { return true; });
}
