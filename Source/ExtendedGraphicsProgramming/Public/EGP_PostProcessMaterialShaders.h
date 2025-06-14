#pragma once

#include "CoreMinimal.h"
#include "MaterialDomain.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "Runtime/Renderer/Private/SceneRendering.h"

#include "EGP_GetMaterialShader.h"

/*
    This file lets you write Material shaders using the infrastructure of the post-process Material domain,
        to accomplish one of two things:

      * Offscreen work (e.g. compute simulation). We call this a "simulation" pass.
      * Screen-space graphics work, with a Compute or Vertex+Pixel shader. We call this a "screen-space" pass.

	To make shaders for one of these passes, do the following:
	  1. Inherit from FSimulationShader or FScreenSpaceShader
	  2. Include an extra line in your shader(s)' parameter struct, either
	       EGP_SIMULATION_PASS_MATERIAL_DATA() or EGP_SCREEN_SPACE_PASS_MATERIAL_DATA().
	  3. Create an 'input' to configure the post-process Material inputs, either
	       FSimulationPassMaterialInputs or FScreenSpacePassMaterialInputs.
	  4. Create a 'state' to describe how to execute your shader pipeline, either
		   FSimulationPassState or FScreenSpacePassRenderState or FScreenSpacePassComputeState
		   (or the templated child versions that provide extra control).
	  5. Call the correct function for your pass, either
		   AddSimulationMaterialPass() or AddScreenSpaceRenderPass() or AddScreenSpaceComputePass().
*/

//First define Simulation passes:
namespace EGP
{
	//The base class for shaders that run Simulation passes.
	struct EXTENDEDGRAPHICSPROGRAMMING_API FSimulationShader : public FMaterialShader
	{
		using FMaterialShader::FMaterialShader;
		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters&, FShaderCompilerEnvironment&);
		static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters&);
		void SetParameters(FRHIBatchedShaderParameters&, const FMaterialRenderProxy*, const FMaterial&, const FViewInfo&);
	};
	
	//These inputs translate into the typical post-process input nodes in a Material graph.
	//Not all kinds of post-process input nodes are handled in a Simulation pass.
	struct FSimulationPassMaterialInputs
	{
    	//The Post-Process input textures (Materials can sample from these).
    	//You decide which textures to expose to Materials and which slot each one goes into.
    	TStaticArray<FScreenPassTextureInput, kPostProcessMaterialInputCountMax> Textures;
	};

	//Defines how a simulation pass compute-shader should be executed.
	//
	//By default, parameter setup will be done automatically by calling SetShaderParametersMixedCS(...).
	//If you instead want to provide a lambda for parameter setup, use the child struct TSimulationPassState.
	struct FSimulationPassState
	{
		//The group count can be given now, taken from a GPU buffer ("indirect dispatch"),
		//    or computed by a lambda just before dispatch.
		//
		//Small note: it is UB for the lambda instance to be used in more than one pass or called anywhere else.
		TVariant<FIntVector3, //Known group count
				 TTuple<FRDGBufferRef, uint32>, //Indirect dispatch, reading group count from the given buffer at the given byte offset
				 FRDGDispatchGroupCountCallback* //Group count is computed immediately before dispatch, on the Render Thread.
				> GroupCount;
		
		//The permutation of the Compute Shader to use.
		int PermutationID = 0;

		bool UseAsyncCompute = false;


		FSimulationPassState() { }
		
		template<typename TGroupCount>
		FSimulationPassState(const TGroupCount& gc, int permutationID = 0, bool asyncCompute = false)
			: PermutationID(permutationID), UseAsyncCompute(asyncCompute)
		{
			if constexpr (std::is_same_v<TGroupCount, decltype(GroupCount)>)
				GroupCount = gc;
			else
				GroupCount.Set<TGroupCount>(gc);
		}

		virtual ~FSimulationPassState() { }
	};
	
	//Defines how a simulation pass compute-shader should be executed.
	//
	//This variant allows you to provide a custom lambda for parameter setup,
	//    rather than the default behavior of it calling SetShaderParametersMixedCS(...).
	template<typename SetupFn>
	struct TSimulationPassState : public FSimulationPassState
	{
		//Called right before shader dispatch,
		//    and right *after* group count is computed if you used a lambda for that.
		//
		//Its signature should be
		//		(TOptional<FIntVector3> groupCountUnlessIndirect,
		//		 FRHICommandList&, TShaderRef<TComputeShader>,
		//		 const FMaterialRenderProxy*, const FMaterial*
		//	    ) -> void
		//  with an extra last parameter 'const FViewInfo&' if running a Screen-Space pass instead of a Simulation pass.
		//
		//Small note: it is UB for the lambda instance to be used in more than one pass or called anywhere else.
		SetupFn SetupCallback;

		template<typename TGroupCount>
		TSimulationPassState(SetupFn&& setupCallback, TGroupCount groupCount,
							 int permutationID = 0,
							 bool useAsyncCompute = false)
			: FSimulationPassState{ groupCount, permutationID, useAsyncCompute },
			  SetupCallback(MoveTemp(setupCallback))
		{
			
		}
	};

	//Contains the boilerplate parameters for a Simulation material pass.
	//The data in this struct will be filled in for you when you add the pass to the RDG. 
	#define EGP_SIMULATION_PASS_MATERIAL_DATA() \
		SHADER_PARAMETER_STRUCT_INCLUDE(::EGP::impl::FSimulationMaterialParameters, SimulationPassData)
	
	//Private stuff
	namespace impl
	{
		BEGIN_SHADER_PARAMETER_STRUCT(FSimulationMaterialParameters, EXTENDEDGRAPHICSPROGRAMMING_API)
			SHADER_PARAMETER_STRUCT_ARRAY(FScreenPassTextureInput, PostProcessInput, [kPostProcessMaterialInputCountMax])
			SHADER_PARAMETER_SAMPLER(SamplerState, PostProcessInput_BilinearSampler)
		END_SHADER_PARAMETER_STRUCT()
	
		EXTENDEDGRAPHICSPROGRAMMING_API void FillSimulationMaterialParams(
			FRDGBuilder& renderGraph,
			FSimulationMaterialParameters* params,
			const FMaterial* material,
			const FSimulationPassMaterialInputs& inputs
		);
	}

	
	//Executes a compute Material Shader using the given Material.
	//
	//Your shader parameter struct must contain `EGP_SIMULATION_PASS_MATERIAL_DATA()`,
	//    and its contents will be filled in by this function.
	template<typename TComputeShader, typename TPassParams, typename SetupFn>
	void AddSimulationMaterialPass(FRDGBuilder& renderGraph, FRDGEventName&& event,
								   ERHIFeatureLevel::Type featureLevel,
								   const UMaterialInterface* material,
								   const FSimulationPassMaterialInputs& inputs,
								   const TSimulationPassState<SetupFn>& state,
								   TPassParams* paramStruct)
	{
		static_assert(std::is_base_of_v<FSimulationShader, TComputeShader>,
					  "Your Simulation pass shader must inherit from FSimulationShader!");
		check(IsInRenderingThread());

		//Compile the shaders against the Material.
		FMaterialShaderTypes types;
		types.AddShaderType<TComputeShader>(state.PermutationID);
		auto foundShaders = EGP::FindMaterialShaders_RenderThread(
			material, types,
			{ MD_PostProcess, featureLevel }
		);
		check(foundShaders);

		//Extract the shader and material proxy.
		auto* materialProxy = foundShaders->MaterialProxy;
		auto* materialF = foundShaders->Material;
		TShaderRef<TComputeShader> shaderC;
		ensure(foundShaders->Shaders.TryGetComputeShader(shaderC));

		//Run the pass.
		impl::FillSimulationMaterialParams(renderGraph, &paramStruct->SimulationPassData, materialF, inputs);
		auto setupCallback = state.SetupCallback;
		ERDGPassFlags flags = state.UseAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute;
		const decltype(FSimulationPassState::GroupCount)& groupCount = state.GroupCount; //Avoid dependent template BS
		//The usual helper function for compute dispatch, FComputeShaderUtils::AddPass,
		//    is made for Global shaders and does not handle Material shaders correctly.
		//Therefore we have to do it all manually :(
		if (groupCount.IsType<FIntVector3>())
		{
			auto gc = groupCount.Get<FIntVector3>();
			renderGraph.AddPass(MoveTemp(event), paramStruct, flags,
							    [gc, shaderC, materialF, materialProxy,
							    			   setupCallback = MoveTemp(setupCallback)]
									(FRHICommandList& cmds) {
				FRHIComputeShader* shaderRHI = shaderC.GetComputeShader();
				SetComputePipelineState(cmds, shaderRHI);
							    	
				setupCallback(gc, cmds, shaderC, materialProxy, materialF);
				cmds.DispatchComputeShader(gc.X, gc.Y, gc.Z);
				UnsetShaderUAVs(cmds, shaderC, shaderRHI);
		    });
		}
		else if (groupCount.IsType<TTuple<FRDGBufferRef, uint32>>())
		{
			auto indirectArgs = groupCount.Get<TTuple<FRDGBufferRef, uint32>>();
			renderGraph.AddPass(MoveTemp(event), paramStruct, flags,
							    [indirectArgs, shaderC, materialF, materialProxy,
							    		       setupCallback = MoveTemp(setupCallback)]
									(FRHICommandList& cmds) {
				//The RDG doesn't know that we truly use the indirect dispatch buffer
				//    because it gets sent directly into a command-list,
				//    so we need to tell it we do to avoid warnings (and possibly out-of-order scheduling?).
				indirectArgs.Key->MarkResourceAsUsed();
				FComputeShaderUtils::ValidateIndirectArgsBuffer(indirectArgs.Key, indirectArgs.Value);
							    	
				FRHIComputeShader* shaderRHI = shaderC.GetComputeShader();
				SetComputePipelineState(cmds, shaderRHI);
							    	
				setupCallback(NullOpt, cmds, shaderC, materialProxy, materialF);
				cmds.DispatchIndirectComputeShader(indirectArgs.Key->GetIndirectRHICallBuffer(), indirectArgs.Value);
				UnsetShaderUAVs(cmds, shaderC, shaderRHI);
		    });
		}
		else if (groupCount.IsType<FRDGDispatchGroupCountCallback*>())
		{
			auto groupCountLambda = MoveTemp(*groupCount.Get<FRDGDispatchGroupCountCallback*>());
			renderGraph.AddPass(MoveTemp(event), paramStruct, flags,
							    [groupCountLambda = MoveTemp(groupCountLambda),
							    			   shaderC, materialF, materialProxy,
							    			   setupCallback = MoveTemp(setupCallback)]
							        (FRHICommandList& cmds) {
				FRHIComputeShader* shaderRHI = shaderC.GetComputeShader();
				SetComputePipelineState(cmds, shaderRHI);

				FIntVector3 groupCount = groupCountLambda();
				setupCallback(groupCount, cmds, shaderC, materialProxy, materialF);
				cmds.DispatchComputeShader(groupCount.X, groupCount.Y, groupCount.Z);
				UnsetShaderUAVs(cmds, shaderC, shaderRHI);
		    });
		}
		else
		{
			//Unhandled case!
			check(false);
		}
	}
	//Executes a compute Material Shader using the given Material.
	//
	//Your shader parameter struct must contain `EGP_SIMULATION_PASS_MATERIAL_DATA()`,
	//    and its contents will be filled in by this function.
	//
	//While a Simulation pass doesn't conceptually have an associated View,
	//    unfortunately it appears that Material shaders *need* to refer to one when setting parameters.
	template<typename TComputeShader, typename TPassParams>
	void AddSimulationMaterialPass(FRDGBuilder& renderGraph, FRDGEventName&& event,
								   const FSimulationPassMaterialInputs& inputs,
								   const FSimulationPassState& state,
								   const FViewInfo& view,
								   TPassParams* paramStruct,
								   const UMaterialInterface* material)
	{
		auto defaultSetupFn = [paramStruct, &view]
								(TOptional<FIntVector3> groupCountIfDirect,
								 FRHICommandList& cmds,
								 TShaderRef<TComputeShader> shaderC,
								 const FMaterialRenderProxy* matProxy,
								 const FMaterial* mat)
		{
			SetShaderParametersMixedCS(cmds, shaderC, *paramStruct,
									   matProxy, *mat, view);
		};
		
		AddSimulationMaterialPass<TComputeShader, TPassParams, decltype(defaultSetupFn)>(
			renderGraph, MoveTemp(event), view.FeatureLevel,
			material, inputs,
			TSimulationPassState<decltype(defaultSetupFn)>{
				MoveTemp(defaultSetupFn),
				state.GroupCount, state.PermutationID, state.UseAsyncCompute
			},
			paramStruct
		);
	}
}
	
//Now define Screen-Space passes:
namespace EGP
{
	//The base class for shaders that run Screen-Space passes.
	struct EXTENDEDGRAPHICSPROGRAMMING_API FScreenSpaceShader : public FMaterialShader
	{
		using FMaterialShader::FMaterialShader;
		static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters&, FShaderCompilerEnvironment&);
		static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters&);
		void SetParameters(FRHIBatchedShaderParameters&, const FMaterialRenderProxy*, const FMaterial&, const FViewInfo&);
	};
	
	//These inputs translate into the typical post-process input nodes in a Material graph.
	struct FScreenSpacePassMaterialInputs
	{
		//The Post-Process input textures (Materials can sample from these).
		//You decide which textures to expose to Materials and which slot each one goes into.
		TStaticArray<FScreenPassTextureInput, kPostProcessMaterialInputCountMax> Textures;
		
		//The uniform buffer containing all scene textures, if such a thing exists in your case.
    	TOptional<FSceneTextureShaderParameters> SceneTextures;

		//Informs the Material how to compute UV's correctly in various operations.
    	FScreenPassTextureViewport InputViewportData, OutputViewportData;

		const FViewInfo* TargetView = nullptr;
    };
	
	//Contains the boilerplate parameters for a Screen-Space material pass.
	//The data in this struct will be filled in for you when you add the pass to the RDG. 
	#define EGP_SCREEN_SPACE_PASS_MATERIAL_DATA() \
		SHADER_PARAMETER_STRUCT_INCLUDE(::EGP::impl::FScreenSpaceMaterialParameters, ScreenSpacePassData)

	//Private stuff.
	namespace impl
	{
		BEGIN_SHADER_PARAMETER_STRUCT(FScreenSpaceMaterialParameters, EXTENDEDGRAPHICSPROGRAMMING_API)
			SHADER_PARAMETER_STRUCT_INCLUDE(impl::FSimulationMaterialParameters, BaseParams)
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
			SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, PostProcessOutput)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, EyeAdaptationBuffer)
		END_SHADER_PARAMETER_STRUCT()

		EXTENDEDGRAPHICSPROGRAMMING_API void FillScreenSpaceMaterialParams(
			FRDGBuilder& renderGraph,
			FScreenSpaceMaterialParameters* params,
			const FMaterial* material,
			const FScreenSpacePassMaterialInputs& inputs
		);
	}

	//Unreal's FScreenPassPipelineState seems to have changed the stencil ref type from uint32 to uint8 after 5.3.
	using uint_UnrealScreenPassStencil_t =
		#if ENGINE_MINOR_VERSION < 4
			uint32
		#else
			uint8
		#endif
	;
	
	//Instructions for how a screen-space Material pass should render itself, using a Vertex and Pixel shader.
	//By default, parameter setup will be done automatically by calling
	//    SetShaderParametersMixedVS(...) and SetShaderParametersMixedPS(...).
	//
	//If you want to dispatch a screen-space Compute Shader,
	//    use the alternative struct FScreenSpacePassComputeState.
	//
	//If you wanted to provide a lambda for parameter setup,
	//    use the child struct TScreenSpacePassRenderState.
	struct FScreenSpacePassRenderState
	{
		FRHIBlendState* BlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
		FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();
		uint_UnrealScreenPassStencil_t StencilRef = 0;

		int PermutationIdVS = 0,
			PermutationIdPS = 0;
	};
	
	//Instructions for how a screen-space Material pass should render itself, using a Vertex and Pixel shader.
	//This variant allows you to provide a custom lambda for parameter setup,
	//    rather than the default behavior of it calling SetShaderParametersMixed[VS|PS](...).
	template<typename SetupFn>
	struct TScreenSpacePassRenderState : public FScreenSpacePassRenderState
	{
		//Called right before shader dispatch.
		//
		//Its signature should be
		//		(FRHICommandList&,
		//		 TShaderRef<TVertexShader>, TShaderRef<TPixelShader>,
		//		 const FMaterialRenderProxy*, const FMaterial*,
		//		 const FViewInfo&
		//	    ) -> void
		SetupFn SetupCallback;

		TScreenSpacePassRenderState(SetupFn&& setupCallback,
								    FRHIBlendState* blendState = nullptr,
								    FRHIDepthStencilState* depthStencilState = nullptr,
								    uint_UnrealScreenPassStencil_t stencilRef = 0,
								    int permutationIdVS = 0, int permutationIdPS = 0)
			: FScreenSpacePassRenderState{ blendState, depthStencilState, stencilRef, permutationIdVS, permutationIdPS },
			  SetupCallback(MoveTemp(setupCallback))
		{
			
		}
	};

	
	//Instructions for how a screen-space Material pass should execute, using a Compute shader.
	//
	//By default, parameter setup will be done automatically by calling SetShaderParametersMixedCS(...).
	//If you instead want to provide a lambda for parameter setup, use the child struct TScreenSpacePassComputeState.
	using FScreenSpacePassComputeState = FSimulationPassState;
	
	//Instructions for how a screen-space Material pass should execute, using a Compute shader.
	//
	//This variant allows you to provide a custom lambda for parameter setup,
	//    rather than the default behavior of it calling SetShaderParametersMixedCS(...).
	template<typename SetupFn>
	using TScreenSpacePassComputeState = TSimulationPassState<SetupFn>;
	
	
	//A simple Vertex Shader for Screen-Space render passes.
	class EXTENDEDGRAPHICSPROGRAMMING_API FScreenSpaceRenderVS : public FScreenSpaceShader
    {
    public:
    	DECLARE_SHADER_TYPE(FScreenSpaceRenderVS, Material);

    	using FParameters = impl::FScreenSpaceMaterialParameters;
    	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FScreenSpaceRenderVS, FScreenSpaceShader);
    };

	//Sets up a Screen-Space render pass, with a vertex and pixel shader using a post-process Material.
	//In most cases you can use 'EGP::FScreenSpaceRenderVS' for your vertex shader.
	//
	//No attempt is made to respect the Material's blend mode, stencil mode, etc;
	//    you are responsible for that if it's something you care about.
	//
	//See `AddPostProcessMaterialPass()` for sample engine code.
	template<typename TVertexShader, typename TPixelShader, typename TPassParams, typename SetupFn>
	void AddScreenSpaceRenderPass(FRDGBuilder& renderGraph, FRDGEventName&& event,
								  const FScreenSpacePassMaterialInputs& inputs,
								  const TScreenSpacePassRenderState<SetupFn>& state,
								  TPassParams* paramStruct,
								  const UMaterialInterface* material)
	{
		static_assert(std::is_base_of_v<FScreenSpaceShader, TVertexShader>,
					  "Your Screen-Space pass vertex shader must inherit from FScreenSpaceShader!");
		static_assert(std::is_base_of_v<FScreenSpaceShader, TPixelShader>,
					  "Your Screen-Space pass pixel shader must inherit from FScreenSpaceShader!");
		check(IsInRenderingThread());
		check(inputs.TargetView);

		//Compile the shaders against the Material.
		FMaterialShaderTypes types;
		types.AddShaderType<TVertexShader>(state.PermutationIdVS);
		types.AddShaderType<TPixelShader>(state.PermutationIdPS);
		auto foundShaders = EGP::FindMaterialShaders_RenderThread(
			material, types,
			{ MD_PostProcess, inputs.TargetView->FeatureLevel }
		);
		check(foundShaders);

		//Extract the shaders and material proxy.
		auto* materialProxy = foundShaders->MaterialProxy;
		auto* materialF = foundShaders->Material;
		TShaderRef<TVertexShader> shaderV;
		TShaderRef<TPixelShader> shaderP;
		ensure(foundShaders->Shaders.TryGetVertexShader(shaderV) &&
			   foundShaders->Shaders.TryGetPixelShader(shaderP));

		//Run the pass.
		impl::FillScreenSpaceMaterialParams(renderGraph, &paramStruct->ScreenSpacePassData,
										    materialF, inputs);
		auto setupLambda = state.SetupCallback;
		auto& view = *inputs.TargetView;
		AddDrawScreenPass(
			renderGraph, MoveTemp(event),
			FScreenPassViewInfo{ *inputs.TargetView },
			inputs.OutputViewportData,
			inputs.InputViewportData,
			FScreenPassPipelineState{
				shaderV, shaderP,
				state.BlendState, state.DepthStencilState, state.StencilRef
			},
			paramStruct,
			EScreenPassDrawFlags::AllowHMDHiddenAreaMask,
			[materialProxy, materialF, &view, shaderV, shaderP, setupLambda = MoveTemp(setupLambda)]
				(FRHICommandList& cmds)
			{
				setupLambda(cmds, shaderV, shaderP, materialProxy, materialF, view);
			}
		);
	}
	//Sets up a Screen-Space render pass, with a vertex and pixel shader using a post-process Material.
	//In most cases you can use 'EGP::FScreenSpaceRenderVS' for your vertex shader.
	//
	//Both shaders are likely to have different parameter structs, so
	//    you should keep both of them inside your main parameter struct
	//    and pass us references to those inner structs as well.
	//For the default FScreenSpaceRenderVS, use 'params->ScreenSpacePassData'.
	//
	//No attempt is made to respect the Material's blend mode, stencil mode, etc;
	//    you are responsible for that if it's something you care about.
	//
	//See `AddPostProcessMaterialPass()` for sample engine code.
	template<typename TVertexShader, typename TPixelShader, typename TPassParams>
	void AddScreenSpaceRenderPass(FRDGBuilder& renderGraph, FRDGEventName&& event,
								  const FScreenSpacePassMaterialInputs& inputs,
								  const FScreenSpacePassRenderState& state,
								  TPassParams* paramStruct,
								  const UMaterialInterface* material,
								  typename TVertexShader::FParameters* paramStructInnerVS,
								  typename TPixelShader::FParameters* paramStructInnerPS)
	{
		auto defaultSetupFn = [paramStructInnerVS, paramStructInnerPS]
							     (FRHICommandList& cmds,
							      TShaderRef<TVertexShader> shaderV,
							      TShaderRef<TPixelShader> shaderP,
							      const FMaterialRenderProxy* matProxy, const FMaterial* mat,
							      const FViewInfo& view)
		{
			SetShaderParametersMixedVS(cmds, shaderV, *paramStructInnerVS, matProxy, *mat, view);
			SetShaderParametersMixedPS(cmds, shaderP, *paramStructInnerPS, matProxy, *mat, view);
		};

		AddScreenSpaceRenderPass<TVertexShader, TPixelShader, TPassParams, decltype(defaultSetupFn)>(
			renderGraph, MoveTemp(event), inputs,
			TScreenSpacePassRenderState<decltype(defaultSetupFn)>{
				MoveTemp(defaultSetupFn),
				state.BlendState, state.DepthStencilState, state.StencilRef,
				state.PermutationIdVS, state.PermutationIdPS
			},
			paramStruct, material
		);
	}

	//Sets up a screen-space compute pass, using a post-process Material and your compute shader.
	template<typename TComputeShader, typename TPassParams, typename SetupFn>
	void AddScreenSpaceComputePass(FRDGBuilder& renderGraph, FRDGEventName&& event,
								   const FScreenSpacePassMaterialInputs& inputs,
								   const TScreenSpacePassComputeState<SetupFn>& state,
								   TPassParams* paramStruct, const UMaterialInterface* material)
	{
		
		static_assert(std::is_base_of_v<FScreenSpaceShader, TComputeShader>,
					  "Your Screen-Space pass compute shader must inherit from FScreenSpaceShader!");
		check(IsInRenderingThread());
		check(inputs.TargetView);

		//Compile the shaders against the Material.
		FMaterialShaderTypes types;
		types.AddShaderType<TComputeShader>(state.PermutationID);
		auto foundShaders = EGP::FindMaterialShaders_RenderThread(
			material, types,
			{ MD_PostProcess, inputs.TargetView->FeatureLevel }
		);
		check(foundShaders);

		//Extract the shader and material proxy.
		auto* materialProxy = foundShaders->MaterialProxy;
		auto* materialF = foundShaders->Material;
		TShaderRef<TComputeShader> shaderC;
		ensure(foundShaders->Shaders.TryGetComputeShader(shaderC));

		//Run the pass.
		impl::FillScreenSpaceMaterialParams(renderGraph, &paramStruct->ScreenSpacePassData, materialF, inputs);
		auto setupCallback = state.SetupCallback;
		const auto& view = *inputs.TargetView;
		ERDGPassFlags flags = state.UseAsyncCompute ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute;
		//The usual helper function for compute dispatch, FComputeShaderUtils::AddPass,
		//    is made for Global shaders and does not handle Material shaders correctly.
		//Therefore we have to do it all manually :(
		if (state.GroupCount.template IsType<FIntVector3>())
		{
			auto groupCount = state.GroupCount.template Get<FIntVector3>();
			renderGraph.AddPass(MoveTemp(event), paramStruct, flags,
							    [groupCount, shaderC, materialF, materialProxy, &view,
							    			   setupCallback = MoveTemp(setupCallback)]
									(FRHICommandList& cmds) {
				FRHIComputeShader* shaderRHI = shaderC.GetComputeShader();
				SetComputePipelineState(cmds, shaderRHI);
							    	
				setupCallback(groupCount, cmds, shaderC, materialProxy, materialF, view);
				cmds.DispatchComputeShader(groupCount.X, groupCount.Y, groupCount.Z);
				UnsetShaderUAVs(cmds, shaderC, shaderRHI);
		    });
		}
		else if (state.GroupCount.template IsType<TTuple<FRDGBufferRef, uint32>>())
		{
			auto indirectArgs = state.GroupCount.template Get<TTuple<FRDGBufferRef, uint32>>();
			renderGraph.AddPass(MoveTemp(event), paramStruct, flags,
							    [indirectArgs, shaderC, materialF, materialProxy, &view,
							    		       setupCallback = MoveTemp(setupCallback)]
									(FRHICommandList& cmds) {
				//The RDG doesn't know that we truly use the indirect dispatch buffer
				//    because it gets sent directly into a command-list,
				//    so we need to explicitly tell it in order to avoid warnings (and possibly out-of-order scheduling?).
				indirectArgs.Key->MarkResourceAsUsed();
				FComputeShaderUtils::ValidateIndirectArgsBuffer(indirectArgs.Key, indirectArgs.Value);
							    	
				FRHIComputeShader* shaderRHI = shaderC.GetComputeShader();
				SetComputePipelineState(cmds, shaderRHI);
							    	
				setupCallback(NullOpt, cmds, shaderC, materialProxy, materialF, view);
				cmds.DispatchIndirectComputeShader(indirectArgs.Key->GetIndirectRHICallBuffer(), indirectArgs.Value);
				UnsetShaderUAVs(cmds, shaderC, shaderRHI);
		    });
		}
		else if (state.GroupCount.template IsType<FRDGDispatchGroupCountCallback*>())
		{
			auto groupCountLambda = MoveTemp(*state.GroupCount.template Get<FRDGDispatchGroupCountCallback*>());
			renderGraph.AddPass(MoveTemp(event), paramStruct, flags,
							    [groupCountLambda = MoveTemp(groupCountLambda),
							    			   shaderC, materialF, materialProxy, &view,
							    			   setupCallback = MoveTemp(setupCallback)]
							        (FRHICommandList& cmds) {
				FRHIComputeShader* shaderRHI = shaderC.GetComputeShader();
				SetComputePipelineState(cmds, shaderRHI);

				FIntVector3 groupCount = groupCountLambda();
				setupCallback(groupCount, cmds, shaderC, materialProxy, materialF, view);
				cmds.DispatchComputeShader(groupCount.X, groupCount.Y, groupCount.Z);
				UnsetShaderUAVs(cmds, shaderC, shaderRHI);
		    });
		}
		else
		{
			//Unhandled case!
			check(false);
		}
	}
	//Sets up a screen-space compute pass, using a post-process Material and your compute shader.
	template<typename TComputeShader, typename TPassParams>
	void AddScreenSpaceComputePass(FRDGBuilder& renderGraph, FRDGEventName&& event,
								   const FScreenSpacePassMaterialInputs& inputs,
								   const FScreenSpacePassComputeState& state,
								   TPassParams* paramStruct, const UMaterialInterface* material)
	{
		auto defaultSetupFn = [paramStruct]
								 (FRHICommandList& cmds,
								  TShaderRef<TComputeShader> shaderC,
								  const FMaterialRenderProxy* matProxy, const FMaterial* mat,
								  const FViewInfo& view)
		{
			SetShaderParametersMixedCS(cmds, shaderC, *paramStruct, matProxy, *mat, view);
		};

		AddScreenSpaceMaterialPass<TComputeShader, TPassParams, decltype(defaultSetupFn)>(
			renderGraph, MoveTemp(event), inputs,
			TScreenSpacePassComputeState<decltype(defaultSetupFn)>{
				MoveTemp(defaultSetupFn),
				state.GroupCount, state.PermutationID, state.UseAsyncCompute
			},
			paramStruct, material
		);
	}
}