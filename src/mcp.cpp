#include "common.h"
#include <sddl.h>
#include <thread>
#include <atomic>
#include <mutex>

std::wstring Wide(const std::string& s) {
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8. Save this file as UTF-8 or UTF-16 first.");
    std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),w.data(),n); return w;
}
std::string Utf8(const std::wstring& w) {
    if(w.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Text contains invalid Unicode.");
    std::string s(n,0); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr); return s;
}
std::wstring UserSid() {
    HANDLE token=nullptr; if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot identify current user.");
    DWORD size=0; GetTokenInformation(token,TokenUser,nullptr,0,&size); std::vector<BYTE> buffer(size);
    if(!GetTokenInformation(token,TokenUser,buffer.data(),size,&size)) { CloseHandle(token); throw std::runtime_error("Cannot read user token."); }
    LPWSTR sid=nullptr; ConvertSidToStringSidW(((TOKEN_USER*)buffer.data())->User.Sid,&sid);
    CloseHandle(token); if(!sid)throw std::runtime_error("Cannot read SID."); std::wstring result(sid); LocalFree(sid); return result;
}
std::wstring PipeName() { return L"\\\\.\\pipe\\simple-md-"+UserSid(); }
namespace {
std::thread pipeThread;
std::atomic<bool> stopping{true};
HANDLE CreateLocalPipe(const std::wstring& name,SECURITY_ATTRIBUTES* attributes) {
    return CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,1500,attributes);
}
bool ReadExact(HANDLE h,void* data,DWORD size) {
    auto* p=(BYTE*)data;
    while(size) { DWORD n=0; if(!ReadFile(h,p,size,&n,nullptr)||!n)return false; size-=n;p+=n; } return true;
}
bool WriteExact(HANDLE h,const void* data,DWORD size) {
    auto* p=(const BYTE*)data;
    while(size) { DWORD n=0; if(!WriteFile(h,p,size,&n,nullptr)||!n)return false; size-=n;p+=n; } return true;
}
Json ErrorResult(const std::string& message) { return {{"content",Json::array({{{"type","text"},{"text",message}}})},{"isError",true}}; }
Json Forward(const Json& request) {
    auto name=PipeName();
    HANDLE pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE && GetLastError()==ERROR_PIPE_BUSY) { WaitNamedPipeW(name.c_str(),1500); pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr); }
    if(pipe==INVALID_HANDLE_VALUE)return ErrorResult("Open simple-md and enable MCP (Read or Edit) first.");
    auto s=request.dump(); DWORD size=(DWORD)s.size();
    bool ok=size<=MAX_RPC_BYTES && WriteExact(pipe,&size,4) && WriteExact(pipe,s.data(),size);
    if(ok)ok=ReadExact(pipe,&size,4) && size<=MAX_RPC_BYTES;
    std::string reply; if(ok) { reply.resize(size); ok=ReadExact(pipe,reply.data(),size); }
    CloseHandle(pipe);
    if(!ok)return ErrorResult("simple-md connection closed or response exceeded 4 MB.");
    return Json::parse(reply);
}
Json Tool(const std::string& name,const std::string& description,Json properties,Json required,bool readOnly) {
    return {{"name",name},{"description",description},{"inputSchema",{{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}}},
        {"annotations",{{"readOnlyHint",readOnly},{"destructiveHint",!readOnly},{"idempotentHint",readOnly},{"openWorldHint",false}}}};
}
}
Json ToolDefinitions() {
    Json id={{"type","string"},{"description","Document id returned by list_documents"}};
    return Json::array({
        Tool("list_documents","List only documents currently open in simple-md. MCP must be enabled.",Json::object(),Json::array(),true),
        Tool("read_document","Read current editor buffer, including unsaved changes. Large documents may require offset/limit; offsets count Unicode code points. Returns a revision for safe edits.",
            {{"id",id},{"offset",{{"type","integer"},{"minimum",0}}},{"limit",{{"type","integer"},{"minimum",1},{"maximum",200000}}}},Json::array({"id"}),true),
        Tool("update_document","Replace an open document's editor buffer using the revision from read_document. Requires MCP Edit mode. Changes remain UNSAVED; the user reviews and presses Ctrl+S. No filesystem access, no automatic save.",
            {{"id",id},{"revision",{{"type","integer"},{"minimum",0}}},{"content",{{"type","string"},{"maxLength",1000000}}}},Json::array({"id","revision","content"}),false)
    });
}
void StartMcpPipe(HWND window) {
    if(pipeThread.joinable())return;
    PSECURITY_DESCRIPTOR sd=nullptr;
    auto acl=L"D:P(A;;GA;;;"+UserSid()+L")";
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))throw std::runtime_error("Cannot create MCP user permissions.");
    SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};auto name=PipeName();
    HANDLE first=CreateLocalPipe(name,&sa);
    if(first==INVALID_HANDLE_VALUE){LocalFree(sd);throw std::runtime_error("Cannot enable MCP. Another instance may already own the local connection.");}
    LocalFree(sd);
    stopping=false;
    try {pipeThread=std::thread([window,first] {
        HANDLE pipe=first;
        while(!stopping) {
            if(pipe==INVALID_HANDLE_VALUE)break;
            bool connected=ConnectNamedPipe(pipe,nullptr)!=FALSE || GetLastError()==ERROR_PIPE_CONNECTED;
            if(connected && !stopping) {
                DWORD size=0;
                if(ReadExact(pipe,&size,4) && size && size<=MAX_RPC_BYTES) {
                    std::string s(size,0);
                    if(ReadExact(pipe,s.data(),size)) {
                        try {
                            McpCall call{Json::parse(s),{}};
                            // UI owns the buffers; the pipe thread never touches controls.
                            if(SendMessageW(window,WM_MCP_CALL,0,(LPARAM)&call)) {
                                auto reply=call.result.dump(); size=(DWORD)reply.size();
                                if(size<=MAX_RPC_BYTES && WriteExact(pipe,&size,4) && WriteExact(pipe,reply.data(),size))FlushFileBuffers(pipe);
                            }
                        }catch(...) {}
                    }
                }
            }
            DisconnectNamedPipe(pipe);
            if(stopping){CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;break;}
            // Reuse the same server instance so there is no pipe-name ownership gap.
        }
        if(pipe!=INVALID_HANDLE_VALUE)CloseHandle(pipe);
    });}catch(...){CloseHandle(first);stopping=true;throw;}
}
void StopMcpPipe() {
    stopping=true;
    if(!pipeThread.joinable())return;
    // Cancel synchronous connect/read/write; never join a worker waiting on the UI thread.
    CancelSynchronousIo((HANDLE)pipeThread.native_handle());
    // Drain any synchronous UI dispatch before joining.
    while(WaitForSingleObject((HANDLE)pipeThread.native_handle(),0)==WAIT_TIMEOUT) {
        MSG msg; while(PeekMessageW(&msg,nullptr,WM_MCP_CALL,WM_MCP_CALL,PM_REMOVE))DispatchMessageW(&msg);
        CancelSynchronousIo((HANDLE)pipeThread.native_handle());
        Sleep(1);
    }
    pipeThread.join();
}
int RunMcp() {
    HANDLE input=GetStdHandle(STD_INPUT_HANDLE),output=GetStdHandle(STD_OUTPUT_HANDLE);
    std::string pending; char buffer[8192]; DWORD n=0;
    bool initialized=false;
    while(ReadFile(input,buffer,sizeof(buffer),&n,nullptr) && n) {
        pending.append(buffer,n);
        for(;;) {
            auto end=pending.find('\n'); if(end==std::string::npos)break;
            if(end>MAX_RPC_BYTES)return 2;
            std::string line=pending.substr(0,end); pending.erase(0,end+1);
            if(line.empty()||line=="\r")continue;
            Json id=nullptr, response;
            try {
                auto req=Json::parse(line);
                if(!req.is_object())throw std::invalid_argument("Expected a JSON-RPC object.");
                if(req.contains("id"))id=req["id"];
                if(req.value("jsonrpc","")!="2.0" || !req.contains("method") || !req["method"].is_string())throw std::invalid_argument("Invalid JSON-RPC request.");
                auto method=req["method"].get<std::string>();
                if(!req.contains("id"))continue;
                Json result;
                if(method=="initialize") {
                    std::string version=req.value("params",Json::object()).value("protocolVersion","2025-11-25");
                    if(version!="2024-11-05" && version!="2025-03-26" && version!="2025-06-18" && version!="2025-11-25")version="2025-11-25";
                    result={{"protocolVersion",version},{"capabilities",{{"tools",Json::object()}}},{"serverInfo",{{"name","simple-md"},{"version","1.0.0"}}},{"instructions","Enable MCP Read/Edit in the native app. Only open documents are shared. Edits are staged and must be saved by the user."}}; initialized=true;
                } else if(method=="ping")result=Json::object();
                else if(!initialized) { response={{"jsonrpc","2.0"},{"id",id},{"error",{{"code",-32002},{"message","Initialize first."}}}}; }
                else if(method=="tools/list")result={{"tools",ToolDefinitions()}};
                else if(method=="tools/call")result=Forward(req.value("params",Json::object()));
                else { response={{"jsonrpc","2.0"},{"id",id},{"error",{{"code",-32601},{"message","Method not found"}}}}; }
                if(response.is_null())response={{"jsonrpc","2.0"},{"id",id},{"result",result}};
            } catch(const Json::parse_error&) { response={{"jsonrpc","2.0"},{"id",nullptr},{"error",{{"code",-32700},{"message","Parse error"}}}}; }
            catch(const std::invalid_argument& e) { response={{"jsonrpc","2.0"},{"id",id},{"error",{{"code",-32600},{"message",e.what()}}}}; }
            catch(const std::exception& e) { response={{"jsonrpc","2.0"},{"id",id},{"error",{{"code",-32602},{"message",e.what()}}}}; }
            auto wire=response.dump()+"\n"; if(!WriteExact(output,wire.data(),(DWORD)wire.size()))return 1;
        }
        if(pending.size()>MAX_RPC_BYTES)return 2;
    }
    return 0;
}
