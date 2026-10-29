#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <stdexcept>
#include "json.hpp"
using Json = nlohmann::json;
constexpr size_t MAX_FILE_BYTES = 16 * 1024 * 1024;
constexpr size_t MAX_RPC_BYTES = 4 * 1024 * 1024;
constexpr UINT WM_MCP_CALL = WM_APP + 1;
constexpr UINT WM_PREVIEW_READY = WM_APP + 2;
std::wstring Wide(const std::string& s);
std::string Utf8(const std::wstring& s);
std::wstring PipeName();
std::wstring UserSid();
std::string MarkdownRtf(const std::string& source);
Json ToolDefinitions();
int RunMcp();
void StartMcpPipe(HWND window);
void StopMcpPipe();
struct McpCall { Json request; Json result; };
