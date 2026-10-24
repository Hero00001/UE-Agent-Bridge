# UE Agent Bridge

Let AI coding agents **see and drive the Unreal Engine 4.27 editor** over plain local HTTP — list actors, run editor Python, automate level work. No marketplace plugin, no MCP protocol, no extra installers. If your agent can run `curl` (or any HTTP call), it can control your editor.

Built for **UE 4.27** because Epic's official MCP server (`ModelContextProtocol` plugin) only ships with UE 5.7/5.8+. This plugin backfills that capability for 4.27 projects.

---

## What it does

Once the editor is open with the plugin loaded, it serves three endpoints on `http://127.0.0.1:8027`:

| Endpoint | Method | What it gives you |
|---|---|---|
| `/ping` | GET | Engine version, project name, open map — the connection smoke test |
| `/actors` | GET | Every actor in the open editor world: name, label, class, location, rotation |
| `/exec` | POST | Run **arbitrary multi-line editor Python** and get `print()` output + errors back |

That covers: spawning/moving/deleting actors, setting properties, importing assets, creating Blueprint assets, bulk renames, opening/saving maps, Asset Registry queries, lighting setup, and everything else in UE 4.27's Python API.

## Requirements

- Unreal Engine **4.27** (tested on 4.27.2, Windows x64)
- To compile from source: Visual Studio 2019/2022 with the UE 4.27 game workload
- The editor must be running — the bridge lives inside the editor process

---

## Install

### Option A — single project

1. Copy the `UEAgentBridge/` folder into your project's `Plugins/` folder (create it if missing).
2. Open the project. UE asks to build the missing module → click **Yes**.
3. In the Output Log you should see: `[DSBridge] READY - GET /ping, GET /actors, POST /exec`

### Option B — every 4.27 project at once (engine-wide)

1. Copy the `UEAgentBridge/` folder into `<EngineInstall>/UE_4.27/Engine/Plugins/`.
2. If you have prebuilt binaries (from a Release zip), copy them into `UEAgentBridge/Binaries/Win64/`. Otherwise open any 4.27 project and click **Yes** on the build prompt.
3. The plugin sets `EnabledByDefault: true`, so **every** 4.27 project gets it automatically — no per-project setup.

### Verify it's alive

```bash
curl http://127.0.0.1:8027/ping
```

```json
{
  "status": "ok",
  "engine": "4.27.2",
  "project": "MyGame",
  "mapName": "MyLevel",
  "worldValid": true
}
```

---

## Using it

### List actors

```bash
curl http://127.0.0.1:8027/actors
```

### Run editor Python

```bash
curl -X POST http://127.0.0.1:8027/exec \
  -H "Content-Type: application/json" \
  -d "{\"code\": \"import unreal\\nprint('hello from inside the editor')\"}"
```

Response:

```json
{
  "status": "ok",
  "output": "hello from inside the editor\r\n"
}
```

Spawn a cube 300 units in front of the origin:

```json
{ "code": "import unreal\nunreal.EditorLevelLibrary.spawn_actor_from_class(\n    unreal.StaticMeshActor, unreal.Vector(300, 0, 100))" }
```

Errors come back with the real Python traceback (`status: "error"` + `result`), so agents can self-debug.

---

## Connecting an AI agent

Any agent with shell access needs exactly two things: the endpoint and the API. Paste this into your agent's context or say it directly:

> There is a plugin called UE Agent Bridge running inside my open Unreal 4.27 editor at `http://127.0.0.1:8027`. Endpoints: `GET /ping` (identity), `GET /actors` (list level actors), `POST /exec` with JSON body `{"code": "<editor python>"}` which returns `{"status", "output", "result"}`. Use it to inspect and modify the level instead of asking me to paste Python into the editor.

That's the whole integration — no MCP client config, no plugin marketplace account.

`Examples/connect_and_list.py` is a minimal stdlib-only client you can crib from.

---

## Security

- The server binds **loopback only** (`127.0.0.1`) — other machines on your network cannot reach it.
- There is **no authentication**. It can execute arbitrary Python in your editor. This is a local development tool: don't expose the port, don't run it on shared machines, and treat any process on your machine as trusted.

## Limitations (be honest with your agents)

- **One editor at a time** — the port is fixed at 8027; two simultaneous editor instances will collide (the second logs a bind error and its bridge stays dead).
- **4.27 only** — other engine versions will need a recompile and possibly small API fixes (the 4.27 HTTPServer/Python APIs it uses are version-sensitive).
- **Blueprint node graphs** — actors and BP *assets* can be created and configured via Python, but placing/wiring individual Blueprint nodes is not exposed to Python in 4.27. (A future version could add native C++ endpoints using the `UEdGraph` API.)
- The editor must be running; the bridge cannot launch it.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `curl` connection refused | Editor isn't open, or the plugin didn't load. Check Output Log for `[DSBridge] READY`. |
| "InitializeModule function was not found" | Stale/incorrectly built DLL — rebuild the plugin. |
| "module could not be initialized successfully" | Old binary from a failed build — delete the plugin's `Binaries/` and `Intermediate/`, rebuild. |
| Editor opens but nothing listens | Confirm with `netstat -ano | findstr 8027`; if absent, rebuild the plugin. |

## License

MIT — see [LICENSE](LICENSE). Unreal Engine itself stays under Epic's EULA; this plugin only drives the editor you already have.
