# simple-md

An ultra-lightweight native Windows Markdown editor built with C++. Read, edit, and save multiple files with a minimal UI and optional MCP integration for Claude and Codex.

Vietnamese usage guide: [HUONG_DAN.md](HUONG_DAN.md).

> **Status: working first build.** 84 native checks cover document handling, editor behavior, cache eviction, and the local MCP bridge, including a real stdio client connected to the editor. MCP stdio tests also pass. Memory benchmarks are recorded below.

## Goals

- Keep RAM usage and executable size low with native C++ and Win32 controls.
- Use Windows RichEdit for editing and reading Markdown, without a browser engine.
- Provide a clean black-and-white interface with no animations.
- Open multiple `.md` files, switch between Read / Edit / Split, and save with `Ctrl+S`.
- Load document contents on demand and render previews only when needed.
- Offer optional local MCP integration so Claude or Codex can work with open documents.

Memory usage depends on file sizes, the number of loaded documents, and the active view. There is no fixed RAM promise. The current release executable is **492,032 bytes (480.5 KiB)**.

Measured on the development machine (Windows 11 x64, 2026-10-29), using a fresh hidden process and synthetic files for each scenario:

| Scenario | Total working set | Private working set | Private committed memory |
| --- | ---: | ---: | ---: |
| Empty window | 10.42 MiB | 1.05 MiB | 1.64 MiB |
| One 2.67 KB file | 15.71 MiB | 1.66 MiB | 2.39 MiB |
| 30 listed small files, first loaded | 15.70 MiB | 1.65 MiB | 2.39 MiB |
| 178 KB file with preview | 18.29 MiB | 4.25 MiB | 4.97 MiB |
| 1.78 MB file, preview paused | 23.10 MiB | 9.25 MiB | 9.95 MiB |

Total working set includes resident shared Windows DLL pages. Private working set counts resident pages exclusive to this process; private committed memory includes private allocations that may not currently reside in RAM. These metrics are reported separately. Microsoft's [explanation of Task Manager memory](https://blogs.windows.com/msedgedev/2021/01/13/investigate-microsoft-edge-memory-usage/) describes the default Memory column as private working set.

**The requested 5 MB empty-window target has not been reached for total working set.** The measured private working set is below 5 MB in the synthetic empty-window test. A diagnostic program that only creates a bare hidden Win32 window measured 6.48 MiB total working set on this machine; this is an observed comparison, not a universal Windows minimum. No IME/text input support is disabled to reduce memory.

Every scenario recorded 0 ms CPU time during a one-second idle sample. Results vary with Windows, display state, files, and usage; these are observations, not guaranteed limits. No artificial working-set trimming is used. See [current raw results](benchmarks/memory-current.json), [resident module breakdown](benchmarks/memory-empty-modules.json), and [control-window results](benchmarks/memory-floor.json). Earlier [baseline](benchmarks/memory-baseline.json) and [deferred-loading measurements](benchmarks/memory-deferred.json) remain available for comparison.

Clean inactive editor buffers are evicted to keep at most three loaded clean documents and an 8 MiB clean source-text cache. The active file and unsaved documents are preserved even when they exceed that cache budget. Large files keep a shorter undo history. Preview is released in Edit mode and pauses above 400,000 characters. The editor accepts files up to 16 MiB. Folder browsing lists up to 2,000 Markdown files in the selected folder and loads contents on selection.

The empty window does not create an editor, preview, search field, or renderer thread. RichEdit and the renderer are released when the last document closes. File dialog, shell, COM, and MCP permission libraries are loaded only when their features need them.

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

Create a portable ZIP with usage guides, benchmark results, and third-party licenses:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\package.ps1
```

Output: `dist\simple-md-1.0.0-win-x64.zip` and its SHA-256 checksum. The ZIP is generated locally and is not automatically published as a GitHub Release.

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

There is no crash recovery for unsaved buffers in this version; save regularly. UTF-8, UTF-8 BOM, UTF-16 LE/BE, and LF/CRLF are supported. Other encodings need conversion before opening. Inactive clean documents can be reloaded from disk after eviction, so their prior undo history is not retained indefinitely.

## Development

See [PROGRESS.md](PROGRESS.md) for the current checklist. Priorities are low RAM usage, responsive editing, reliable saving, and a simple interface.
