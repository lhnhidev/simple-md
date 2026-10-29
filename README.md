# simple-md

An ultra-lightweight native Windows Markdown editor built with C++. Read, edit, and save multiple files with a minimal UI and optional MCP integration for Claude and Codex.

> **Status: working first build.** Document handling, MCP stdio, and the local MCP bridge pass automated tests. Memory benchmarks are recorded below. Portable packaging is in progress.

## Goals

- Keep RAM usage and executable size low with native C++ and Win32 controls.
- Use Windows RichEdit for editing and reading Markdown, without a browser engine.
- Provide a clean black-and-white interface with no animations.
- Open multiple `.md` files, switch between Read / Edit / Split, and save with `Ctrl+S`.
- Load document contents on demand and render previews only when needed.
- Offer optional local MCP integration so Claude or Codex can work with open documents.

Memory usage depends on file sizes, the number of loaded documents, and the active view. There is no fixed 1 MB RAM promise. The current release executable is **488,448 bytes (477 KiB)**.

Measured on the development machine (Windows 11 x64, 2026-10-29), using a fresh hidden process and synthetic files for each scenario:

| Scenario | Working set | Private committed memory |
| --- | ---: | ---: |
| Empty window | 15.07 MiB | 2.39 MiB |
| One 2.67 KB file | 17.55 MiB | 2.74 MiB |
| 30 listed small files, first loaded | 17.58 MiB | 2.77 MiB |
| 178 KB file with preview | 19.15 MiB | 4.29 MiB |
| 1.78 MB file, preview paused | 25.06 MiB | 10.55 MiB |

Working set includes resident shared Windows DLL pages; private committed memory is a different metric and is not a substitute for total RAM usage. Every scenario recorded 0 ms CPU time during a one-second idle sample. A visible empty-window spot check was around 18 MiB working set. Results vary with Windows, display state, files, and usage; these are observations, not guaranteed limits. No artificial working-set trimming is used. See [raw results and methodology](benchmarks/memory-baseline.json).

Clean inactive editor buffers are evicted to keep at most three loaded clean documents and an 8 MiB clean source-text cache. The active file and unsaved documents are preserved even when they exceed that cache budget. Large files keep a shorter undo history. Preview is released in Edit mode and pauses above 400,000 characters. The editor accepts files up to 16 MiB. Folder browsing lists up to 2,000 Markdown files in the selected folder and loads contents on selection.

## MCP design

- **Off by default.** The user enables Read or Edit access in the application.
- Only documents already open in simple-md are exposed.
- Edit operations modify the editor buffer; changes remain unsaved for user review.
- The user saves files explicitly. MCP has no arbitrary filesystem or shell access.
- The executable runs as an MCP stdio server with `--mcp` and connects to the running editor through a local named pipe restricted to the current Windows user.
- Tools: `list_documents`, `read_document`, and `update_document`.

Use **MCP: Off → Copy connection setup** to copy client snippets with the executable's actual path. Merge the relevant section into your client configuration, then choose **Read** or **Edit** in simple-md. MCP returns to Off at every launch. No Claude or Codex configuration is changed automatically.

For Codex, add to `%USERPROFILE%\.codex\config.toml`:

```toml
[mcp_servers.simple-md]
command = 'D:\workspace\simple-md\dist\simple-md.exe'
args = ["--mcp"]
```

For Claude Desktop, merge into `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "simple-md": {
      "command": "D:\\workspace\\simple-md\\dist\\simple-md.exe",
      "args": ["--mcp"]
    }
  }
}
```

Adjust the path if you move the executable. The app implements the initialize-based MCP revisions through `2025-11-25` over stdio.

## Technology

| Component | Technology |
| --- | --- |
| Language | C++17 |
| UI | Win32 API + Windows RichEdit |
| Compiler | Microsoft Visual C++ (MSVC), x64 |
| Markdown parser | [md4c 0.5.2](https://github.com/mity/md4c), MIT |
| JSON / MCP messages | [nlohmann/json 3.11.3](https://github.com/nlohmann/json), MIT |
| Distribution target | Portable Windows `.exe` |

Third-party licenses are included in `vendor/`.

## Project structure

```text
src/          Native application, Markdown renderer, and MCP bridge
vendor/       Pinned third-party source code and licenses
tests/        Native document/editor checks and MCP stdio tests
build.ps1     MSVC release build script
PROGRESS.md   Development checklist
```

## Build

Requires Windows 10/11 x64 and Visual Studio / Build Tools with **Desktop development with C++** and a Windows SDK.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
.\dist\simple-md.exe
```

Run automated checks (Node.js is needed for the MCP protocol test):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\test.ps1
```

Reproduce the memory benchmark with the editor closed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\measure-memory.ps1
```

The release executable is generated at `dist\simple-md.exe`. It uses system Windows components and requires no .NET, Node.js, or browser runtime to run. Build outputs are excluded from Git.

## Keyboard shortcuts

| Action | Shortcut |
| --- | --- |
| New / open | Ctrl+N / Ctrl+O |
| Save / save as | Ctrl+S / Ctrl+Shift+S |
| Close document | Ctrl+W |
| Next / previous document | Ctrl+Tab / Ctrl+Shift+Tab |
| Read / Edit / Split | Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Find / next match | Ctrl+F / F3 |
| Bold / italic Markdown | Ctrl+B / Ctrl+I |

The read view supports headings, emphasis, lists, task lists, quotes, code, and text tables. Images appear as labels and are not downloaded. Raw HTML remains text. The current preview does not load remote content or open links.

## Development

See [PROGRESS.md](PROGRESS.md) for the current checklist. Priorities are low RAM usage, responsive editing, reliable saving, and a simple interface.
