#include "EGP_PostProcessMaterialShaders.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "Runtime/Renderer/Private/SceneTextureParameters.h"
#include "SystemTextures.h"


void EGP::impl::FillSimulationMaterialParams(FRDGBuilder& renderGraph,
											  FSimulationMaterialParameters* params,
											  const FMaterial* material,
											  const FSimulationPassMaterialInputs& inputs)
{
	auto* samplerPointClamp = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	auto* samplerBilinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	params->PostProcessInput_BilinearSampler = samplerBilinearClamp;
	
	FScreenPassTexture blackDummyTex{ GSystemTextures.GetBlackDummy(renderGraph) };
	renderGraph.RemoveUnusedTextureWarning(blackDummyTex.Texture);
	for (int i = 0; i < kPostProcessMaterialInputCountMax; ++i)
	{
		if (!inputs.Textures[i].Texture || !material->GetRenderingThreadShaderMap()->UsesSceneTexture(PPI_PostProcessInput0 + i))
			params->PostProcessInput[i] = GetScreenPassTextureInput(blackDummyTex, samplerPointClamp);
		else
			params->PostProcessInput[i] = inputs.Textures[i];
	}
}

void EGP::impl::FillScreenSpaceMaterialParams(FRDGBuilder& renderGraph,
										       FScreenSpaceMaterialParameters* params,
										       const FMaterial* material,
										       const FScreenSpacePassMaterialInputs& inputs)
{
	check(inputs.TargetView);
	FillSimulationMaterialParams(renderGraph, &params->BaseParams, material,
								 { inputs.Textures });
	
	if (inputs.SceneTextures)
		params->SceneTextures = *inputs.SceneTextures;
	else
		params->SceneTextures = { };
	
	params->View = inputs.TargetView->ViewUniformBuffer;
	params->PostProcessOutput = GetScreenPassTextureViewportParameters(inputs.OutputViewportData);
	params->EyeAdaptationBuffer = renderGraph.CreateSRV(GetEyeAdaptationBuffer(renderGraph, *inputs.TargetView));
}

void EGP::FSimulationShader::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& params,
														   FShaderCompilerEnvironment& env)
{
	FMaterialShader::ModifyCompilationEnvironment(params, env);
	env.SetDefine(TEXT("EGP_IS_SIMULATION"), 1);
	env.SetDefine(TEXT("EGP_POST_PASS"), 1);
}
void EGP::FScreenSpaceShader::ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& params,
														    FShaderCompilerEnvironment& env)
{
	FMaterialShader::ModifyCompilationEnvironment(params, env);
	env.SetDefine(TEXT("EGP_IS_SIMULATION"), 0);
	env.SetDefine(TEXT("EGP_POST_PASS"), 1);
}

bool EGP::FSimulationShader::ShouldCompilePermutation(const FMaterialShaderPermutationParameters& params)
{
	return FMaterialShader::ShouldCompilePermutation(params) &&
		   (params.MaterialParameters.MaterialDomain == MD_PostProcess) &&
		   (!IsMobilePlatform(params.Platform) || IsMobileHDR());
}
bool EGP::FScreenSpaceShader::ShouldCompilePermutation(const FMaterialShaderPermutationParameters& params)
{
	return FMaterialShader::ShouldCompilePermutation(params) &&
		   (params.MaterialParameters.MaterialDomain == MD_PostProcess) &&
		   (!IsMobilePlatform(params.Platform) || IsMobileHDR());
}

void EGP::FSimulationShader::SetParameters(FRHIBatchedShaderParameters& paramBatch,
											const FMaterialRenderProxy* matProxy, const FMaterial& mat,
											const FViewInfo& view)
{
	FMaterialShader::SetViewParameters(paramBatch, view, view.ViewUniformBuffer);
	FMaterialShader::SetParameters(paramBatch, matProxy, mat, view);
}
void EGP::FScreenSpaceShader::SetParameters(FRHIBatchedShaderParameters& paramBatch,
											 const FMaterialRenderProxy* matProxy, const FMaterial& mat,
											 const FViewInfo& view)
{
	FMaterialShader::SetViewParameters(paramBatch, view, view.ViewUniformBuffer);
	FMaterialShader::SetParameters(paramBatch, matProxy, mat, view);
}

IMPLEMENT_SHADER_TYPE(, EGP::FScreenSpaceRenderVS, TEXT("/EGP/ScreenPass/simple_vs.usf"), TEXT("MainVS"), SF_Vertex);
