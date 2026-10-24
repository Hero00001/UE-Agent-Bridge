"""Minimal UE Agent Bridge client - Python stdlib only, no dependencies.

Connects to the bridge inside your open UE 4.27 editor, proves the connection,
lists the level's actors, and spawns one cube as a live demo.

Usage:  python connect_and_list.py
"""

import json
import urllib.request

BRIDGE = "http://127.0.0.1:8027"


def call(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(
        BRIDGE + path,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST" if payload is not None else "GET")
    return json.load(urllib.request.urlopen(req, timeout=20))


def main():
    # 1. Identity check
    ping = call("/ping")
    print("Connected to:", ping["project"], "| engine", ping["engine"],
          "| map", ping["mapName"])

    # 2. Read the level
    actors = call("/actors")
    print("Actors in level:", actors["count"])
    for a in actors["actors"][:10]:
        print("   %-28s %-24s @ %s" % (a["label"], a["class"], a["location"]))

    # 3. Do something - run editor Python
    code = (
        "import unreal\n"
        "cube = unreal.EditorLevelLibrary.spawn_actor_from_class(\n"
        "    unreal.StaticMeshActor, unreal.Vector(0, 0, 150))\n"
        "if cube:\n"
        "    cube.set_actor_label('AgentBridge_DemoCube')\n"
        "    print('spawned:', cube.get_actor_label())\n"
    )
    result = call("/exec", {"code": code})
    print("Exec status:", result["status"])
    print("Editor said:", result.get("output", "").strip())


if __name__ == "__main__":
    main()
