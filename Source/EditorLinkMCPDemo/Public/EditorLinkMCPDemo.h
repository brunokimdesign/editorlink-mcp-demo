#pragma once

#include "Modules/ModuleManager.h"

class FEditorLinkMCPDemoModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedRef<class SDockTab> SpawnEditorLinkTab(const class FSpawnTabArgs& Args);
	void OpenEditorLinkTab();
	void RegisterMenus();
};

