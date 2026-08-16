#pragma once

#include "CoreMinimal.h"

class FJsonObject;

class FEditorLinkMCPDemoCommands
{
public:
	static TSharedPtr<FJsonObject> Execute(const FString& Command, const TSharedPtr<FJsonObject>& Parameters);

private:
	static TSharedPtr<FJsonObject> MakeSuccess(const TSharedPtr<FJsonObject>& Data = nullptr);
	static TSharedPtr<FJsonObject> MakeError(const FString& Error, const FString& Code = TEXT("command_failed"));
};


