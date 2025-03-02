#include "EGP_DownsampleDepthPass.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Runtime/Renderer/Private/SceneRendering.h"


//This file contains a literal copy-paste of the Unreal implementation,
//    with tiny changes clearly noted by comments.
#pragma warning( push, 0 )
// ReSharper disable All

class FEGPDownsampleDepthPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FEGPDownsampleDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEGPDownsampleDepthPS, FGlobalShader);

	class FOutputMinAndMaxDepth : SHADER_PERMUTATION_BOOL("OUTPUT_MIN_AND_MAX_DEPTH");

	using FPermutationDomain = TShaderPermutationDomain<FOutputMinAndMaxDepth>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER(FVector2f, DstToSrcPixelScale)
		SHADER_PARAMETER(FVector2f, SourceMaxUV)
		SHADER_PARAMETER(FVector2f, DestinationResolution)
		SHADER_PARAMETER(uint32, DownsampleDepthFilter)
		SHADER_PARAMETER(FIntVector4, DstPixelCoordMinAndMax)
		SHADER_PARAMETER(FIntVector4, SrcPixelCoordMinAndMax)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FEGPDownsampleDepthPS, "/Engine/Private/DownsampleDepthPixelShader.usf", "Main", SF_Pixel);

void EGP::AddDownsampleDepthPass(FRDGBuilder& GraphBuilder, const FViewInfo& View,
                                  FScreenPassTexture Input, FScreenPassRenderTarget Output,
                                  EDownsampleDepthFilter DownsampleDepthFilter)
{
	const FScreenPassTextureViewport InputViewport(Input);
	const FScreenPassTextureViewport OutputViewport(Output);

	TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);

	const bool bIsMinAndMaxDepthFilter = DownsampleDepthFilter == EDownsampleDepthFilter::MinAndMaxDepth;
	FEGPDownsampleDepthPS::FPermutationDomain Permutation;
	Permutation.Set<FEGPDownsampleDepthPS::FOutputMinAndMaxDepth>(bIsMinAndMaxDepthFilter ? 1 : 0);
	TShaderMapRef<FEGPDownsampleDepthPS> PixelShader(View.ShaderMap, Permutation);

	// The lower right corner pixel whose coordinate is max considered excluded https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d11-rect
	// That is why we subtract -1 from the maximum value of the source viewport.

	FEGPDownsampleDepthPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FEGPDownsampleDepthPS::FParameters>();
	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->DepthTexture = Input.Texture;
	//NOTE: original unreal code used viewport Extents, but this breaks if the source depth buffer is a subset of its texture (which it is for Scene Capture Components)
	PassParameters->DstToSrcPixelScale = FVector2f(float(InputViewport.Rect.Width()) / float(OutputViewport.Rect.Width()), float(InputViewport.Rect.Height()) / float(OutputViewport.Rect.Height()));
	PassParameters->SourceMaxUV = FVector2f((float(View.ViewRect.Max.X) -1.0f - 0.51f) / InputViewport.Extent.X, (float(View.ViewRect.Max.Y) - 1.0f - 0.51f) / InputViewport.Extent.Y);
	PassParameters->DownsampleDepthFilter = (uint32)DownsampleDepthFilter;

	const int32 DownsampledSizeX = OutputViewport.Rect.Width();
	const int32 DownsampledSizeY = OutputViewport.Rect.Height();
	PassParameters->DestinationResolution = FVector2f(DownsampledSizeX, DownsampledSizeY);

	PassParameters->DstPixelCoordMinAndMax = FIntVector4(OutputViewport.Rect.Min.X, OutputViewport.Rect.Min.Y, OutputViewport.Rect.Max.X-1, OutputViewport.Rect.Max.Y-1);
	PassParameters->SrcPixelCoordMinAndMax = FIntVector4( InputViewport.Rect.Min.X,  InputViewport.Rect.Min.Y,  InputViewport.Rect.Max.X-1,  InputViewport.Rect.Max.Y-1);

	FRHIDepthStencilState* DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();

	if (bIsMinAndMaxDepthFilter)
	{
		DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, Output.LoadAction);
	}
	else
	{
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Output.Texture, Output.LoadAction, Output.LoadAction, FExclusiveDepthStencil::DepthWrite_StencilWrite);
	}

	static const TCHAR* kFilterNames[] = {
		TEXT("Point"),
		TEXT("Max"),
		TEXT("CheckerMinMax"),
		TEXT("MinAndMaxDepth"),
	};

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("DownsampleDepth(%s) %dx%dx -> %dx%d",
			kFilterNames[int32(DownsampleDepthFilter)],
			InputViewport.Rect.Width(),
			InputViewport.Rect.Height(),
			OutputViewport.Rect.Width(),
			OutputViewport.Rect.Height()),
		View,
		OutputViewport, InputViewport,
		VertexShader, PixelShader,
		DepthStencilState,
		PassParameters);
}

#pragma warning( pop )
