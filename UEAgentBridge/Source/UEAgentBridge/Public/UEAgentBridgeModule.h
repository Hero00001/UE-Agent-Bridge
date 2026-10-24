// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"
#include "Modules/ModuleManager.h"

class IHttpRouter;

/**
 * FUEAgentBridgeModule - a local HTTP bridge inside the 4.27 editor.
 *
 * The downloaded ZCode "unreal-engine" plugin only speaks Epic's experimental
 * MCP protocol, which ships with UE 5.8+ - it can never attach to 4.27. This
 * plugin replaces that missing server with a tiny REST bridge I can drive
 * directly from the agent shell:
 *
 *   GET  /ping    -> engine/project info + world state
 *   GET  /actors  -> every actor in the open editor world (name, class, transform)
 *   POST /exec    -> run editor Python ("code" field), returns the command result
 *
 * Loopback development tool only: no authentication, for local agent use.
 */
class FUEAgentBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void BindRoutes();

	int32 Port;
	TSharedPtr<class IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;
};
