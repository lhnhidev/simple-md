# simple-md

An ultra-lightweight native Windows Markdown editor built with C++. Read, edit, and save multiple files with a minimal UI and optional MCP integration for Claude and Codex.

> **Status: work in progress.** This first commit contains the project foundation, Markdown renderer, and MCP bridge. The native editor UI and runnable executable are still being implemented. Features below describe the intended first release, not a finished product.

## Goals

- Keep RAM usage and executable size low with native C++ and Win32 controls.
- Use Windows RichEdit for editing and reading Markdown, without a browser engine.
- Provide a clean black-and-white interface with no animations.
- Open multiple `.md` files, switch between Read / Edit / Split, and save with `Ctrl+S`.
- Load document contents on demand and render previews only when needed.
- Offer optional local MCP integration so Claude or Codex can work with open documents.

Memory usage depends on file sizes, the number of loaded documents, and the active view. There is no fixed 1 MB RAM promise. Executable size and actual memory measurements will be published after the first working build.

## MCP design

- **Off by default.** The user enables Read or Edit access in the application.
- Only documents already open in simple-md are exposed.
- Edit operations modify the editor buffer; changes remain unsaved for user review.
- The user saves files explicitly. MCP has no arbitrary filesystem or shell access.
- The executable runs as an MCP stdio server with `--mcp` and connects to the running editor through a local named pipe restricted to the current Windows user.
- Tools: `list_documents`, `read_document`, and `update_document`.

Client setup instructions will be added with the first working build. No Claude or Codex configuration is changed automatically.

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
tests/        Functional verification (being implemented)
build.ps1     MSVC release build script
PROGRESS.md   Development checklist
```

## Build

Requires Windows 10/11 x64 and Visual Studio / Build Tools with **Desktop development with C++** and a Windows SDK.

The build script is included, but it will become runnable once the remaining application entry point and UI are committed:

```powershell
.\build.ps1
```

The release executable will be generated at `dist\simple-md.exe`. Build outputs are excluded from Git.

## Development

See [PROGRESS.md](PROGRESS.md) for the current checklist. Priorities are low RAM usage, responsive editing, reliable saving, and a simple interface.
