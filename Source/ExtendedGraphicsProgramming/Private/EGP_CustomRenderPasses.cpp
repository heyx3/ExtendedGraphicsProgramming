#include "EGP_CustomRenderPasses.h"

#include "Engine/TextureRenderTarget.h"
#include "SceneViewExtensionContext.h"
#include "Algo/AllOf.h"


bool U_EGP_ViewFilter::ShouldRenderFor(const FViewport* viewport) const
{
	bool rThread = IsInRenderingThread(),
		 gThread = IsInGameThread();
	check(rThread || gThread);
	
	return !ExcludeAll &&
		     ((rThread && byViewport_RT.IsAllowed(viewport)) ||
		      (gThread && byViewport_GT.IsAllowed(viewport)));
}
bool U_EGP_ViewFilter::ShouldRenderFor(const FSceneInterface* scene) const
{
	bool rThread = IsInRenderingThread(),
		 gThread = IsInGameThread();
	check(rThread || gThread);
	
	return !ExcludeAll &&
		     ((rThread && byScene_RT.IsAllowed(scene)) ||
		      (gThread && byScene_GT.IsAllowed(scene)));
}
bool U_EGP_ViewFilter::ShouldRenderFor(const FSceneViewExtensionContext& sveContext) const
{
	return ShouldRenderFor(sveContext.Scene) &&
		   ShouldRenderFor(sveContext.Viewport);
}
bool U_EGP_ViewFilter::ShouldRenderFor(const FSceneViewFamily& viewFamily) const
{
	bool rThread = IsInRenderingThread(),
		 gThread = IsInGameThread();
	check(rThread || gThread);
	
	return !ExcludeAll &&
		     ((rThread && byScene_RT.IsAllowed(viewFamily.Scene)) ||
		      (gThread && byScene_GT.IsAllowed(viewFamily.Scene))) &&
		     ((rThread && byRenderTarget_RT.IsAllowed(viewFamily.RenderTarget)) ||
		      (gThread && byRenderTarget_GT.IsAllowed(viewFamily.RenderTarget)));
}
bool U_EGP_ViewFilter::ShouldRenderFor(const FSceneView& view) const
{
	bool rThread = IsInRenderingThread(),
		 gThread = IsInGameThread();
	check(rThread || gThread);
	
	return ShouldRenderFor(*view.Family) &&
		   ((rThread && byPlayerIndex_RT.IsAllowed(view.PlayerIndex)) ||
		   	(gThread && byPlayerIndex_GT.IsAllowed(view.PlayerIndex))) &&
		   ((rThread && byViewActor_RT.IsAllowed(view.ViewActor)) ||
		   	(gThread && byViewActor_GT.IsAllowed(view.ViewActor)));
}

void U_EGP_ViewFilter::FilterByRenderTarget(UTextureRenderTarget* rt, bool isWhitelist)
{
	check(IsInGameThread());
	
	if (rt == nullptr)
	{
		const FRenderTarget* nullRT = nullptr;
		UpdateFilterList(byRenderTarget_GT, byRenderTarget_RT, nullRT, true, isWhitelist);
	}
	else
	{
		auto* resource = rt->GameThread_GetRenderTargetResource();
		//Don't interpret an uninitialized resource as a reference to all null render-targets!
		if (resource != nullptr)
			FilterByRenderTarget(resource, isWhitelist);
	}
}
void U_EGP_ViewFilter::RemoveByRenderTarget(UTextureRenderTarget* rt)
{
	check(IsInGameThread());
	
	if (rt == nullptr)
	{
		const FRenderTarget* nullRT = nullptr;
		UpdateFilterList(byRenderTarget_GT, byRenderTarget_RT, nullRT, true, false);
	}
	else
	{
		auto* resource = rt->GameThread_GetRenderTargetResource();
		//Don't interpret an uninitialized resource as a reference to all null render-targets!
		if (resource != nullptr)
			RemoveByRenderTarget(resource);
	}
}


U_EGP_RenderPassComponent::U_EGP_RenderPassComponent()
	: renderThreadProxy(MakeShared<EGP::CustomRenderPasses::ProxyData_t>())
{
	PrimaryComponentTick.bCanEverTick = true;
}
void U_EGP_RenderPassComponent::BeginPlay()
{
	Super::BeginPlay();

	auto* world = GetWorld();
	auto* subsystem = (IsValid(world)) ? world->GetSubsystem<U_EGP_RenderPassSubsystem>() : nullptr;
	auto* pass = IsValid(subsystem) ? subsystem->GetPass(GetPassType(), true) : nullptr;
	if (IsValid(pass))
		pass->RegisterPassComponent(this);
	else
		UE_LOG(LogEGP, Error,
			   TEXT("%s component created but there's no world/subsystem for custom render passes! No custom rendering can happen"),
			   *GetName());
}
void U_EGP_RenderPassComponent::EndPlay(const EEndPlayReason::Type reason)
{
	auto* world = GetWorld();
	auto* subsystem = (IsValid(world)) ? world->GetSubsystem<U_EGP_RenderPassSubsystem>() : nullptr;
	auto* pass = IsValid(subsystem) ? subsystem->GetPass(GetPassType(), true) : nullptr;
	if (IsValid(pass))
		pass->UnregisterPassComponent(this);
	
	DestructProxyData_GameThread();
	
	Super::EndPlay(reason);
}
void U_EGP_RenderPassComponent::TickComponent(float deltaSeconds, ELevelTick, FActorComponentTickFunction*)
{
	//Update the render-thread references.
	//TODO: Double-check that this actually works as intended (change proxy data during play and see that it updates).
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("EGP.UpdateCustomRenderProxy %s"), *GetName()));
		EGP::CustomRenderPasses::ProxyData_t newProxy;
		ConstructProxyData_GameThread(newProxy);
		
		auto renderThreadSharedPtr = renderThreadProxy;
		auto* targetPtr = &renderThreadTarget;
		auto* newTarget = Cast<UPrimitiveComponent>(GetAttachParent());
		ENQUEUE_RENDER_COMMAND(CopyCustomPassProxy)([renderThreadSharedPtr, targetPtr, newProxy, newTarget](FRHICommandListImmediate&)
		{
			*renderThreadSharedPtr = newProxy;
			*targetPtr = newTarget;
		});
	}
}

U_EGP_RenderPass::U_EGP_RenderPass()
	: ViewFilter(CreateDefaultSubobject<U_EGP_ViewFilter>(TEXT("ViewFilter")))
{
	
}
inline void U_EGP_RenderPass::Tick_GameThread(UWorld& thisWorld, float deltaSeconds)
{
	//Collect the components to use in the pass this frame and send it to the render thread.
	//Try to avoid heap usage, but also avoid coming up with a complex memory-pooling solution for this array,
	//    by assuming there are at most N components.
	constexpr uint32 StackCount = 256;
	TArray<U_EGP_RenderPassComponent*, TInlineAllocator<StackCount>> components;
	for (const auto& c : Components_GameThread)
		if (IsValid(c))
			components.Add(c);
	if (!warnedAboutArrayHeapUsage && components.Num() > StackCount)
	{
		UE_LOG(LogEGP, Warning,
			   TEXT("Custom render pass '%s' can only hold %i components "
					  "before some heap usage happens every frame. We are now at %i components!"),
			   *GetName(), StackCount, components.Num());
		warnedAboutArrayHeapUsage = true;
	}

	//Submit the proxy data and schedule a render-thread tick.
	auto* _this = this;
	const auto* scene = thisWorld.Scene;
	ENQUEUE_RENDER_COMMAND(UpdateCustomRenderPassProxies)([_this, scene, deltaSeconds, components = MoveTemp(components)](FRHICommandListImmediate&)
	{
		_this->ComponentProxies_RenderThread.Reset();
		for (const auto& c : components)
		{
			const auto& proxy = c->GetProxy_RenderThread();
			if (!_this->warnedAboutProxyHeapUsage && proxy.Num() > EGP::CustomRenderPasses::MaxInlineProxyByteSize)
			{
				UE_LOG(LogEGP, Warning,
					   TEXT("Render-thread proxy for custom render pass '%s' exceeds %i bytes (%i), "
							   "meaning it is allocated on the heap instead of the stack, "
							   "several times per frame per component! "
							   "Consider replacing the struct with a pooled memory pointer to avoid a performance hit."),
					   *_this->GetName(), EGP::CustomRenderPasses::MaxInlineProxyByteSize, proxy.Num());
				_this->warnedAboutProxyHeapUsage = true;
			}
			
			_this->ComponentProxies_RenderThread.Add(c, proxy);
		}

		_this->Tick_RenderThread(*scene, deltaSeconds);
	});
}
inline void U_EGP_RenderPass::Tick_RenderThread(const FSceneInterface& thisScene, float gameThreadDeltaSeconds)
{
	
}
void U_EGP_RenderPass::RegisterPassComponent(U_EGP_RenderPassComponent* component)
{
	check(IsInGameThread());
	Components_GameThread.Add(component);
}
void U_EGP_RenderPass::UnregisterPassComponent(U_EGP_RenderPassComponent* component)
{
	check(IsInGameThread());
	Components_GameThread.Remove(component);
}


void U_EGP_RenderPassSubsystem::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);
	
	auto* world = GetWorld();
	check(world);

	//Put the passes in a buffer and then tick with that buffer,
	//    in case passes try to disconnect themselves and invalidate the 'passes' collection.
	check(passBuffer.IsEmpty());
	for (const auto& [type, pass] : passes)
		passBuffer.Add(pass);
	for (auto pass : passBuffer)
		pass->Tick_GameThread(*world, deltaSeconds);
	passBuffer.Empty();
}
void U_EGP_RenderPassSubsystem::BeginDestroy()
{
	Super::BeginDestroy();

	//Clean up all render passes.
	//Once we get to 'FinishDestroy()' we'll hang until these jobs are completed.
	isCurrentlyDying = true;
	//When calling DestroyPass internally, the 'passes' collection is left alone so that this loop is safe.
	for (const auto& type : passes)
		DestroyPass_Impl_GameThread(type.Value->GetClass(), false);
	passes.Empty();
}
bool U_EGP_RenderPassSubsystem::IsReadyForFinishDestroy()
{
	return Algo::AllOf(
		dyingPassFences,
		[&](const auto& kvp) { return kvp.Value->IsFenceComplete(); }
	);
}
void U_EGP_RenderPassSubsystem::FinishDestroy()
{
	dyingPassFences.Empty();
	
	Super::FinishDestroy();
}

U_EGP_RenderPass* U_EGP_RenderPassSubsystem::GetPass(TSubclassOf<U_EGP_RenderPass> type, bool createIfNeeded)
{
	check(IsInGameThread());
	check(GetWorld());

	//See if the pass already exists.
	auto found = passes.Find(type);
	if (found != nullptr)
		return *found;
	if (!createIfNeeded)
		return nullptr;

	//Create a new render pass.
	auto* newPass = NewObject<U_EGP_RenderPass>(this, type, NAME_None, RF_Transient);
	newPass->Subsystem = this;
	passes.Add(type, newPass);
	newPass->SceneViewExtension = newPass->InitThisPass_GameThread(*GetWorld());

	//Go to the render thread to finish initialization.
	ENQUEUE_RENDER_COMMAND(InitPass)([newPass](FRHICommandListImmediate&) {
		newPass->InitThisPass_RenderThread();
	});
	
	return newPass;
}
bool U_EGP_RenderPassSubsystem::DestroyPass_Impl_GameThread(TSubclassOf<U_EGP_RenderPass> type, bool isExternalCall)
{
	check(IsInGameThread());
	
	auto found = passes.Find(type);
	if (found == nullptr)
		return false;
	auto* pass = *found;

	//If a user tried to kill the pass during subsystem cleanup, then this call is redundant.
	//If this pass is being killed internally due to the subsystem dying, then we leave the 'passes' collection alone.
	if (isCurrentlyDying)
	{
		if (!isExternalCall)
			return true;
	}
	//Otherwise the user killed this pass during gameplay and it's safe to immediately remove it from the main list.
	else
	{
		check(isExternalCall);
		passes.Remove(type);
	}

	//Set up a fence so we know when this pass is destroyed.
	auto* fence = dyingPassFences.Add(
		TStrongObjectPtr<U_EGP_RenderPass>{ pass },
		std::make_unique<FRenderCommandFence>()
	).get();
	
	//Go to the render thread to finish cleanup.
	auto* world = GetWorld();
	check(world);
	ENQUEUE_RENDER_COMMAND(CleanupPass)([world, pass, isExternalCall](FRHICommandListImmediate& cmds)
	{
		pass->CleanupThisPass_RenderThread(*world, isExternalCall);
	});
	fence->BeginFence();
	
	return true;
}