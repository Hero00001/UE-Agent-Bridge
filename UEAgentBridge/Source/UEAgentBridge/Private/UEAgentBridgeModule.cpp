// Copyright Epic Games, Inc. All Rights Reserved.

#include "UEAgentBridgeModule.h"

#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpRouteHandle.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "IPythonScriptPlugin.h"
#include "Json.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Logging/LogMacros.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDSBridge, Log, All);

// Module bootstrap - without this the DLL exports no InitializeModule and the
// ModuleManager rejects it ("InitializeModule function was not found").
IMPLEMENT_MODULE(FUEAgentBridgeModule, UEAgentBridge)

namespace
{
	FString MakeJson(TSharedRef<FJsonObject> Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	TUniquePtr<FHttpServerResponse> JsonPtrResponse(const FString& Json)
	{
		return FHttpServerResponse::Create(Json, TEXT("application/json"));
	}

	bool ParseJsonObjectBody(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutObject)
	{
		const char* BodyChars = reinterpret_cast<const char*>(Request.Body.GetData());
		const FString BodyUtf8(BodyChars, Request.Body.Num());
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyUtf8);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}
}

void FUEAgentBridgeModule::StartupModule()
{
	Port = 8027;

	UE_LOG(LogDSBridge, Log, TEXT("[DSBridge] starting HTTP bridge on port %d"), Port);

	Router = FHttpServerModule::Get().GetHttpRouter(Port);
	if (Router.IsValid())
	{
		// GetHttpRouter creates the listener, but in 4.27 it only starts listening
		// if the module-wide flag was already enabled. Nothing enables it by
		// default, so flip it explicitly (idempotent for other listeners too).
		FHttpServerModule::Get().StartAllListeners();
		BindRoutes();
		UE_LOG(LogDSBridge, Log, TEXT("[DSBridge] READY - GET /ping, GET /actors, POST /exec"));
	}
	else
	{
		UE_LOG(LogDSBridge, Error, TEXT("[DSBridge] could not create HTTP router on port %d"), Port);
	}
}

void FUEAgentBridgeModule::ShutdownModule()
{
	if (Router.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			Router->UnbindRoute(Handle);
		}
		RouteHandles.Reset();
		Router.Reset();
	}
}

void FUEAgentBridgeModule::BindRoutes()
{
	// ---- GET /ping : identity + world state (the connection smoke test) ----
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/ping")), EHttpServerRequestVerbs::VERB_GET,
		[](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
		{
			// Marshal to the game thread: engine globals are not thread-safe
			AsyncTask(ENamedThreads::GameThread, [OnComplete]() mutable
			{
				TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetStringField(TEXT("status"), TEXT("ok"));
				Out->SetStringField(TEXT("plugin"), TEXT("UEAgentBridge 0.1"));
				Out->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
				Out->SetStringField(TEXT("project"), FApp::GetProjectName());
				Out->SetStringField(TEXT("projectDir"), FPaths::ProjectDir());
				Out->SetStringField(TEXT("mapName"), TEXT("unknown"));

				bool bWorldValid = false;
				if (GEditor != nullptr)
				{
					if (UWorld* World = GEditor->GetEditorWorldContext().World())
					{
						bWorldValid = true;
						Out->SetStringField(TEXT("mapName"), World->GetMapName());
					}
				}
				Out->SetBoolField(TEXT("worldValid"), bWorldValid);

				OnComplete(JsonPtrResponse(MakeJson(Out)));
			});
			return true;
		}));

	// ---- GET /actors : every actor in the open editor world ----
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/actors")), EHttpServerRequestVerbs::VERB_GET,
		[](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
		{
			AsyncTask(ENamedThreads::GameThread, [OnComplete]() mutable
			{
				TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
				Out->SetStringField(TEXT("status"), TEXT("error"));
				Out->SetStringField(TEXT("error"), TEXT("no editor world"));

				UWorld* World = (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
				if (World != nullptr)
				{
					Out->SetStringField(TEXT("status"), TEXT("ok"));
					Out->SetStringField(TEXT("mapName"), World->GetMapName());

					TArray<TSharedPtr<FJsonValue>> ActorArray;
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						AActor* Actor = *It;
						if (Actor == nullptr)
						{
							continue;
						}
						TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
						A->SetStringField(TEXT("name"), Actor->GetName());
						A->SetStringField(TEXT("label"), Actor->GetActorLabel());
						A->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
						const FVector Loc = Actor->GetActorLocation();
						const FRotator Rot = Actor->GetActorRotation();
						A->SetStringField(TEXT("location"), FString::Printf(TEXT("%.0f, %.0f, %.0f"), Loc.X, Loc.Y, Loc.Z));
						A->SetStringField(TEXT("rotation"), FString::Printf(TEXT("%.0f, %.0f, %.0f"), Rot.Pitch, Rot.Yaw, Rot.Roll));
						ActorArray.Add(MakeShared<FJsonValueObject>(A));
					}
					Out->SetArrayField(TEXT("actors"), ActorArray);
					Out->SetNumberField(TEXT("count"), ActorArray.Num());
				}

				OnComplete(JsonPtrResponse(MakeJson(Out)));
			});
			return true;
		}));

	// ---- POST /exec : run editor Python ({"code": "..."}) ----
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/exec")), EHttpServerRequestVerbs::VERB_POST,
		[](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
		{
			TSharedPtr<FJsonObject> Body;
			const bool bParsed = ParseJsonObjectBody(Request, Body);
			FString Code;
			if (bParsed)
			{
				Code = Body->GetStringField(TEXT("code"));
			}

			AsyncTask(ENamedThreads::GameThread, [Code, OnComplete]() mutable
			{
				TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

				// Python needs the game thread too; fail loudly when unavailable
				if (GEditor == nullptr)
				{
					Out->SetStringField(TEXT("status"), TEXT("error"));
					Out->SetStringField(TEXT("error"), TEXT("editor unavailable"));
					OnComplete(JsonPtrResponse(MakeJson(Out)));
					return;
				}

				if (IPythonScriptPlugin* Python = IPythonScriptPlugin::Get())
				{
					FPythonCommandEx Ex;
					Ex.Command = Code;
					// ExecuteFile accepts multi-statement scripts (EvaluateStatement
					// only runs a single statement and rejects multi-line code).
					Ex.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
					if (Python->ExecPythonCommandEx(Ex))
					{
						// print() calls land in LogOutput - hand them back too
						FString CapturedOutput;
						for (const FPythonLogOutputEntry& Entry : Ex.LogOutput)
						{
							CapturedOutput += Entry.Output;
							CapturedOutput += LINE_TERMINATOR;
						}
						Out->SetStringField(TEXT("status"), TEXT("ok"));
						Out->SetStringField(TEXT("result"), Ex.CommandResult);
						Out->SetStringField(TEXT("output"), CapturedOutput);
					}
					else
					{
						Out->SetStringField(TEXT("status"), TEXT("error"));
						Out->SetStringField(TEXT("result"), Ex.CommandResult);
						Out->SetStringField(TEXT("error"), TEXT("python execution failed (see editor Output Log)"));
					}
				}
				else
				{
					Out->SetStringField(TEXT("status"), TEXT("error"));
					Out->SetStringField(TEXT("error"), TEXT("PythonScriptPlugin unavailable"));
				}

				OnComplete(JsonPtrResponse(MakeJson(Out)));
			});
			return true;
		}));
}
