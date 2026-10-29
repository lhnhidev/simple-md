// Test the real document and editor code in an isolated native process.
#include "../src/main.cpp"
#include <iostream>
#include <chrono>
namespace {
int checks=0;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);++checks;}
void Pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
Json Payload(const Json& result){return Json::parse(result.at("content").at(0).at("text").get<std::string>());}
Json PipeCall(Application& app,const Json& request){
    Json result;std::exception_ptr failure;std::atomic<bool> finished{false};
    std::thread client([&]{try{
        auto name=PipeName();HANDLE pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE&&GetLastError()==ERROR_PIPE_BUSY){WaitNamedPipeW(name.c_str(),1000);pipe=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);}
        if(pipe==INVALID_HANDLE_VALUE)throw std::runtime_error("Integration test could not connect to MCP pipe");
        auto send=request.dump();DWORD size=(DWORD)send.size(),n=0;bool ok=WriteFile(pipe,&size,4,&n,nullptr)&&n==4&&WriteFile(pipe,send.data(),size,&n,nullptr)&&n==size;
        ok=ok&&ReadFile(pipe,&size,4,&n,nullptr)&&n==4&&size<=MAX_RPC_BYTES;
        std::string reply;if(ok){reply.resize(size);DWORD used=0;while(used<size){if(!ReadFile(pipe,reply.data()+used,size-used,&n,nullptr)||!n){ok=false;break;}used+=n;}}
        CloseHandle(pipe);if(!ok)throw std::runtime_error("Integration test pipe transfer failed");result=Json::parse(reply);
    }catch(...){failure=std::current_exception();}finished=true;});
    while(!finished){Pump();Sleep(1);}client.join();if(failure)std::rethrow_exception(failure);return result;
}
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HMODULE rich=LoadLibraryW(L"Msftedit.dll");
    wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);std::wstring folder=std::wstring(temp)+L"simple-md-tests-"+std::to_wstring(GetCurrentProcessId());CreateDirectoryW(folder.c_str(),nullptr);
    std::vector<std::wstring> files;
    try{
        std::wstring source=L"# Xin chào Việt Nam\n\n**Đọc và sửa** 🦀\n- [x] xong\n";
        for(auto enc:{Encoding::Utf8,Encoding::Utf8Bom,Encoding::Utf16LE,Encoding::Utf16BE})for(bool crlf:{false,true}){
            auto path=folder+L"\\roundtrip-"+std::to_wstring(files.size())+L".md";files.push_back(path);
            auto bytes=EncodeDocument(source,enc,crlf);AtomicWrite(path,bytes,false);auto disk=ReadDocument(path);
            Check(disk.text==source,"Unicode/newline roundtrip failed");Check(disk.encoding==enc,"Encoding was lost");Check(disk.crlf==crlf,"Line endings were lost");Check(EncodeDocument(disk.text,disk.encoding,disk.crlf)==bytes,"Byte-identical roundtrip failed");
            AtomicWrite(path,EncodeDocument(source+L"updated\n",enc,crlf),true);Check(ReadDocument(path).text==source+L"updated\n","Atomic replacement failed");
        }
        Check(NormalizeLf(L"a\r\nb\rc\n")==L"a\nb\nc\n","Newline normalization failed");
        bool invalid=false;try{Wide("\xff");}catch(...){invalid=true;}Check(invalid,"Invalid UTF-8 was accepted");
        std::string rendered=MarkdownRtf(Utf8(source+L"\n```cpp\n{ code \\ }\n```\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n&amp; &#x1f980;\n"));
        Check(rendered.find("\\b")!=std::string::npos,"Markdown emphasis missing");Check(rendered.find("\\u")!=std::string::npos,"Unicode RTF missing");
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"simple-md.TestWindow";RegisterClassExW(&wc);
        Application app;
        HWND window=CreateWindowExW(0,wc.lpszClassName,L"simple-md tests",WS_OVERLAPPEDWINDOW,0,0,1000,700,nullptr,nullptr,wc.hInstance,&app);
        Check(window!=nullptr,"Test window creation failed");
        app.Open(files[0]);Check(app.Current()!=nullptr,"Document did not open");Check(ControlText(app.Current()->editor)==source+L"updated\n","Editor changed source text");
        auto* d=app.Current();auto id=std::to_string(d->id);
        Check(app.Mcp({{"name","list_documents"}})["isError"].get<bool>(),"MCP default should be off");
        app.mcp=1;
        auto listed=app.Mcp({{"name","list_documents"}});Check(!listed["isError"].get<bool>()&&Payload(listed).size()==1,"MCP listing failed");
        auto read=app.Mcp({{"name","read_document"},{"arguments",{{"id",id},{"offset",0},{"limit",20}}}});
        Check(!read["isError"].get<bool>()&&Payload(read)["hasMore"].get<bool>(),"MCP pagination failed");
        auto rev=d->revision;
        Json update={{"name","update_document"},{"arguments",{{"id",id},{"revision",rev},{"content",Utf8(source)}}}};
        Check(app.Mcp(update)["isError"].get<bool>(),"Read-only MCP accepted an edit");
        app.mcp=2;Check(!app.Mcp(update)["isError"].get<bool>(),"MCP edit failed");
        Check(d->dirty&&d->revision>rev,"MCP change was not tracked");Check(ControlText(d->editor)==source,"MCP update corrupted Unicode");
        Check(ReadDocument(files[0]).text==source+L"updated\n","MCP edit unexpectedly saved the file");
        Check(app.Mcp(update)["isError"].get<bool>(),"Stale revision was accepted");
        auto beforeUndo=d->revision;SendMessageW(d->editor,EM_UNDO,0,0);
        Check(ControlText(d->editor)==source+L"updated\n"&&d->revision>beforeUndo,"Undo after MCP edit failed");
        Check(app.Save(*d),"Saving editor failed");Check(!d->dirty,"Save did not clear dirty state");Check(ReadDocument(files[0]).text==ControlText(d->editor),"Saved text differs from buffer");
        auto unknown=app.Mcp({{"name","read_document"},{"arguments",{{"id","not-open"}}}});Check(unknown["isError"].get<bool>(),"MCP exposed an unopened document");
        auto malformed=app.Mcp({{"name","read_document"},{"arguments",{{"id",id},{"offset",-1}}}});Check(malformed["isError"].get<bool>(),"Invalid pagination accepted");
        // Code point pagination must not split surrogate pairs.
        SetRichText(d->editor,L"a🦀b");
        auto emoji=Payload(app.Mcp({{"name","read_document"},{"arguments",{{"id",id},{"offset",1},{"limit",1}}}}));
        Check(emoji["content"].get<std::string>()==Utf8(L"🦀"),"Pagination split a surrogate pair");
        app.Save(*d);
        app.McpMode(1);
        auto pipeList=PipeCall(app,{{"name","list_documents"},{"arguments",Json::object()}});
        Check(!pipeList["isError"].get<bool>()&&Payload(pipeList).size()==1,"Named pipe listing failed");
        auto pipeRead=PipeCall(app,{{"name","read_document"},{"arguments",{{"id",id}}}});
        Check(Payload(pipeRead)["content"].get<std::string>()=="a🦀b","Named pipe read corrupted Unicode");
        Json pipeUpdate={{"name","update_document"},{"arguments",{{"id",id},{"revision",d->revision},{"content","pipe edit"}}}};
        Check(PipeCall(app,pipeUpdate)["isError"].get<bool>(),"Named pipe ignored Read permission");
        app.McpMode(2);
        Check(!PipeCall(app,pipeUpdate)["isError"].get<bool>()&&ControlText(d->editor)==L"pipe edit","Named pipe edit failed");
        app.Save(*d);app.McpMode(0);
        HANDLE disabled=CreateFileW(PipeName().c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        Check(disabled==INVALID_HANDLE_VALUE,"MCP listener remained after disabling");if(disabled!=INVALID_HANDLE_VALUE)CloseHandle(disabled);
        // Restart, then cancel a connected client that sends no data.
        app.McpMode(1);HANDLE stalled=CreateFileW(PipeName().c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        Check(stalled!=INVALID_HANDLE_VALUE,"MCP restart failed");app.McpMode(0);if(stalled!=INVALID_HANDLE_VALUE)CloseHandle(stalled);
        for(size_t i=1;i<files.size();++i)app.Open(files[i]);
        size_t loaded=0;for(auto& doc:app.documents)if(doc->loaded)++loaded;Check(loaded<=3,"Clean editor buffer budget was exceeded");
        // Unsaved buffers must remain in memory, even while clean buffers are evicted.
        auto* retained=app.Current();SetRichText(retained->editor,L"unsaved");for(size_t i=0;i<5;++i)app.Select((int)i);Check(retained->loaded&&retained->dirty&&ControlText(retained->editor)==L"unsaved","Unsaved buffer was evicted");
        app.Save(*retained);
        auto large=folder+L"\\large.md";files.push_back(large);std::string big;for(int i=0;i<20000;++i)big+="# Heading\n\nA paragraph with **bold**, text, and a list.\n\n- first\n- second\n\n";AtomicWrite(large,big,false);
        auto started=std::chrono::steady_clock::now();app.Open(large);auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        Check(ControlText(app.Current()->editor).size()==big.size(),"Large file was truncated");
        app.QueuePreview();for(int i=0;i<10;++i){Pump();Sleep(10);}Check(PlainText(app.preview).find(L"Large document")!=std::wstring::npos,"Large preview guard did not activate");
        app.View(1);Check(PlainText(app.preview).empty(),"Edit mode retained rendered preview");
        for(int i=0;i<2;++i){auto cached=folder+L"\\large-cache-"+std::to_wstring(i)+L".md";files.push_back(cached);AtomicWrite(cached,big,false);app.Open(cached);}
        size_t cachedBytes=0;for(auto& doc:app.documents)if(doc->loaded&&!doc->dirty){GETTEXTLENGTHEX length{GTL_NUMCHARS,1200};cachedBytes+=(size_t)SendMessageW(doc->editor,EM_GETTEXTLENGTHEX,(WPARAM)&length,0)*2;}
        Check(cachedBytes<=8*1024*1024,"Clean source payload exceeded the cache budget");
        // Reloading an evicted file must invalidate previous MCP revisions if disk changed.
        auto* evicted=app.documents[0].get();Check(!evicted->loaded,"Expected an inactive buffer to be evicted");auto oldRevision=evicted->revision;
        AtomicWrite(evicted->path,"changed externally\n",true);
        auto reloaded=app.Mcp({{"name","read_document"},{"arguments",{{"id",std::to_string(evicted->id)}}}});
        // MCP is Off after the cancellation test; enable read without starting a listener.
        Check(reloaded["isError"].get<bool>(),"Disabled MCP became accessible during cache activity");
        app.mcp=1;reloaded=app.Mcp({{"name","read_document"},{"arguments",{{"id",std::to_string(evicted->id)}}}});
        Check(!reloaded["isError"].get<bool>()&&Payload(reloaded)["revision"].get<uint64_t>()>oldRevision,"Evicted file reload did not update its revision");
        app.mcp=0;Check(app.Close(),"Application failed to close");DestroyWindow(window);
        for(auto& file:files)DeleteFileW(file.c_str());RemoveDirectoryW(folder.c_str());
        std::cout<<"PASS: "<<checks<<" checks; large file "<<big.size()<<" bytes opened in "<<elapsed<<" ms.\n";
        FreeLibrary(rich);CoUninitialize();return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
