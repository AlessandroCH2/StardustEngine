# AI Agent

**Window → AI Agent** opens a chat with an AI model that works on your project. It can read and change the open
scene, write and fix C# scripts, read the console and the compiler errors, and look up the scripting API.

## Providers

| Provider | What you need | Default address |
|---|---|---|
| Anthropic (Claude) | an API key from console.anthropic.com | `https://api.anthropic.com` |
| OpenAI | an API key | `https://api.openai.com/v1` |
| Google Gemini | an API key from Google AI Studio | `https://generativelanguage.googleapis.com/v1beta` |
| Ollama (local) | Ollama running, a model with tool support pulled (`ollama pull qwen3`) | `http://localhost:11434/v1` |
| LM Studio (local) | the local server started, a model with tool support loaded | `http://localhost:1234/v1` |
| Custom | any server that speaks the OpenAI Chat Completions format (vLLM, llama.cpp server, proxies) | `http://localhost:8080/v1` |

1. Press the provider and model button next to Send and choose the provider.
2. Press the gear and paste the API key (local servers usually need none), then **Save**. For OpenAI, local and
   custom servers you can change the server address there too.
3. Type the model name in the same menu, or pick one from the models the server lists there.

Keys and settings are kept in `%APPDATA%\Stardust\ai-settings.json`, never in the project, so they do not end
up in a repository or a build. Without a saved key the agent reads `ANTHROPIC_API_KEY`, `OPENAI_API_KEY` or
`GEMINI_API_KEY` (or `GOOGLE_API_KEY`) from the environment.

For Anthropic the **Effort** setting (low … max) trades speed and cost against depth; `high` is the default.
The requests ask for Anthropic's fallback model when a request is declined, so a reply comes back instead of an
error.

## What the agent can do

| Tool | What it does |
|---|---|
| list_entities, get_entity | the scene hierarchy; one object as JSON with every component |
| create_entity, edit_entity | create objects (cube, sphere, plane, cylinder or empty) and change any component with a JSON merge patch; reparent |
| delete_entity | delete an object (asks first) |
| component_reference | the default JSON of each component, to add one |
| find_api | searches the C# API of the engine and of your scripts, with the `///` documentation |
| list_files, read_file | the project's files |
| write_file, edit_file, delete_file | create and change files; replacing, editing or deleting an existing file asks first |
| compile_scripts | compiles the scripts and returns the errors |
| get_console | the last lines of the console |
| get_selection, select_entities | what you selected; selecting objects to show you something |
| save_scene | saves the open scene (only when you ask) |

**Undo:** every change to the scene can be undone with **Ctrl+Z**, like an edit you made yourself. File changes
cannot, so they need your approval: **Allow**, **Allow for this chat** (no more questions until **New chat**) or
**Deny**. The agent stops after 30 steps in a row and asks you to say "continue".

The agent keeps working when its window is closed. The square button cancels the current request; **New**
(or **+**) starts a new chat.

## The chat

- Each action of the agent is a card: a spinner while it runs, a check or a cross when done. Click a card to
  see what the agent sent and what came back.
- Every text in the chat can be selected and copied (Ctrl+C, or right-click → Copy). The icon under an answer
  copies all of it.
- **Chats** lists the earlier chats of the project; open one to continue it. They are kept in
  `Library/AgentChats.json` of the project (not in the repository).
- The provider and model are chosen with the button next to Send; the gear holds the API key, the server
  address and the effort.

## Examples

- "Add a row of five crates with box colliders and rigidbodies in front of the camera"
- "Write a script that makes Player jump with Space, compile it and attach it"
- "Why do my scripts not compile?"
- "Make every point light in the scene 20% warmer"

## How it works

The chat, the provider formats and the tools are C# (`ScriptCore/Source/EditorAgent.cs`); they call the
providers' HTTP APIs directly, so the engine needs no provider SDK. The scene tools reach the editor through
`Native.Query` requests whose kind starts with `agent` (`Editor/src/AgentQueries.cpp`), and the window is
`Editor/src/AiAgent.cpp`. The runtime tests run the three wire formats against a local mock server.
