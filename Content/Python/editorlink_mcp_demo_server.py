"""Provider-neutral MCP server for EditorLink MCP Demo.

The MCP process communicates with an authenticated loopback bridge that lives in
the Unreal Editor process. No Unreal project data is exposed over the network.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP


_parser = argparse.ArgumentParser(add_help=False)
_parser.add_argument("--project", required=True)
_args, _unknown = _parser.parse_known_args()
_project_dir = Path(_args.project).expanduser().resolve()
_session_file = _project_dir / "Saved" / "EditorLinkMCPDemo" / "session.json"

mcp = FastMCP(
    "EditorLink MCP Demo",
    instructions=(
        "Inspect and edit an Unreal Engine project through explicit, typed tools. "
        "Changes remain unsaved until a save tool is called. Destructive tools "
        "require confirm=true, and supported edits participate in Unreal Undo."
    ),
)


def _vector(x: float, y: float, z: float) -> dict[str, float]:
    return {"x": x, "y": y, "z": z}


def _rotation(pitch: float, yaw: float, roll: float) -> dict[str, float]:
    return {"pitch": pitch, "yaw": yaw, "roll": roll}


def _call(command: str, **params: Any) -> str:
    """Call the native bridge and return a readable JSON result."""
    if not _session_file.is_file():
        raise RuntimeError(
            "EditorLink MCP Demo is not running. Enable the plugin, restart Unreal "
            f"Editor, and keep this project open. Missing: {_session_file}"
        )

    try:
        session = json.loads(_session_file.read_text(encoding="utf-8"))
        endpoint_value = session.get("endpoint")
        if endpoint_value:
            endpoint = str(endpoint_value)
        else:
            host = str(session.get("host", "127.0.0.1"))
            port = int(session["port"])
            path = str(session.get("path", "/command"))
            endpoint = f"http://{host}:{port}{path}"
        token = str(session["token"])
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"The EditorLink session descriptor is invalid: {exc}") from exc

    payload = json.dumps(
        {"token": token, "command": command, "params": params}
    ).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=payload,
        method="POST",
        headers={
            "Content-Type": "application/json",
            "X-EditorLink-Token": token,
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            raw = response.read().decode("utf-8")
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"EditorLink bridge returned HTTP {exc.code}: {raw}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(
            "Could not reach the EditorLink bridge. Confirm that Unreal Editor is open "
            f"with this project and plugin enabled: {exc.reason}"
        ) from exc

    try:
        result = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"EditorLink returned invalid JSON: {raw[:500]}") from exc

    return json.dumps(result, ensure_ascii=False, indent=2)


@mcp.tool()
def editorlink_ping() -> str:
    """Check bridge availability, plugin version, engine version, and editor readiness."""
    return _call("ping")


@mcp.tool()
def editorlink_list_capabilities() -> str:
    """List the feature groups and safety behavior available in this edition."""
    return _call("list_capabilities")


@mcp.tool()
def editorlink_get_editor_state() -> str:
    """Read project, current level, play state, selected actors, and selected assets."""
    return _call("get_editor_state")


@mcp.tool()
def editorlink_search_assets(query: str = "", class_filter: str = "", limit: int = 100) -> str:
    """Search project assets by name/path and optionally filter by Unreal class."""
    return _call("search_assets", query=query, class_filter=class_filter, limit=limit)


@mcp.tool()
def editorlink_inspect_asset(asset_path: str, limit: int = 150) -> str:
    """Inspect an asset and its editable properties without changing it."""
    return _call("inspect_asset", asset_path=asset_path, limit=limit)


@mcp.tool()
def editorlink_duplicate_asset(asset_path: str, destination_path: str) -> str:
    """Duplicate an asset to a new Unreal content path. The copy is not auto-saved."""
    return _call("duplicate_asset", asset_path=asset_path, destination_path=destination_path)


@mcp.tool()
def editorlink_rename_asset(asset_path: str, destination_path: str) -> str:
    """Rename or move an asset to a new Unreal content path."""
    return _call("rename_asset", asset_path=asset_path, destination_path=destination_path)


@mcp.tool()
def editorlink_delete_asset(asset_path: str, confirm: bool = False) -> str:
    """Permanently delete an asset. Requires confirm=true."""
    return _call("delete_asset", asset_path=asset_path, confirm=confirm)


@mcp.tool()
def editorlink_save_asset(asset_path: str) -> str:
    """Explicitly save one loaded asset to disk."""
    return _call("save_asset", asset_path=asset_path)


@mcp.tool()
def editorlink_import_asset(
    source_file: str, destination_path: str, replace_existing: bool = False
) -> str:
    """Import a local file into an Unreal content folder without automatically saving it."""
    return _call(
        "import_asset",
        source_file=source_file,
        destination_path=destination_path,
        replace_existing=replace_existing,
    )


@mcp.tool()
def editorlink_list_actors(
    label_filter: str = "", class_filter: str = "", limit: int = 100
) -> str:
    """List actors in the current editor level, with optional label/class filters."""
    return _call(
        "list_actors", label_filter=label_filter, class_filter=class_filter, limit=limit
    )


@mcp.tool()
def editorlink_inspect_actor(actor: str, limit: int = 150) -> str:
    """Inspect an actor by label, object name, or full path, including components."""
    return _call("inspect_actor", actor=actor, limit=limit)


@mcp.tool()
def editorlink_spawn_actor(
    class_path: str,
    label: str = "",
    location_x: float = 0,
    location_y: float = 0,
    location_z: float = 0,
    pitch: float = 0,
    yaw: float = 0,
    roll: float = 0,
    scale_x: float = 1,
    scale_y: float = 1,
    scale_z: float = 1,
) -> str:
    """Spawn an actor in the current editor level. The edit supports Undo."""
    return _call(
        "spawn_actor",
        class_path=class_path,
        label=label,
        location=_vector(location_x, location_y, location_z),
        rotation=_rotation(pitch, yaw, roll),
        scale=_vector(scale_x, scale_y, scale_z),
    )


@mcp.tool()
def editorlink_delete_actor(actor: str, confirm: bool = False) -> str:
    """Delete an actor from the current level. Requires confirm=true and supports Undo."""
    return _call("delete_actor", actor=actor, confirm=confirm)


@mcp.tool()
def editorlink_set_actor_transform(
    actor: str,
    location_x: float,
    location_y: float,
    location_z: float,
    pitch: float,
    yaw: float,
    roll: float,
    scale_x: float = 1,
    scale_y: float = 1,
    scale_z: float = 1,
) -> str:
    """Set an actor's complete transform. The edit supports Undo."""
    return _call(
        "set_actor_transform",
        actor=actor,
        location=_vector(location_x, location_y, location_z),
        rotation=_rotation(pitch, yaw, roll),
        scale=_vector(scale_x, scale_y, scale_z),
    )


@mcp.tool()
def editorlink_add_actor_component(
    actor: str, component_class: str, component_name: str = ""
) -> str:
    """Add an instance component to an actor using an Unreal class path."""
    return _call(
        "add_actor_component",
        actor=actor,
        component_class=component_class,
        component_name=component_name,
    )


@mcp.tool()
def editorlink_remove_actor_component(
    actor: str, component_name: str, confirm: bool = False
) -> str:
    """Remove an instance component from an actor. Requires confirm=true and supports Undo."""
    return _call(
        "remove_actor_component",
        actor=actor,
        component_name=component_name,
        confirm=confirm,
    )


@mcp.tool()
def editorlink_set_editable_property(target: str, property_name: str, value: str) -> str:
    """Set an exposed editable property using Unreal text syntax. The edit supports Undo."""
    return _call(
        "set_editable_property", target=target, property_name=property_name, value=value
    )


@mcp.tool()
def editorlink_load_level(level_path: str) -> str:
    """Open a level/map in Unreal Editor. Unsaved-change prompts may be shown by Unreal."""
    return _call("load_level", level_path=level_path)


@mcp.tool()
def editorlink_save_current_level() -> str:
    """Explicitly save the current editor level."""
    return _call("save_current_level")


@mcp.tool()
def editorlink_inspect_blueprint(asset_path: str, limit: int = 200) -> str:
    """Inspect Blueprint variables, components, graphs, nodes, pins, and connections."""
    return _call("inspect_blueprint", asset_path=asset_path, limit=limit)


@mcp.tool()
def editorlink_inspect_animation_blueprint(asset_path: str, limit: int = 200) -> str:
    """Inspect an Animation Blueprint, including its target skeleton and full graph data."""
    return _call("inspect_anim_blueprint", asset_path=asset_path, limit=limit)


@mcp.tool()
def editorlink_compile_blueprint(asset_path: str) -> str:
    """Compile a Blueprint and report status, errors, warnings, and compiler messages."""
    return _call("compile_blueprint", asset_path=asset_path)


@mcp.tool()
def editorlink_add_blueprint_variable(
    asset_path: str,
    variable_name: str,
    variable_type: str = "string",
    object_class: str = "",
    default_value: str = "",
) -> str:
    """Add a Blueprint variable. Types include bool, byte, int, int64, float, name, string, text, vector, rotator, transform, and object."""
    return _call(
        "add_blueprint_variable",
        asset_path=asset_path,
        variable_name=variable_name,
        type=variable_type,
        object_class=object_class,
        default_value=default_value,
    )


@mcp.tool()
def editorlink_remove_blueprint_variable(
    asset_path: str, variable_name: str, confirm: bool = False
) -> str:
    """Remove a Blueprint member variable. Requires confirm=true and remains unsaved."""
    return _call(
        "remove_blueprint_variable",
        asset_path=asset_path,
        variable_name=variable_name,
        confirm=confirm,
    )


@mcp.tool()
def editorlink_add_blueprint_component(
    asset_path: str, component_name: str, component_class: str
) -> str:
    """Add a component to a Blueprint's component hierarchy."""
    return _call(
        "add_blueprint_component",
        asset_path=asset_path,
        component_name=component_name,
        component_class=component_class,
    )


@mcp.tool()
def editorlink_remove_blueprint_component(
    asset_path: str, component_name: str, confirm: bool = False
) -> str:
    """Remove a Blueprint component. Requires confirm=true."""
    return _call(
        "remove_blueprint_component",
        asset_path=asset_path,
        component_name=component_name,
        confirm=confirm,
    )


@mcp.tool()
def editorlink_create_graph_node(
    asset_path: str,
    graph_name: str,
    node_kind: str,
    x: float = 0,
    y: float = 0,
    event_name: str = "",
    owner_class: str = "",
    function_name: str = "",
    node_class: str = "",
    source_node_guid: str = "",
    auto_place: bool = True,
    horizontal_gap: int = 160,
    vertical_gap: int = 64,
) -> str:
    """Create a Blueprint node and, by default, move it to avoid overlapping existing nodes."""
    return _call(
        "create_graph_node",
        asset_path=asset_path,
        graph_name=graph_name,
        node_kind=node_kind,
        x=x,
        y=y,
        event_name=event_name,
        owner_class=owner_class,
        function_name=function_name,
        node_class=node_class,
        source_node_guid=source_node_guid,
        auto_place=auto_place,
        horizontal_gap=horizontal_gap,
        vertical_gap=vertical_gap,
    )


@mcp.tool()
def editorlink_connect_graph_pins(
    asset_path: str,
    graph_name: str,
    source_node_guid: str,
    source_pin: str,
    target_node_guid: str,
    target_pin: str,
) -> str:
    """Connect one Blueprint output pin to a compatible input pin."""
    return _call(
        "connect_graph_pins",
        asset_path=asset_path,
        graph_name=graph_name,
        node_guid=source_node_guid,
        pin=source_pin,
        target_node_guid=target_node_guid,
        target_pin=target_pin,
    )


@mcp.tool()
def editorlink_disconnect_graph_pin(
    asset_path: str, graph_name: str, node_guid: str, pin: str
) -> str:
    """Disconnect all links from one Blueprint graph pin."""
    return _call(
        "disconnect_graph_pin",
        asset_path=asset_path,
        graph_name=graph_name,
        node_guid=node_guid,
        pin=pin,
    )


@mcp.tool()
def editorlink_set_graph_pin_default(
    asset_path: str, graph_name: str, node_guid: str, pin: str, value: str
) -> str:
    """Set the default text value of an unconnected Blueprint input pin."""
    return _call(
        "set_graph_pin_default",
        asset_path=asset_path,
        graph_name=graph_name,
        node_guid=node_guid,
        pin=pin,
        value=value,
    )


@mcp.tool()
def editorlink_delete_graph_node(
    asset_path: str, graph_name: str, node_guid: str, confirm: bool = False
) -> str:
    """Delete a Blueprint graph node. Set confirm=true to acknowledge the destructive edit."""
    if not confirm:
        return json.dumps(
            {
                "success": False,
                "code": "confirmation_required",
                "error": "Set confirm=true to delete a graph node.",
            },
            indent=2,
        )
    return _call(
        "delete_graph_node",
        asset_path=asset_path,
        graph_name=graph_name,
        node_guid=node_guid,
        confirm=True,
    )


@mcp.tool()
def editorlink_inspect_skeleton(skeleton_path: str) -> str:
    """Inspect skeleton bones, sockets, and animation slot groups."""
    return _call("inspect_skeleton", skeleton_path=skeleton_path)


@mcp.tool()
def editorlink_upsert_skeleton_socket(
    skeleton_path: str,
    socket_name: str,
    bone_name: str,
    location_x: float = 0,
    location_y: float = 0,
    location_z: float = 0,
    pitch: float = 0,
    yaw: float = 0,
    roll: float = 0,
    scale_x: float = 1,
    scale_y: float = 1,
    scale_z: float = 1,
) -> str:
    """Create or update a skeleton socket and its relative transform."""
    return _call(
        "upsert_skeleton_socket",
        skeleton_path=skeleton_path,
        socket_name=socket_name,
        bone_name=bone_name,
        location=_vector(location_x, location_y, location_z),
        rotation=_rotation(pitch, yaw, roll),
        scale=_vector(scale_x, scale_y, scale_z),
    )


@mcp.tool()
def editorlink_delete_skeleton_socket(
    skeleton_path: str, socket_name: str, confirm: bool = False
) -> str:
    """Delete a skeleton socket. Requires confirm=true and supports Undo."""
    return _call(
        "delete_skeleton_socket",
        skeleton_path=skeleton_path,
        socket_name=socket_name,
        confirm=confirm,
    )


@mcp.tool()
def editorlink_add_animation_slot(skeleton_path: str, slot_name: str) -> str:
    """Register an animation montage slot on a skeleton."""
    return _call("add_animation_slot", skeleton_path=skeleton_path, slot_name=slot_name)


@mcp.tool()
def editorlink_get_dirty_packages(limit: int = 200) -> str:
    """List modified project packages that have not been explicitly saved."""
    return _call("get_dirty_packages", limit=limit)


@mcp.tool()
def editorlink_undo() -> str:
    """Undo the most recent supported Unreal Editor transaction."""
    return _call("undo")


@mcp.tool()
def editorlink_redo() -> str:
    """Redo the most recently undone Unreal Editor transaction."""
    return _call("redo")


@mcp.tool()
def editorlink_capture_viewport(file_name: str = "") -> str:
    """Request a screenshot of the active Unreal viewport for visual verification."""
    return _call("capture_viewport", file_name=file_name)


@mcp.tool()
def editorlink_start_play_in_editor() -> str:
    """Start Play In Editor for functional verification."""
    return _call("start_pie")


@mcp.tool()
def editorlink_stop_play_in_editor() -> str:
    """Stop the current Play In Editor session."""
    return _call("stop_pie")


if __name__ == "__main__":
    try:
        mcp.run(transport="stdio")
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        print(f"EditorLink MCP Demo failed: {exc}", file=sys.stderr)
        raise

