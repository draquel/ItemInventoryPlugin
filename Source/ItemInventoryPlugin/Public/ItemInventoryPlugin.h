#pragma once

#include "Modules/ModuleManager.h"

class FItemInventoryPluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
