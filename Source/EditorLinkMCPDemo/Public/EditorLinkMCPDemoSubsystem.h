#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"
#include "EditorLinkMCPDemoSubsystem.generated.h"

UCLASS()
class EDITORLINKMCPDEMO_API UEditorLinkMCPDemoSubsystem final : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsServerRunning() const { return HttpRouter.IsValid() && RouteHandle.IsValid(); }
	uint32 GetPort() const { return Port; }
	const FString& GetSessionToken() const { return SessionToken; }
	FString GetServerScriptPath() const;
	FString GetProjectPath() const;
	FString GetStatusText() const;
	FString MakeJsonClientSnippet() const;
	FString MakeTomlClientSnippet() const;

private:
	void StartServer();
	void StopServer();
	void WriteSessionDescriptor() const;
	void RemoveSessionDescriptor() const;
	bool HandleCommandRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	void RecordActivity(const FString& Command, bool bSuccess);

	TSharedPtr<IHttpRouter> HttpRouter;
	FHttpRouteHandle RouteHandle;
	uint32 Port = 6010;
	FString SessionToken;
	TArray<FString> RecentActivity;
};


