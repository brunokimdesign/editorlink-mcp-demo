#include "EditorLinkMCPDemoSubsystem.h"

#include "EditorLinkMCPDemoCommands.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaxRequestBytes = 1024 * 1024;

	FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	TUniquePtr<FHttpServerResponse> JsonResponse(const TSharedPtr<FJsonObject>& Object, EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(SerializeJson(Object), TEXT("application/json"));
		Response->Code = Code;
		return Response;
	}

	TSharedPtr<FJsonObject> ErrorObject(const FString& Message, const FString& Code)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), Message);
		Result->SetStringField(TEXT("code"), Code);
		return Result;
	}

	FString SessionDescriptorPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EditorLinkMCPDemo"), TEXT("session.json"));
	}
}

void UEditorLinkMCPDemoSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SessionToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	StartServer();
}

void UEditorLinkMCPDemoSubsystem::Deinitialize()
{
	StopServer();
	Super::Deinitialize();
}

FString UEditorLinkMCPDemoSubsystem::GetServerScriptPath() const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EditorLinkMCPDemo"));
	return Plugin.IsValid()
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Content"), TEXT("Python"), TEXT("editorlink_mcp_demo_server.py")))
		: FString();
}

FString UEditorLinkMCPDemoSubsystem::GetProjectPath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
}

FString UEditorLinkMCPDemoSubsystem::GetStatusText() const
{
	return FString::Printf(
		TEXT("Status: %s\nLocal bridge: http://127.0.0.1:%u/command\nSecurity: loopback session token required\nMCP transport: STDIO\nProject: %s"),
		IsServerRunning() ? TEXT("Running") : TEXT("Stopped"),
		Port,
		*GetProjectPath());
}

FString UEditorLinkMCPDemoSubsystem::MakeJsonClientSnippet() const
{
	FString Script = GetServerScriptPath().Replace(TEXT("\\"), TEXT("\\\\"));
	FString Project = GetProjectPath().Replace(TEXT("\\"), TEXT("\\\\"));
	return FString::Printf(
		TEXT("{\n  \"mcpServers\": {\n    \"editorlink-unreal\": {\n      \"command\": \"python\",\n      \"args\": [\"%s\", \"--project\", \"%s\"]\n    }\n  }\n}"),
		*Script,
		*Project);
}

FString UEditorLinkMCPDemoSubsystem::MakeTomlClientSnippet() const
{
	FString Script = GetServerScriptPath().Replace(TEXT("\\"), TEXT("/"));
	FString Project = GetProjectPath().Replace(TEXT("\\"), TEXT("/"));
	return FString::Printf(
		TEXT("[mcp_servers.editorlink-unreal]\ncommand = \"python\"\nargs = [\"%s\", \"--project\", \"%s\"]\nstartup_timeout_sec = 30\ntool_timeout_sec = 120"),
		*Script,
		*Project);
}

void UEditorLinkMCPDemoSubsystem::StartServer()
{
	FHttpServerModule& Module = FHttpServerModule::Get();
	HttpRouter = Module.GetHttpRouter(Port);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("EditorLink MCP Demo could not obtain an HTTP router on port %u"), Port);
		return;
	}

	RouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/command")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateUObject(this, &UEditorLinkMCPDemoSubsystem::HandleCommandRequest));

	Module.StartAllListeners();
	WriteSessionDescriptor();
	UE_LOG(LogTemp, Log, TEXT("EditorLink MCP Demo bridge started at 127.0.0.1:%u"), Port);
}

void UEditorLinkMCPDemoSubsystem::StopServer()
{
	if (HttpRouter.IsValid() && RouteHandle.IsValid())
	{
		HttpRouter->UnbindRoute(RouteHandle);
		RouteHandle.Reset();
	}
	RemoveSessionDescriptor();
	HttpRouter.Reset();
}

void UEditorLinkMCPDemoSubsystem::WriteSessionDescriptor() const
{
	TSharedPtr<FJsonObject> Descriptor = MakeShared<FJsonObject>();
	Descriptor->SetStringField(TEXT("server"), TEXT("EditorLink MCP Demo"));
	Descriptor->SetStringField(TEXT("version"), TEXT("0.1.4-demo"));
	Descriptor->SetStringField(TEXT("host"), TEXT("127.0.0.1"));
	Descriptor->SetNumberField(TEXT("port"), Port);
	Descriptor->SetStringField(TEXT("path"), TEXT("/command"));
	Descriptor->SetStringField(TEXT("token"), SessionToken);
	Descriptor->SetStringField(TEXT("project"), GetProjectPath());
	Descriptor->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());

	const FString Path = SessionDescriptorPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(SerializeJson(Descriptor), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void UEditorLinkMCPDemoSubsystem::RemoveSessionDescriptor() const
{
	IFileManager::Get().Delete(*SessionDescriptorPath(), false, true, true);
}

bool UEditorLinkMCPDemoSubsystem::HandleCommandRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	if (Request.Body.Num() <= 0 || Request.Body.Num() > MaxRequestBytes)
	{
		OnComplete(JsonResponse(ErrorObject(TEXT("Request body is empty or exceeds 1 MiB."), TEXT("invalid_request_size")), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
	const FString Body(Converted.Length(), Converted.Get());
	TSharedPtr<FJsonObject> RequestObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
	{
		OnComplete(JsonResponse(ErrorObject(TEXT("Body must be a valid JSON object."), TEXT("invalid_json")), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	FString Token;
	if (!RequestObject->TryGetStringField(TEXT("token"), Token) || !Token.Equals(SessionToken, ESearchCase::CaseSensitive))
	{
		OnComplete(JsonResponse(ErrorObject(TEXT("A valid EditorLink session token is required."), TEXT("unauthorized")), EHttpServerResponseCodes::Denied));
		return true;
	}

	FString Command;
	if (!RequestObject->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
	{
		OnComplete(JsonResponse(ErrorObject(TEXT("The command field is required."), TEXT("missing_command")), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	const TSharedPtr<FJsonObject>* ParametersPtr = nullptr;
	TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
	if (RequestObject->TryGetObjectField(TEXT("params"), ParametersPtr) && ParametersPtr && ParametersPtr->IsValid())
	{
		Parameters = *ParametersPtr;
	}

	AsyncTask(ENamedThreads::GameThread, [this, Command, Parameters, OnComplete]()
	{
		TSharedPtr<FJsonObject> Result = FEditorLinkMCPDemoCommands::Execute(Command, Parameters);
		const bool bSuccess = Result.IsValid() && Result->GetBoolField(TEXT("success"));
		RecordActivity(Command, bSuccess);
		OnComplete(JsonResponse(Result.IsValid() ? Result : ErrorObject(TEXT("Command returned no result."), TEXT("empty_result")), EHttpServerResponseCodes::Ok));
	});

	return true;
}

void UEditorLinkMCPDemoSubsystem::RecordActivity(const FString& Command, bool bSuccess)
{
	RecentActivity.Insert(FString::Printf(TEXT("%s | %s | %s"), *FDateTime::Now().ToIso8601(), bSuccess ? TEXT("OK") : TEXT("FAILED"), *Command), 0);
	if (RecentActivity.Num() > 100)
	{
		RecentActivity.SetNum(100);
	}
}

