#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "Runtime/Renderer/Private/SceneRendering.h"

#include "ExtendedGraphicsProgramming.h"

#include "EGP_CustomRenderPasses.generated.h"


class U_EGP_RenderPassComponent;
class F_EGP_RenderPassSceneViewExtension;
class U_EGP_RenderPass;
class U_EGP_RenderPassSubsystem;


#pragma region Filtering views

namespace EGP
{
	//A whitelist OR blacklist of some objects.
	//You can pick which on construction, or after adding your first object.
	template<typename T, typename CompareFn = std::equal_to<T>>
	class FilterList
	{
	public:
		FilterList(TOptional<bool> _isWhitelist = NullOpt)
			: isWhitelist(_isWhitelist) { }
		FilterList(CompareFn comparator, TOptional<bool> _isWhitelist = NullOpt)
			: Comparator(std::move(comparator)), isWhitelist(_isWhitelist) { }

		//If this list hasn't been configured (not whitelist or blacklist), then it allows everything.
		bool IsAllowed(const T& t) const
		{
			if (!isWhitelist.IsSet())
				return true;

			bool isListed = false;
			for (const T& element : elements)
			{
				if (Comparator(element, t))
				{
					isListed = true;
					break;
				}
			}
			
			return isListed == *isWhitelist;
		}

		//Returns 'true' if whitelist, 'false' if blacklist, and 'null' if not configured yet.
		TOptional<bool> IsAWhitelist() const { return isWhitelist; }
		
		void AddBlacklisted(const T& t)
		{
			check(isWhitelist != true);
			isWhitelist = false;
			elements.Add(t);
		}
		void AddWhitelisted(const T& t)
		{
			check(isWhitelist != false);
			isWhitelist = true;
			elements.Add(t);
		}
		void Remove(const T& t)
		{
			elements.RemoveAll([&](const T& t2)
			{
				return Comparator(t, t2);
			});
		}

		//Updates this filter to be a blacklist or whitelist, without changing its elements.
		void Configure(bool _isWhitelist) { isWhitelist = _isWhitelist; }
		
		void Clear(TOptional<bool> isNowWhitelist = NullOpt)
		{
			elements.Empty();
			isWhitelist = isNowWhitelist;
		}

		size_t GetSize() const { return elements.Num(); }

		CompareFn Comparator;
	private:
		TOptional<bool> isWhitelist;
		TArray<T> elements;
	};
}


//A set of blacklists and/or whitelists for render views.
//Useful for custom passes to specify when they are drawn.
//
//The filter data cannot be directly read; it's write-only
//    to make sure filters are accessed in a thread-safe manner.
UCLASS(BlueprintType)
class EXTENDEDGRAPHICSPROGRAMMING_API U_EGP_ViewFilter : public UObject
{
	GENERATED_BODY()
public:

	//If true, then no viewports pass the filter.
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool ExcludeAll = false;
	
	//Adds the given viewport actor to a whitelist or blacklist.
	//Note that you can't do both whitelisting *and* blacklisting!
	//
	//The actors that get tested against this filter are usually PlayerControllers
	//    or the target of their PlayerCameraManager if that exists.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Actor")
	void FilterByActor(const AActor* actor, bool isWhitelist = true) { UpdateFilterList(byViewActor_GT, byViewActor_RT, { actor }, true, isWhitelist); }
	//Removes the given viewport actor from the filter list
	//    (enabling it if using a blacklist, or disabling it if using a whitelist).
	//
	//Does nothing if the actor isn't in the list.
	//
	//The actors that get tested against this filter are usually PlayerControllers
	//    or the target of their PlayerCameraManager if that exists.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Actor")
	void RemoveByActor(const AActor* actor) { UpdateFilterList(byViewActor_GT, byViewActor_RT, { actor }, false, false); }
	//Sets the actor filter to be a blacklist or whitelist.
	//
	//This can also be done automatically when adding the first element to the filter.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Actor")
	void ConfigureByActor(bool isWhitelist) { ConfigureFilterList(byViewActor_GT, byViewActor_RT, isWhitelist); }
	//Clears all filtering by viewport actor, including the question of whether it's a whitelist or blacklist.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Actor")
	void ClearsByActor() { ClearFilterList(byViewActor_GT, byViewActor_RT); }

	//Adds the given player controller index to a whitelist or blacklist.
	//Note that you can't do both whitelisting *and* blacklisting!
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Player Index")
	void FilterByPlayerIdx(int playerIdx, bool isWhitelist = true) { UpdateFilterList(byPlayerIndex_GT, byPlayerIndex_RT, playerIdx, true, isWhitelist); }
	//Removes the given player controller index from the filter list
	//    (enabling it if using a blacklist, or disabling it if using a whitelist).
	//
	//Does nothing if the index isn't in the list.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Player Index")
	void RemoveByPlayerIdx(int playerIdx) { UpdateFilterList(byPlayerIndex_GT, byPlayerIndex_RT, playerIdx, false, false); }
	//Sets the player-index filter to be a blacklist or whitelist.
	//
	//This can also be done automatically when adding the first element to the filter.
	//This can't be done after an element has been added.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Player Index")
	void ConfigureByPlayerIdx(bool isWhitelist) { ConfigureFilterList(byPlayerIndex_GT, byPlayerIndex_RT, isWhitelist); }
	//Clears all filtering by player index, including the question of whether it's a whitelist or blacklist.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Player Index")
	void ClearsByPlayerIdx() { ClearFilterList(byPlayerIndex_GT, byPlayerIndex_RT); }

	//Adds the given viewport to a whitelist or blacklist.
	//Note that you can't do both whitelisting *and* blacklisting!
	void FilterByViewport(const FViewport* viewport, bool isWhitelist = true) { UpdateFilterList(byViewport_GT, byViewport_RT, viewport, true, isWhitelist); }
	//Removes the give viewport from the filter list
	//    (enabling it if using a blacklist, or disabling it if using a whitelist).
	//
	//Does nothing if the index isn't in the list.
	void RemoveByViewport(const FViewport* viewport) { UpdateFilterList(byViewport_GT, byViewport_RT, viewport, false, false); }
	//Sets the viewport filter to be a blacklist or whitelist.
	//
	//This can also be done automatically when adding the first element to the filter.
	//This can't be done after an element has been added.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Viewport")
	void ConfigureByViewport(bool isWhitelist) { ConfigureFilterList(byViewport_GT, byViewport_RT, isWhitelist); }
	//Clears all filtering by viewport reference, including the question of whether it's a whitelist or blacklist.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Viewport")
	void ClearsByViewport() { ClearFilterList(byViewport_GT, byViewport_RT); }

	//Adds the given scene to a whitelist or blacklist.
	//Note that you can't do both whitelisting *and* blacklisting!
	void FilterByScene(const FSceneInterface* scene, bool isWhitelist = true) { UpdateFilterList(byScene_GT, byScene_RT, scene, true, isWhitelist); }
	//Removes the given scene from the filter list
	//    (enabling it if using a blacklist, or disabling it if using a whitelist).
	//
	//Does nothing if the index isn't in the list.
	void RemoveByScene(const FSceneInterface* scene) { UpdateFilterList(byScene_GT, byScene_RT, scene, false, false); }
	//Sets the viewport filter to be a blacklist or whitelist.
	//
	//This can also be done automatically when adding the first element to the filter.
	//This can't be done after an element has been added.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Viewport")
	void ConfigureByScene(bool isWhitelist) { ConfigureFilterList(byScene_GT, byScene_RT, isWhitelist); }
	//Clears all filtering by scene reference, including the question of whether it's a whitelist or blacklist.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Viewport")
	void ClearsByScene() { ClearFilterList(byScene_GT, byScene_RT); }

	//Adds the given render-target to a whitelist or blacklist.
	//Note that you can't do both whitelisting *and* blacklisting!
	void FilterByRenderTarget(const FRenderTarget* rt, bool isWhitelist = true) { UpdateFilterList(byRenderTarget_GT, byRenderTarget_RT, rt, true, isWhitelist); }
	//Adds the given render-target to a whitelist or blacklist.
	//Note that you can't do both whitelisting *and* blacklisting!
	//
	//Also note that this filter breaks if the render target's underlying proxy is recreated.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Render-Target")
	void FilterByRenderTarget(UTextureRenderTarget* rt, bool isWhitelist = true);
	//Removes the given scene from the filter list
	//    (enabling it if using a blacklist, or disabling it if using a whitelist).
	//
	//Does nothing if the index isn't in the list.
	void RemoveByRenderTarget(const FRenderTarget* rt) { UpdateFilterList(byRenderTarget_GT, byRenderTarget_RT, rt, false, false); }
	//Removes the given scene from the filter list
	//    (enabling it if using a blacklist, or disabling it if using a whitelist).
	//
	//Does nothing if the index isn't in the list.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Render-Target")
	void RemoveByRenderTarget(UTextureRenderTarget* rt);
	//Sets the render-target filter to be a blacklist or whitelist.
	//
	//This can also be done automatically when adding the first element to the filter.
	//This can't be done after an element has been added.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Render-Target")
	void ConfigureByRenderTarget(bool isWhitelist) { ConfigureFilterList(byRenderTarget_GT, byRenderTarget_RT, isWhitelist); }
	//Clears all filtering by render-target, including the question of whether it's a whitelist or blacklist.
	UFUNCTION(BlueprintCallable, Category="Viewport Filtering|Render-Target")
	void ClearsByRenderTarget() { ClearFilterList(byRenderTarget_GT, byRenderTarget_RT); }

	
	bool ShouldRenderFor(const FViewport* viewport) const;
	bool ShouldRenderFor(const FSceneInterface* scene) const;
	bool ShouldRenderFor(const FSceneViewExtensionContext& sveContext) const;
	bool ShouldRenderFor(const FSceneViewFamily& viewFamily) const;
	bool ShouldRenderFor(const FSceneView& view) const;
	
protected:

	//Keep a game-thread and render-thread copy of each filter.
	//This is needed because sometimes decisions (like SVE applicability) are made on the game-thread,
	//    but most render stuff is done on the render-thread.
	EGP::FilterList<const FRenderTarget*> byRenderTarget_GT, byRenderTarget_RT;
	EGP::FilterList<const FSceneInterface*> byScene_GT, byScene_RT;
	EGP::FilterList<const FViewport*> byViewport_GT, byViewport_RT;
	EGP::FilterList<TWeakObjectPtr<const AActor>> byViewActor_GT, byViewActor_RT;
	EGP::FilterList<int> byPlayerIndex_GT, byPlayerIndex_RT;

	//Modifies the given filter list.
	//Callable from anywhere.
	template<typename T>
	void UpdateFilterList(EGP::FilterList<T>& _filter_GT, EGP::FilterList<T>& _filter_RT,
						  T element, bool isAdding, bool isAddingAsWhitelist)
	{
		//References captured by copy do actually copy the object, so we need pointers. 
		auto* filterGT = &_filter_GT;
		auto* filterRT = &_filter_RT;

		auto updateFilter = [element, isAdding, isAddingAsWhitelist](EGP::FilterList<T>& filter)
		{
			//You can't add a whitelisted object to a blacklist, and vice versa.
			if (isAdding && filter.IsAWhitelist().IsSet() && filter.IsAWhitelist() != isAddingAsWhitelist)
			{
				UE_LOG(LogEGP, Error,
					   TEXT("Tried to add a %s element to a %s view-filter! The operation failed."),
					   isAddingAsWhitelist ? TEXT("whitelisted") : TEXT("blacklisted"),
					   filter.IsAWhitelist() ? TEXT("whitelist") :  TEXT("blacklist")
				);
				return;
			}
			
			if (isAdding)
				if (isAddingAsWhitelist)
					filter.AddWhitelisted(element);
				else
					filter.AddBlacklisted(element);
				else
					filter.Remove(element);
		};
		
		ENQUEUE_RENDER_COMMAND(UpdateFilter)([filterRT, updateFilter](FRHICommandListImmediate&)
		{
			updateFilter(*filterRT);
		});
		
		if (IsInGameThread())
		{
			updateFilter(*filterGT);
		}
		else AsyncTask(ENamedThreads::GameThread, [filterGT, updateFilter]()
		{
			updateFilter(*filterGT);
		});
	}
	
	//Clears the given filter list.
	//Callable from anywhere.
	template<typename T>
	void ClearFilterList(EGP::FilterList<T>& _filter_GT, EGP::FilterList<T>& _filter_RT)
	{
		//References captured by copy do actually copy the object, so we need pointers. 
		auto* filterGT = &_filter_GT;
		auto* filterRT = &_filter_RT;

		auto updateFilter = [](EGP::FilterList<T>& filter) { filter.Clear(); };

		ENQUEUE_RENDER_COMMAND(ClearFilter)([filterRT, updateFilter](FRHICommandListImmediate&)
		{
			updateFilter(*filterRT);
		});

		if (IsInGameThread())
		{
			updateFilter(*filterGT);
		}
		else AsyncTask(ENamedThreads::GameThread, [filterGT, updateFilter]()
		{
			updateFilter(*filterGT);
		});
	}
	
	//Configures the given filter list.
	//Callable from anywhere.
	template<typename T>
	void ConfigureFilterList(EGP::FilterList<T>& _filter_GT, EGP::FilterList<T>& _filter_RT,
				   		     bool makeWhitelist)
	{
		//References captured by copy do actually copy the object, so we need pointers. 
		auto* filterGT = &_filter_GT;
		auto* filterRT = &_filter_RT;

		auto updateFilter = [makeWhitelist](EGP::FilterList<T>& filter) { filter.Configure(makeWhitelist); };

		ENQUEUE_RENDER_COMMAND(ClearFilter)([filterRT, updateFilter](FRHICommandListImmediate&)
		{
			updateFilter(*filterRT);
		});

		if (IsInGameThread())
		{
			updateFilter(*filterGT);
		}
		else AsyncTask(ENamedThreads::GameThread, [filterGT, updateFilter]()
		{
			updateFilter(*filterGT);
		});
	}
};



#pragma endregion

namespace EGP { namespace CustomRenderPasses
{
	//We don't know what kind of proxy data each render pass uses, and templates don't play well with Unreal,
	//    so we allocate a constant number of bytes per proxy
	//    and let the user worry about pooling heap memory if they truly need more than that.
	//
	//You can also `#define EGP_CUSTOMRENDERPASS_MAXPROXYBYTESIZE 1024` or whatever size you want,
	//    but this forces all custom passes in the project to use the same size.
	static constexpr uint32 MaxInlineProxyByteSize =
		#if defined(EGP_CUSTOMRENDERPASS_MAXPROXYBYTESIZE)
			EGP_CUSTOMRENDERPASS_MAXPROXYBYTESIZE
		#else
			512
		#endif
	;

	//A byte array holding a render proxy, which can stay on the stack as long as it is less than N bytes.
	//Otherwise it will be moved to the heap.
	using ProxyData_t = TArray<std::byte, TInlineAllocator<MaxInlineProxyByteSize>>;
} }

#pragma region Component

//Marks its parent component as being part of some custom render pass.
UCLASS(Abstract, BlueprintType, Blueprintable, Placeable, meta=(BlueprintSpawnableComponent))
class EXTENDEDGRAPHICSPROGRAMMING_API U_EGP_RenderPassComponent : public USceneComponent
{
	GENERATED_BODY()
public:

	//The component that will be rendered in the custom pass.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Custom Render Pass", meta=(AllowAnyActor))
	UPrimitiveComponent* Target = nullptr;

	//If false, this component's target is not used in the custom render pass.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Custom Render Pass")
	bool EnabledInCustomPass = true;


	U_EGP_RenderPassComponent();
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type reason) override;
	virtual void TickComponent(float deltaSeconds, ELevelTick tickType, FActorComponentTickFunction* thisTickFn) override;

	//Reports the kind of render pass this component is meant to be a part of.
	virtual TSubclassOf<U_EGP_RenderPass> GetPassType() const
		PURE_VIRTUAL(UCustomRenderPassComponent::GetPassType, return nullptr; )
	//Converts this component's data into a POD struct for the render-thread, constructing it in the given byte buffer.
	//You can usually implement this by just calling through to 'ImplConstructProxyData_GameThread()'.
	virtual void ConstructProxyData_GameThread(EGP::CustomRenderPasses::ProxyData_t& output) const
		PURE_VIRTUAL(UCustomRenderPassComponent::ConstructProxyData_GameThread, )
	//Destroys the POD struct representing this component.
	//You must implement this by calling through to 'ImplDestructProxyData_GameThread()'.
	virtual void DestructProxyData_GameThread() const
		PURE_VIRTUAL(UCustomRenderPassComponent::DestructProxyData_GameThread, )

	//Grabs the most recent version of the render-thread data struct representing this component.
	template<typename POD>
	const POD& GetProxy_RenderThread() const { return reinterpret_cast<const POD&>(GetProxy_RenderThread()); }
	const EGP::CustomRenderPasses::ProxyData_t& GetProxy_RenderThread() const { check(IsInRenderingThread()); return *renderThreadProxy.Get(); }

	UPrimitiveComponent* GetTarget_RenderThread() const { check(IsInRenderingThread()); return renderThreadTarget.Get(); }

protected:

	//Implements the most common behavior for `CreateProxyData_RenderThread()`.
	template<typename POD>
	static void ImplConstructProxyData_GameThread(EGP::CustomRenderPasses::ProxyData_t& output, POD&& proxyData)
	{
		output.SetNumUninitialized(sizeof(POD));
		auto* proxyOutput = reinterpret_cast<POD*>(output.GetData());

		new (proxyOutput) POD(proxyData);
	}
	//Implements the most common behavior for `DestructProxyData_RenderThread()`.
	template<typename POD>
	void ImplDestructProxyData_GameThread() const
	{
		auto proxySharedPtr = renderThreadProxy;
		ENQUEUE_RENDER_COMMAND(DestructCustomRenderPassComponentProxy)([proxySharedPtr](FRHICommandListImmediate&)
		{
			//If the proxy was never created, there's nothing to do.
			if (proxySharedPtr->Num() == 0)
				return;
			
			check(proxySharedPtr->Num() == sizeof(POD));
			auto* proxy = reinterpret_cast<POD*>(proxySharedPtr->GetData());
			
			proxy->~POD();
		});
	}

private:

	TSharedPtr<EGP::CustomRenderPasses::ProxyData_t, ESPMode::ThreadSafe> renderThreadProxy;
	TWeakObjectPtr<UPrimitiveComponent> renderThreadTarget;
};

//If your render pass component can set up its POD proxy by simply calling its constructor,
//    then you can use this macro to implement the component's proxy virtual functions.
#define EGP_PASS_COMPONENT_SIMPLE_PROXY_IMPL(TProxy, createExpr) \
	virtual void ConstructProxyData_GameThread(EGP::CustomRenderPasses::ProxyData_t& output) const override { \
		TProxy localProxyInstance = (createExpr); \
		ImplConstructProxyData_GameThread<TProxy>(output, MoveTemp(localProxyInstance)); \
	} \
	virtual void DestructProxyData_GameThread() const override { \
		ImplDestructProxyData_GameThread<TProxy>(); \
	}

#pragma endregion

#pragma region Subsystem

//Manages all custom render passes.
//Creates them lazily on-demand, either when the first associated CustomRenderPassComponent is created
//    or if you explicitly start the pass.
//
//Unless mentioned otherwise, all functions are game-thread only.
UCLASS(Config=Scalability)
class EXTENDEDGRAPHICSPROGRAMMING_API U_EGP_RenderPassSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:

	//Gets the given type of pass, optionally creating it if needed.
	UFUNCTION(BlueprintCallable, meta=(DeterminesOutputType=type))
	U_EGP_RenderPass* GetPass(TSubclassOf<U_EGP_RenderPass> type, bool createIfNeeded);
	//Gets the given type of pass, optionally creating it if needed.
	template<typename TPassType>
	TPassType* GetPass(bool createIfNeeded) { return CastChecked<TPassType>(GetPass(TPassType::StaticClass())); }

	//Cleans up the given kind of render pass from this world.
	//Returns whether the pass existed in the first place.
	UFUNCTION(BlueprintCallable)
	bool DestroyPass_GameThread(TSubclassOf<U_EGP_RenderPass> type) { return DestroyPass_Impl_GameThread(type, true); }
	//Cleans up the given kind of render pass from this world.
	//Returns whether the pass existed in the first place.
	template<typename TPassType>
	bool DestroyPass() { return DestroyPass_GameThread(TPassType::StaticClass()); }

	virtual void Tick(float deltaSeconds) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCustomRenderPassSubsystem, STATGROUP_Tickables); }
	
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual void FinishDestroy() override;

private:

	UPROPERTY(Transient)
	TMap<TSubclassOf<U_EGP_RenderPass>, U_EGP_RenderPass*> passes;

	bool DestroyPass_Impl_GameThread(TSubclassOf<U_EGP_RenderPass> type, bool isExternalCall);
	TMap<TStrongObjectPtr<U_EGP_RenderPass>, std::unique_ptr<FRenderCommandFence>> dyingPassFences;
	
	//Prevents user "destroy pass" calls from interfering with internal destruction logic.
	bool isCurrentlyDying = false;

	//Used for internal iterations over passes.
	TArray<TWeakObjectPtr<U_EGP_RenderPass>> passBuffer;
};

#pragma endregion

#pragma region Scene View Extension

//The non-templated base class of 'TCustomRenderPassSceneViewExtension'.
//Don't directly inherit from this.
class EXTENDEDGRAPHICSPROGRAMMING_API F_EGP_RenderPassSceneViewExtension : public FSceneViewExtensionBase
{
public:

	UWorld* const World;

	F_EGP_RenderPassSceneViewExtension(const FAutoRegister& r, UWorld* world)
		: FSceneViewExtensionBase(r),
		  World(world)
	{
		//Only render in scenes belonging to this world, and only while this SVE is still alive.
		FSceneViewExtensionIsActiveFunctor testWorld;
		testWorld.IsActiveFunction = [&](const ISceneViewExtension* e, const FSceneViewExtensionContext& c) -> TOptional<bool>
		{
			const auto* me = reinterpret_cast<const F_EGP_RenderPassSceneViewExtension*>(e);
			if (!me->stopAllRendering && (c.GetWorld() == me->World))
				return NullOpt;
			else
				return false;
		};
		IsActiveThisFrameFunctions.Add(testWorld);
	}

	//Immediately stops this SVE from running after the current Render Thread frame.
	//Called automatically when this SVE's World is dying,
	//    but you may call it manually on any thread at any time.
	void KillRendering() { stopAllRendering = true; }

	//These abstract functions should just be virtual, so we give them empty defaults for convenience.
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override { }
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override { }
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override { }

private:

	std::atomic_bool stopAllRendering = false;
};

//The object that generates draw calls for your custom pass.
template<typename PassType, typename ComponentType = void, typename PrimitiveProxyType = void>
class T_EGP_RenderPassSceneViewExtension : public F_EGP_RenderPassSceneViewExtension
{
	static_assert(std::is_base_of_v<U_EGP_RenderPassComponent, ComponentType> ||
				   std::is_same_v<void, ComponentType>,
				  "ComponentType must be a child of U_EGP_RenderPassComponent");
	static_assert(!std::is_same_v<U_EGP_RenderPassComponent, ComponentType>,
				  "ComponentType must be a *child* of U_EGP_RenderPassComponent");
	
	static_assert(std::is_base_of_v<U_EGP_RenderPass, PassType>, "PassType must be a child of U_EGP_RenderPass");
	static_assert(!std::is_same_v<U_EGP_RenderPass, PassType>, "PassType must be a *child* of U_EGP_RenderPass");
	
public:

	PassType* const Pass;

	T_EGP_RenderPassSceneViewExtension(const FAutoRegister& r, PassType* pass)
		: F_EGP_RenderPassSceneViewExtension(r, pass->GetWorld()),
		  Pass(pass)
	{
		//Only render in views that are accepted by the pass's filter.
		FSceneViewExtensionIsActiveFunctor testFilter;
		testFilter.IsActiveFunction = [&](const ISceneViewExtension* e, const FSceneViewExtensionContext& c) -> TOptional<bool>
		{
			const auto* me = reinterpret_cast<const T_EGP_RenderPassSceneViewExtension<PassType, ComponentType, PrimitiveProxyType>*>(e);
			const U_EGP_ViewFilter* filter = me->Pass->ViewFilter;

			if (filter->ShouldRenderFor(c))
				return NullOpt;
			else
				return false;
		};
		IsActiveThisFrameFunctions.Add(testFilter);
	}

	//Iterates over each renderable object for this custom pass and executes your lambda on it.
	//The lambda's signature is:
	//  `(const ComponentType&, const PrimitiveProxyType&,   const UPrimitiveComponent&, const FPrimitiveSceneProxy&) -> void`
	template<typename Lambda>
	void ForEachComponent_RenderThread(Lambda toDo)
	{
		check(IsInRenderingThread());
		if constexpr (std::is_same_v<ComponentType, void>)
		{
			checkf(false, TEXT("You called 'ForEachComponent_RenderThread(), in a scene-view extension "
							     "that doesn't use components!"));
		}
		else
		{
			for (const auto& [_component, proxyBytes] : Pass->GetComponentData_RenderThread())
			{
				//I'm not 100% sure how safe it is to use 'IsValid()' in this thread.
				if (!_component.IsValid())
					continue;

				//Ideally I'd expect the user's custom pass
				//    to grab its own pointer to each primitive component's render-proxy as desired
				//    and store it in the custom pass component's proxy struct, on 'WriteProxyData_RenderThread()'.
				//However in my experience that seems doomed to crash, and I can't for the life of me
				//    see any function or engine code sample to know when a proxy stored that way
				//    has been destroyed and recreated while I wasn't looking.
				//So instead I grab the render proxy on demand, directly from the primitive-component.
				//Primitive scene proxies are only changed on the render-thread so we should be safe from race conditions.
				auto* primitiveComponent = _component->GetTarget_RenderThread();
				
				if (primitiveComponent == nullptr || primitiveComponent->SceneProxy == nullptr)
					return;

				//Not sure if it's safe to use CastChecked on this thread, so just do a raw reinterpret_cast.
				auto* component = reinterpret_cast<const ComponentType*>(_component.Get());
				const auto& proxy = reinterpret_cast<const PrimitiveProxyType&>(proxyBytes);
				const auto* primitiveProxy = primitiveComponent->SceneProxy;

				toDo(*component, proxy,    *primitiveComponent, *primitiveProxy);
			}
		}
	}
};

#pragma endregion

#pragma region Per-view Data

//Some persistent, per-view resources for a custom render pass.
//Managed by a T_EGP_PerViewData<>.
struct F_EGP_ViewPersistentData
{
	//Child constructors must have these parameters, followed by any custom ones.
	F_EGP_ViewPersistentData(FRDGBuilder&, const FViewInfo&, const FIntRect& viewportSubset) { }
	
	//You must define how to resample your data as the user's resolution or screen-percentage changes.
	virtual void Resample(FRDGBuilder&, const FViewInfo&,
						  const FInt32Point& oldResolution,
						  const FInt32Point& newResolution,
						  const FInt32Point& oldToNewPixelOffset) = 0;

	
	//Automatic copies are considered an error.
	F_EGP_ViewPersistentData(const F_EGP_ViewPersistentData&) = delete;
	F_EGP_ViewPersistentData& operator=(const F_EGP_ViewPersistentData&) = delete;

	//Move operations and destructors are important.
	F_EGP_ViewPersistentData(F_EGP_ViewPersistentData&&) = default;
	F_EGP_ViewPersistentData& operator=(F_EGP_ViewPersistentData&&) = default;
	virtual ~F_EGP_ViewPersistentData() { }
};


//Non-templated base class of T_EGP_PerViewData, to expose some of its more general behavior.
struct F_EGP_PerViewData
{
	virtual void Tick() = 0;
	virtual ~F_EGP_PerViewData() { }
};

//Manages any persistent state for a render pass in a specific viewport.
//Meant to be owned by your U_EGP_RenderPass instance.
//All functions are meant to be called on the Render Thread only.
//
//TData must inherit from F_EGP_ViewPersistentData.
template<typename TData>
struct T_EGP_PerViewData final : public F_EGP_PerViewData
{
	static_assert(std::is_base_of_v<F_EGP_ViewPersistentData, TData> &&
				    !std::is_same_v<F_EGP_ViewPersistentData, TData>,
				  "TData must be a child of F_EGP_ViewPersistentData");
	
	//If a view goes this many frames without its data being accessed, it is automatically cleaned up.
	int CleanupFrameThreshold = 60;
	//If a view's ID is in this set, it is never eligible for being cleaned up.
	TSet<int> CleanupPreventionByViewID;
	
	//Should be called once per frame on the render thread.
	//Cleans up view data that hasn't been used in a while.
	virtual void Tick() override final 
	{
		check(IsInRenderingThread());

		viewIDBuffer.Empty();
		dataByViewID.GetKeys(viewIDBuffer);
		for (int viewID : viewIDBuffer)
		{
			//Don't advance the timestamp at all for views that are permanent.
			if (CleanupPreventionByViewID.Contains(viewID))
				continue;
			
			auto& data = dataByViewID[viewID];
			if (data.FramesSinceAccess > CleanupFrameThreshold)
				dataByViewID.Remove(viewID);
			else
				data.FramesSinceAccess += 1;
		}
	}

	//Gets the data for the given view, creating new data if none is registered.
	//When new data is created, the extra arguments you pass are forwarded to your data's constructor.
	//
	//The returned reference is invalidated as soon as you call Tick() or create data for a new view.
	template<typename... NewDataArgs>
	TData& DataForView(FRDGBuilder& graph, const FViewInfo& view,
					   NewDataArgs&&... constructorArgs)
	{
		check(IsInRenderingThread());
		
		int viewID = view.State->GetViewKey();

		//Get or create the asset.
		auto* data = dataByViewID.Find(viewID);
		if (data == nullptr)
		{
			data = &dataByViewID.Emplace(viewID, ViewData{
				TData{ graph, view, view.ViewRect,
						    Forward<NewDataArgs>(constructorArgs)... },
				view.ViewRect,
				view.GetFeatureLevel(),
				0
			});
		}

		//Update the timestamp.
		data->FramesSinceAccess = 0;

		//Resample the asset if needed.
		if (data->PixelSubset != view.ViewRect)
		{
			data->User.Resample(graph, view,
								data->PixelSubset.Size(), view.ViewRect.Size(),
								view.ViewRect.Min - data->PixelSubset.Min);
			data->PixelSubset = view.ViewRect;
		}

		return data->User;
	}
	//(note: no const version, because not being able to update the timestamp or resample makes it very dubiously useful)
	
	bool DoesDataExistForView(const FViewInfo& view) const
	{
		return dataByViewID.Contains(view.State->GetViewKey());
	}

	//Allows you to access each active per-view data instance.
	//The lambda signature should be '(int viewID, TData& data, ERHIFeatureLevel::Type featureLevel) -> void'.
	template<typename Lambda>
	void ForEachView(Lambda toDo)
	{
		for (auto& [id, data] : dataByViewID)
			toDo(int{ id }, data.User, ERHIFeatureLevel::Type{ data.FeatureLevel });
	}
	//Allows you to access each active per-view data instance.
	//The lambda signature should be '(int viewID, const TData& data, ERHIFeatureLevel::Type featureLevel) -> void'.
	template<typename Lambda>
	void ForEachView(Lambda toDo) const
	{
		for (const auto& [id, data] : dataByViewID)
			toDo(id, data.User, data.FeatureLevel);
	}
	
private:
	
	struct ViewData
	{
		TData User;
		FIntRect PixelSubset;
		ERHIFeatureLevel::Type FeatureLevel;
		int FramesSinceAccess = 0;
	};
	TMap<int, ViewData> dataByViewID;

	//Used inside Tick()
	TArray<int> viewIDBuffer;
};

#pragma endregion

#pragma region Pass object

//Base class for managing one custom render pass in one world.
//Is owned by a UCustomRenderPassSubsystem.
UCLASS(Abstract, BlueprintType, Config=Scalability)
class EXTENDEDGRAPHICSPROGRAMMING_API U_EGP_RenderPass : public UObject
{
	GENERATED_BODY()
public:
	
	U_EGP_RenderPass();

	virtual void Tick_GameThread(UWorld& thisWorld, float deltaSeconds);
	virtual void Tick_RenderThread(const FSceneInterface& thisScene, float gameThreadDeltaSeconds);

	const auto& GetComponentData_RenderThread() const { check(IsInRenderingThread()); return ComponentProxies_RenderThread; }

	//The filter settings, controlling which views use this render pass.
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	U_EGP_ViewFilter* const ViewFilter = nullptr;

	
protected:

	//The subsystem and component both manage this render pass's internal logic.
	friend U_EGP_RenderPassComponent;
	friend U_EGP_RenderPassSubsystem;
	
	//Called when this pass is first created (always before the first component registers itself with this pass).
	//Must call 'FSceneViewExtensions::NewExtension' and return it!
	virtual TSharedRef<F_EGP_RenderPassSceneViewExtension> InitThisPass_GameThread(UWorld& thisWorld) PURE_VIRTUAL(UCustomRenderPass::InitThisPass_GameThread, return *(TSharedRef<F_EGP_RenderPassSceneViewExtension>*)(nullptr); )
	//Called on the Render Thread ASAP after this pass is created, and after the game-thread version of this function.
	virtual void InitThisPass_RenderThread() { }
	//Called when the owning subsystem is dying, or a user explicitly killed this pass.
	//
	//You must not queue up any new render commands in your pass after this call!
	//The associated scene-view-extension will be disabled for you so that it can't generate commands.
	//
	//Note that you do not need to manually delete the scene-view extension, as it's tracked with reference-counting.
	virtual void CleanupThisPass_RenderThread(UWorld& thisWorld, bool subsystemIsDying) { SceneViewExtension->KillRendering(); }
	
	//Registers the given component so it starts participating in the pass.
	//If it was already registered, nothing happens.
	virtual void RegisterPassComponent(U_EGP_RenderPassComponent*);
	//Unregisters the given component so it stops participating in the pass.
	//It probably will take a frame or two for this to take effect on the render thread.
	//
	//If the component was not registered, nothing happens.
	virtual void UnregisterPassComponent(U_EGP_RenderPassComponent*);

	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient)
	U_EGP_RenderPassSubsystem* Subsystem = nullptr;
	TSharedPtr<F_EGP_RenderPassSceneViewExtension> SceneViewExtension; 
	
	UPROPERTY(BlueprintReadOnly, VisibleInstanceOnly, Transient, DisplayName="Registered Components")
	TSet<U_EGP_RenderPassComponent*> Components_GameThread;
	TMap<TWeakObjectPtr<const U_EGP_RenderPassComponent>, EGP::CustomRenderPasses::ProxyData_t> ComponentProxies_RenderThread;

	
private:

	bool warnedAboutArrayHeapUsage = false;
	bool warnedAboutProxyHeapUsage = false;
};

#pragma endregion