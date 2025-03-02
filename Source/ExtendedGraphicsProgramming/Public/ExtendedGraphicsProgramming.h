#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FExtendedGraphicsProgrammingModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

EXTENDEDGRAPHICSPROGRAMMING_API DECLARE_LOG_CATEGORY_EXTERN(LogEGP, Warning, Log);