# EditorLink MCP Demo

EditorLink MCP Demo connects MCP-compatible coding assistants to Unreal Editor
5.6 through a provider-neutral, typed tool set.

## What the Demo includes

- Project/editor context, asset search, asset inspection, import, duplicate,
  rename, delete, and explicit save.
- Level actors, components, transforms, editable properties, level loading, and
  explicit level save.
- Blueprint and Animation Blueprint inspection, variables, components, graph
  nodes, pin defaults, connections, deletion, and compilation diagnostics.
- Skeleton bones, sockets, socket transforms, and animation montage slots.
- Dirty-package reporting, Undo/Redo, viewport screenshots, and Play In Editor.

The MCP server does not expose arbitrary Python or console execution. The native
bridge listens only on the local machine, generates a new secret token for every
editor session, limits request size, and removes its session descriptor when the
editor closes. Destructive tools require explicit confirmation. Content changes
are not automatically saved.

## Installation

1. Copy `EditorLinkMCPDemo` into the project's `Plugins` folder.
2. Install the Python MCP dependency used by your client:

   `python -m pip install -r Plugins/EditorLinkMCPDemo/Content/Python/requirements.txt`

3. Enable **EditorLink MCP Demo** in Unreal Editor and restart the editor.
4. Open **Tools > EditorLink MCP Demo** to see the live bridge status and copy
   configuration snippets.
5. Add one of the configurations below to the MCP client. Replace
   `C:/Path/To/Project` with the directory containing the `.uproject` file.

Keep Unreal Editor open while using the tools. The session token is discovered
at runtime, so it must never be copied into client configuration.

## TOML clients (Codex and Grok Build)

```toml
[mcp_servers.editorlink]
command = "python"
args = [
  "C:/Path/To/Project/Plugins/EditorLinkMCPDemo/Content/Python/editorlink_mcp_demo_server.py",
  "--project",
  "C:/Path/To/Project"
]
startup_timeout_sec = 20
tool_timeout_sec = 120
```

Codex can use the user configuration or a project-local `.codex/config.toml`.
Grok Build can use the user configuration or a project-local `.grok/config.toml`.

## JSON clients (Claude Code, Kimi Code, and Qwen Code)

```json
{
  "mcpServers": {
    "editorlink": {
      "command": "python",
      "args": [
        "C:/Path/To/Project/Plugins/EditorLinkMCPDemo/Content/Python/editorlink_mcp_demo_server.py",
        "--project",
        "C:/Path/To/Project"
      ]
    }
  }
}
```

- Claude Code can add the server through its MCP command or JSON settings.
- Kimi Code accepts `mcpServers` in `.kimi-code/mcp.json`.
- Qwen Code accepts `mcpServers` in `.qwen/settings.json`.

## DeepSeek models and other MCP hosts

DeepSeek models can use EditorLink through any host application that supports
standard MCP `stdio` servers. Add the same command and arguments shown above to
that host's MCP configuration. EditorLink does not depend on a particular model
vendor; compatibility is determined by the host application's MCP support.

## Useful Unreal class paths

- Actor: `/Script/Engine.StaticMeshActor`
- Scene component: `/Script/Engine.SceneComponent`
- Static mesh component: `/Script/Engine.StaticMeshComponent`
- Blueprint Branch node: choose `node_kind="branch"`
- Blueprint Sequence node: choose `node_kind="sequence"`

Use the inspection tools before editing. Compile a Blueprint after graph or
structure changes, inspect compiler diagnostics, and save only after validation.

