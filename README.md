# Extended Graphics Programming ("EGP")

An Unreal 5.3+ plugin that helps with advanced graphics programming.
Pure C++ code is in the namepsace `EGP`, unreal types contain the prefix `EGP_`,
    and macros start with `EGP_`.

All logging is done in the category `LogEGP`.

## Post-Process Material Shaders

**`#include "EGP_PostProcessMaterialShaders.h"`**

Lets you write Material shaders using the infrastructure of the post-process Material domain,
    to accomplish one of two things:

* Offscreen work (e.g. compute simulation). We call this a "simulation" pass.
* Screen-space graphics work, with a Compute or Vertex+Pixel shader. We call this a "screen-space" pass.

Unfortunately there's not much reason to use a Simulation pass in practice, as Unreal requires all Material shaders to have an associated viewport anyway.

To make shaders for one of these passes, do the following:

1. Inherit from `FSimulationShader` or `FScreenSpaceShader`
2. Include an extra line in your shader(s)' parameter struct, either
    `EGP_SIMULATION_PASS_MATERIAL_DATA()` or `EGP_SCREEN_SPACE_PASS_MATERIAL_DATA()`.
3. Create an 'input' to configure the post-process Material inputs, either
    `FSimulationPassMaterialInputs` or `FScreenSpacePassMaterialInputs`.
4. Create a 'state' to describe how to execute your shader pipeline, either
    `FSimulationPassState` or `FScreenSpacePassRenderState` or `FScreenSpacePassComputeState`
    (or the templated child versions that provide extra control).
5. Call the correct function for your pass, either
    `AddSimulationMaterialPass()` or `AddScreenSpaceRenderPass()` or `AddScreenSpaceComputePass()`.
6. In your shader source, to import Material and Post-Process code, add the following:

````
#include "/EGP/ScreenPass/pre.ush"
// (any custom Material inputs must be defined here)
#include "/EGP/ScreenPass/post.ush"
````

7. See `post.ush` for code that helps you invoke the Material based on which pass/shader you're in.

## Mesh batch gathering

**`#include "EGP_GetMeshBatches.h"`**

To get all `FMeshBatch` instances associated with a primitive-component, call
    `EGP::ForEachBatch(viewInfo, primitiveSceneProxy, lambdaPerBatch)`.

## Material Shader compilation

**`#include "EGP_GetMaterialShader.h"`**

To compile one or more shaders against a Material,
    call `FindMaterialShaders_RenderThread(material, shaderTypes, findSettings)`.
There is a second overload which takes a predicate lambda,
    to rule out specific fallback Materials.

## Downsample-Depth pass

**`#include "EGP_DownsampleDepthPass.h"`**

Unreal has a nice utility shader for downsampling depth textures in different ways,
    however it's not available outside the engine (you'd get a linker error if you tried to call it).
This file offers a direct copy-paste of that functionality.

## Custom Render Passes

**`#include "EGP_CustomRenderPasses.h"`**

Handles many annoying details of setting up a custom render pass,
    optionally including a mesh pass using specific actors/primitives in the scene.
If you actually want to redraw the entire scene in your pass,
    it's highly recommended to instead fork the engine and add a new official pass.

### Step 1: Define a `U_EGP_RenderPass`

Create a new child of `U_EGP_RenderPass`, providing a user- and Blueprint-friendly window into your pass.
This is where users will configure any global settings;
    if the pass object has any non-POD data then you need to manually copy them
    to a render-thread-only clone, for example on every Tick.

Custom pass instances are unique to a world, like a World Subsystem.
They can be accessed through the `U_EGP_RenderPassSubsystem` by calling `GetPass<MyPassType>(true)`.

This function will automatically create your pass the first time it's called,
    unless you pass `false` to disable that behavior.
If you define a `U_EGP_RenderPassComponent` (see below),
    it will also be automatically created when the first component is spawned.

Passes will be cleaned up when the world/subsystem dies, but you can kill them early by calling `DestroyPass<MyPassType>()`.

The render pass has a number of virtual functions: game-thread Tick, render-thread Tick,
    and lifetime events like `InitThisPass_[X]Thread()` and `CleanupThisPass_RenderThread()`.
The only required function is `InitThisPass_GameThread()`,
    because it must create and return your pass's *scene-view extension* (see below).
Note that scene-view extensions must be created with the snippet
    `FSceneViewExtensions::NewExtension<MySVE>(constructorArgs...)`;

Finally, the pass owns a filter on what views should use it, `ViewFilter`.
By default the pass will be enabled everywhere, including editor thumbnails and preview scenes, so be careful!

### Step 2: Define a `U_EGP_RenderPassComponent` (if making a mesh pass)

Define a new kind of `U_EGP_RenderPassComponent`, which you can attach to every Primitive Component in the scene that you want to draw in your pass.

You must also create a POD "proxy" struct representing the render-thread equivalent of this component,
    with any of its per-primitive pass settings.
If you have no per-primitive pass settings then just define an empty placeholder struct.

Override `GetPassType()` to return the type of your `U_EGP_RenderPass` object.

Invoke the macro `EGP_PASS_COMPONENT_SIMPLE_PROXY_IMPL(FMyProxy, (FMyProxy{ this->param1, this->param2 }))`
    to tell the library how to convert your component into its POD proxy struct.
If your case is more complex, you can manually implement the functions within this macro.

### Step 3: Define a `T_EGP_RenderPassSceneViewExtension`

A Scene-View Extension (or "SVE" for short) is a hook into unreal's renderer,
    to issue custom rendering and compute commands at specific times.
It will be responsible for dispatching all the draw calls for your custom pass.

Unreal's base class for scene view extensions is `FSceneViewExtensionBase`,
    but you will inherit from our own child class
    `T_EGP_SceneViewExtension<TCustomPass[, TComponent, TComponentProxy>`.
The first template argument is your `U_EGP_RenderPass` child type.
The subsequent template arguments are your `U_EGP_RenderPassComponent` and its proxy struct, if applicable.

Unreal has a special convention for SVE constructors which you must follow:
    the first argument must be `const FAutoRegister&`, then subsequent arguments are up to you,
    being passed in from your call to `FSceneViewExtensions::NewExtension<T>(...)`.
In our custom pass framework, the creation of the SVE
    should happen within your `U_EGP_RenderPass` in `InitThisPass_GameThread()`.

This SVE will automatically filter itself out of different views based on the pass's `ViewFilter`,
    and enumerate all living instances of your components with `ForEachComponent_RenderThread()`.
It will also ensure the owning pass object lives at least as long as any render-thread activity,
    so you don't have to worry about the GC causing problems when a pass dies.
    
To execute a mesh pass you will also need a custom `MeshPassProcessor`,
    but those are out-of-scope for this framework because they don't have much boilerplate.
See examples in the engine for how to make these.

### Step 4: Define persistent per-view data (if needed)

If your pass requires persistent data for each viewport (e.g. accumulated data for temporal algorithms),
    define that data as a struct inheriting from `F_EGP_ViewPersistentData`.
The lifetime of the viewport resources should be tied to the lifetime of this struct:

* Initialize on constructor
* Clean up on destructor
* Resize the data when `Resample()` is called.

Next, give your custom pass object a field of type `T_EGP_PerViewData<T>`,
    where `T` is the struct you defined.
In the pass's render-thread Tick, call `Tick` on this per-view-data object.

The `T_EGP_PerViewData` object will lazily instantiate your struct for each new view it encounters,
    and delete struct instances for views that haven't been used in a while
    (unless you mark that view as permanent).
Unfortunately this is the only known way to ensure that dead views get cleaned up.
