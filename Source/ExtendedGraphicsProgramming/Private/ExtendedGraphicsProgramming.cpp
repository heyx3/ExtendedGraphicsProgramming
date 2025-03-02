#include "ExtendedGraphicsProgramming.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FExtendedGraphicsProgrammingModule"


void FExtendedGraphicsProgrammingModule::StartupModule()
{
	//Register our shader folder with the engine.
	auto thisPlugin = IPluginManager::Get().FindPlugin(TEXT("ExtendedGraphicsProgramming"));
	auto thisShadersDir = FPaths::Combine(thisPlugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/EGP"), thisShadersDir);
}

void FExtendedGraphicsProgrammingModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FExtendedGraphicsProgrammingModule, ExtendedGraphicsProgramming)
DEFINE_LOG_CATEGORY(LogEGP);