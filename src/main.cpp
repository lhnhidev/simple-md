#include "common.h"
#include "document.h"
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <algorithm>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace {
constexpr wchar_t WINDOW_CLASS[]=L"simple-md.NativeWindow";
enum { ID_NEW=100,ID_OPEN,ID_FOLDER,ID_SAVE,ID_READ,ID_EDIT,ID_SPLIT,ID_MCP,ID_MORE,ID_CLOSE,ID_SAVEAS,ID_FIND,ID_FINDNEXT,ID_NEXTDOC,ID_PREVDOC,ID_FINDCLOSE,ID_FINDINPUT,ID_FILES,ID_BOLD,ID_ITALIC,ID_MCP_OFF,ID_MCP_READ,ID_MCP_EDIT,ID_MCP_SETUP,ID_HELP };
constexpr COLORREF INK=RGB(23,23,23),MUTED=RGB(112,112,112),LINE=RGB(232,232,232),PAPER=RGB(255,255,255),SOFT=RGB(247,247,247);
struct Document {
    unsigned id=0;
    uint64_t touched=0;
    std::wstring path,title;
    HWND editor=nullptr;
    bool dirty=false,loaded=false;
    Encoding encoding=Encoding::Utf8;
    bool crlf=false;
    uint64_t fingerprint=0,revision=1;
};
struct Preview { unsigned document=0; uint64_t generation=0; std::string rtf; };
struct Stream { const std::string* value; size_t position=0; };
DWORD CALLBACK RtfStream(DWORD_PTR cookie,LPBYTE buffer,LONG count,LONG* copied) {
    auto& s=*(Stream*)cookie; *copied=(LONG)std::min((size_t)count,s.value->size()-s.position);
    memcpy(buffer,s.value->data()+s.position,*copied); s.position+=*copied; return 0;
}
std::wstring ControlText(HWND window) {
    GETTEXTLENGTHEX len{GTL_NUMCHARS|GTL_PRECISE,1200};
    LRESULT n=SendMessageW(window,EM_GETTEXTLENGTHEX,(WPARAM)&len,0);
    std::wstring s((size_t)n+2,0); GETTEXTEX get{(DWORD)(s.size()*sizeof(wchar_t)),GT_DEFAULT,1200,nullptr,nullptr};
    auto used=SendMessageW(window,EM_GETTEXTEX,(WPARAM)&get,(LPARAM)s.data()); s.resize((size_t)used); return NormalizeLf(s);
}
std::wstring PlainText(HWND window) { int n=GetWindowTextLengthW(window); std::wstring s(n+1,0); GetWindowTextW(window,s.data(),n+1);s.resize(n);return s; }
void SetRichText(HWND window,const std::wstring& text) { SETTEXTEX set{ST_DEFAULT,1200};SendMessageW(window,EM_SETTEXTEX,(WPARAM)&set,(LPARAM)text.c_str()); }
void Fill(HDC dc,RECT rect,COLORREF color) { HBRUSH b=CreateSolidBrush(color);FillRect(dc,&rect,b);DeleteObject(b); }
void Label(HDC dc,HFONT font,std::wstring text,RECT rect,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text.c_str(),(int)text.size(),&rect,flags);SelectObject(dc,old);
}
class Application {
public:
    HWND window=nullptr,list=nullptr,preview=nullptr,findInput=nullptr,findNext=nullptr,findClose=nullptr;
    HFONT ui=nullptr,bold=nullptr,smallFont=nullptr,heading=nullptr,mono=nullptr;
    HBRUSH white=CreateSolidBrush(PAPER);
    std::vector<std::unique_ptr<Document>> documents;
    std::vector<HWND> buttons;
    int selected=-1,mode=0,mcp=0,dpi=96;
    bool loading=false,findVisible=false,externalChecking=false,closing=false;
    unsigned nextId=1;
    uint64_t clock=0;
    std::wstring notice;
    std::thread renderer;
    std::mutex renderMutex;
    std::condition_variable renderCondition;
    std::string pendingSource;
    unsigned pendingDoc=0;
    uint64_t generation=0,pendingGeneration=0;
    bool renderStop=false,renderPending=false;

    ~Application() { StopWorker(); DeleteObject(white);DeleteFonts(); }
    int P(int value)const{return MulDiv(value,dpi,96);}
    Document* Current(){return selected>=0&&selected<(int)documents.size()?documents[selected].get():nullptr;}
    void DeleteFonts(){for(auto f:{ui,bold,smallFont,heading,mono})if(f)DeleteObject(f);ui=bold=smallFont=heading=mono=nullptr;}
    HFONT Font(int size,int weight,const wchar_t* face){return CreateFontW(-MulDiv(size,dpi,72),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);}
    void Fonts() {
        HFONT oldUi=ui,oldBold=bold,oldSmall=smallFont,oldHeading=heading,oldMono=mono;
        ui=Font(10,FW_NORMAL,L"Segoe UI"); bold=Font(10,FW_SEMIBOLD,L"Segoe UI");smallFont=Font(9,FW_NORMAL,L"Segoe UI");heading=Font(24,FW_SEMIBOLD,L"Segoe UI");mono=Font(11,FW_NORMAL,L"Consolas");
        for(HWND b:buttons)SendMessageW(b,WM_SETFONT,(WPARAM)ui,TRUE);
        for(HWND b:{list,findInput,findNext,findClose})if(b)SendMessageW(b,WM_SETFONT,(WPARAM)ui,TRUE);
        if(list)SendMessageW(list,LB_SETITEMHEIGHT,0,P(44));
        for(auto& d:documents)if(d->editor)SendMessageW(d->editor,WM_SETFONT,(WPARAM)mono,TRUE);
        for(auto f:{oldUi,oldBold,oldSmall,oldHeading,oldMono})if(f)DeleteObject(f);
    }
    HWND Button(const wchar_t* text,int id){HWND b=CreateWindowW(L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,(HMENU)(INT_PTR)id,nullptr,nullptr);buttons.push_back(b);SendMessageW(b,WM_SETFONT,(WPARAM)ui,TRUE);return b;}
    void Create() {
        dpi=(int)GetDpiForWindow(window);Fonts();
        Button(L"New",ID_NEW);Button(L"Open",ID_OPEN);Button(L"Folder",ID_FOLDER);Button(L"Save",ID_SAVE);
        Button(L"Read",ID_READ);Button(L"Edit",ID_EDIT);Button(L"Split",ID_SPLIT);Button(L"MCP: Off",ID_MCP);Button(L"More",ID_MORE);
        list=CreateWindowExW(0,L"LISTBOX",L"Open documents",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT|LBS_HASSTRINGS,0,0,0,0,window,(HMENU)ID_FILES,nullptr,nullptr);
        SendMessageW(list,WM_SETFONT,(WPARAM)ui,FALSE);SendMessageW(list,LB_SETITEMHEIGHT,0,P(44));
        preview=CreateWindowExW(0,MSFTEDIT_CLASS,L"Markdown preview",WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_READONLY,0,0,0,0,window,nullptr,nullptr,nullptr);
        SendMessageW(preview,EM_SETBKGNDCOLOR,0,PAPER);SendMessageW(preview,EM_EXLIMITTEXT,0,MAX_FILE_BYTES*2);
        SendMessageW(preview,EM_SETUNDOLIMIT,0,0);
        findInput=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,window,(HMENU)ID_FINDINPUT,nullptr,nullptr);
        SendMessageW(findInput,WM_SETFONT,(WPARAM)ui,FALSE);SendMessageW(findInput,EM_SETCUEBANNER,TRUE,(LPARAM)L"Find in document…");
        findNext=Button(L"Next",ID_FINDNEXT);findClose=Button(L"Close",ID_FINDCLOSE);
        ShowWindow(findNext,SW_HIDE);ShowWindow(findClose,SW_HIDE);
        DragAcceptFiles(window,TRUE);
        renderer=std::thread([this] {
            for(;;) {
                std::string source;unsigned id;uint64_t gen;
                { std::unique_lock<std::mutex> lock(renderMutex);renderCondition.wait(lock,[this]{return renderStop||renderPending;});if(renderStop)return;
                    source.swap(pendingSource);id=pendingDoc;gen=pendingGeneration;renderPending=false; }
                auto result=std::make_unique<Preview>();result->document=id;result->generation=gen;
                try { result->rtf=MarkdownRtf(source); }catch(...) { result->rtf=MarkdownRtf("Preview could not be rendered. Switch to Edit to read the source."); }
                if(PostMessageW(window,WM_PREVIEW_READY,0,(LPARAM)result.get()))result.release();
            }
        });
        Layout();
    }
    void StopWorker(){ {std::lock_guard<std::mutex> lock(renderMutex);renderStop=true;}renderCondition.notify_one();if(renderer.joinable())renderer.join(); }
    void Error(const std::exception& e){MessageBoxW(window,Wide(e.what()).c_str(),L"simple-md",MB_OK|MB_ICONWARNING);}
    void RefreshList() {
        SendMessageW(list,WM_SETREDRAW,FALSE,0);SendMessageW(list,LB_RESETCONTENT,0,0);
        for(auto& d:documents)SendMessageW(list,LB_ADDSTRING,0,(LPARAM)d->title.c_str());
        SendMessageW(list,LB_SETCURSEL,selected,0);SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
        auto* d=Current();std::wstring title=L"simple-md";if(d)title=d->title+(d->dirty?L" *":L"")+L" — simple-md";SetWindowTextW(window,title.c_str());
        InvalidateRect(window,nullptr,FALSE);
    }
    void Editor(Document& d,const std::wstring& text) {
        loading=true;
        d.editor=CreateWindowExW(0,MSFTEDIT_CLASS,L"Markdown source",WS_CHILD|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_WANTRETURN|ES_NOHIDESEL,0,0,0,0,window,nullptr,nullptr,nullptr);
        SendMessageW(d.editor,EM_SETTEXTMODE,TM_PLAINTEXT,0);SendMessageW(d.editor,EM_EXLIMITTEXT,0,MAX_FILE_BYTES);
        SendMessageW(d.editor,EM_SETUNDOLIMIT,80,0);SendMessageW(d.editor,EM_SETBKGNDCOLOR,0,PAPER);SendMessageW(d.editor,WM_SETFONT,(WPARAM)mono,FALSE);
        SendMessageW(d.editor,EM_SETEVENTMASK,0,ENM_CHANGE|ENM_SELCHANGE);
        SetRichText(d.editor,EditorLines(text));SendMessageW(d.editor,EM_EMPTYUNDOBUFFER,0,0);SendMessageW(d.editor,EM_SETMODIFY,FALSE,0);
        loading=false;d.loaded=true;
    }
    void Load(Document& d) { if(d.loaded)return;auto disk=ReadDocument(d.path);d.encoding=disk.encoding;d.crlf=disk.crlf;d.fingerprint=disk.fingerprint;Editor(d,disk.text); }
    void Select(int index) {
        if(index<0||index>=(int)documents.size())return;
        try { Load(*documents[index]); }catch(...) {SendMessageW(list,LB_SETCURSEL,selected,0);throw;}
        if(auto* old=Current())if(old->editor)ShowWindow(old->editor,SW_HIDE);
        selected=index;documents[index]->touched=++clock;notice.clear();++generation;SetRichText(preview,L"");TrimEditors();RefreshList();Layout();QueuePreview();
        if(auto* d=Current())SetFocus(mode==0?preview:d->editor);
    }
    void TrimEditors() {
        // Keep at most three clean buffers. Unsaved documents are never evicted.
        std::vector<Document*> clean;for(auto& d:documents)if(d->loaded&&!d->dirty)clean.push_back(d.get());
        std::sort(clean.begin(),clean.end(),[](Document* a,Document* b){return a->touched<b->touched;});
        size_t excess=clean.size()>3?clean.size()-3:0;
        for(auto* d:clean){if(!excess)break;if(d==Current())continue;DestroyWindow(d->editor);d->editor=nullptr;d->loaded=false;--excess;}
    }
    void Open(const std::wstring& input,bool activate=true) {
        auto path=AbsolutePath(input);auto attrs=GetFileAttributesW(path.c_str());if(attrs==INVALID_FILE_ATTRIBUTES || attrs&FILE_ATTRIBUTE_DIRECTORY)throw std::runtime_error("File does not exist or is a directory.");
        auto name=FileName(path);auto ext=name.find_last_of(L'.');std::wstring suffix=ext==std::wstring::npos?L"":name.substr(ext);std::transform(suffix.begin(),suffix.end(),suffix.begin(),::towlower);
        if(suffix!=L".md"&&suffix!=L".markdown"&&suffix!=L".mdown"&&suffix!=L".txt")throw std::runtime_error("Open a .md, .markdown, .mdown, or .txt file.");
        for(size_t i=0;i<documents.size();++i)if(_wcsicmp(documents[i]->path.c_str(),path.c_str())==0){if(activate)Select((int)i);return;}
        auto d=std::make_unique<Document>();d->id=nextId++;d->path=path;d->title=name;documents.push_back(std::move(d));RefreshList();
        if(activate)Select((int)documents.size()-1);
    }
    void New() {
        auto d=std::make_unique<Document>();d->id=nextId++;d->title=L"Untitled "+std::to_wstring(d->id)+L".md";Editor(*d,L"");documents.push_back(std::move(d));mode=1;Select((int)documents.size()-1);
    }
    void OpenDialog() {
        std::vector<wchar_t> buffer(65536);OPENFILENAMEW of{sizeof(of)};of.hwndOwner=window;of.lpstrFilter=L"Markdown and text\0*.md;*.markdown;*.mdown;*.txt\0All files\0*.*\0";of.lpstrFile=buffer.data();of.nMaxFile=(DWORD)buffer.size();of.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&of))return;
        std::wstring first=buffer.data();wchar_t* rest=buffer.data()+first.size()+1;
        if(!*rest)Open(first);else {int firstIndex=(int)documents.size();while(*rest){Open(first+L"\\"+rest,false);rest+=wcslen(rest)+1;}Select(std::min(firstIndex,(int)documents.size()-1));}
    }
    void FolderDialog() {
        IFileOpenDialog* dialog=nullptr;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
        DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);dialog->SetTitle(L"Open Markdown files in a folder (contents load on selection)");
        std::wstring folder;
        if(SUCCEEDED(dialog->Show(window))) {IShellItem* item=nullptr;if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){folder=path;CoTaskMemFree(path);}item->Release();}}
        dialog->Release();if(folder.empty())return;
        WIN32_FIND_DATAW entry{};HANDLE search=FindFirstFileW((folder+L"\\*").c_str(),&entry);if(search==INVALID_HANDLE_VALUE)return;
        std::vector<std::wstring> paths;
        do {if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;std::wstring name=entry.cFileName;auto p=name.find_last_of(L'.');if(p==std::wstring::npos)continue;auto ext=name.substr(p);std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);if(ext==L".md"||ext==L".markdown"||ext==L".mdown")paths.push_back(folder+L"\\"+name);}while(paths.size()<2000&&FindNextFileW(search,&entry));FindClose(search);
        std::sort(paths.begin(),paths.end());int first=(int)documents.size();
        // Batch list updates; only the selected file gets a text control.
        for(auto& path:paths){bool exists=false;for(auto& d:documents)if(_wcsicmp(d->path.c_str(),path.c_str())==0){exists=true;break;}if(exists)continue;auto d=std::make_unique<Document>();d->id=nextId++;d->path=path;d->title=FileName(path);documents.push_back(std::move(d));}
        RefreshList();if(!paths.empty())Select(std::min(first,(int)documents.size()-1));else notice=L"No Markdown files found in this folder.";InvalidateRect(window,nullptr,FALSE);
    }
    bool Save(Document& d,bool saveAs=false) {
        if(!d.loaded)return true;
        std::wstring path=d.path;bool newPath=path.empty()||saveAs;
        if(newPath) {
            std::vector<wchar_t> buffer(32768);auto suggested=d.path.empty()?d.title:d.path;wcsncpy_s(buffer.data(),buffer.size(),suggested.c_str(),_TRUNCATE);
            OPENFILENAMEW of{sizeof(of)};of.hwndOwner=window;of.lpstrFilter=L"Markdown\0*.md\0Text\0*.txt\0";of.lpstrDefExt=L"md";of.lpstrFile=buffer.data();of.nMaxFile=(DWORD)buffer.size();of.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST;
            if(!GetSaveFileNameW(&of))return false;path=AbsolutePath(buffer.data());
            for(auto& other:documents)if(other.get()!=&d&&!other->path.empty()&&_wcsicmp(other->path.c_str(),path.c_str())==0){MessageBoxW(window,L"That file is already open in another document. Save to a different path.",L"simple-md",MB_OK|MB_ICONWARNING);return false;}
        }
        if(!d.dirty&&!newPath)return true;
        bool exists=GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES;
        if(!newPath) {
            bool conflict=!exists;
            if(exists) { auto latest=ReadDocument(path);conflict=latest.fingerprint!=d.fingerprint; }
            if(conflict && MessageBoxW(window,L"This file changed or was removed outside simple-md. Replace it with your editor content?",L"File changed on disk",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return false;
        }
        auto text=ControlText(d.editor);auto bytes=EncodeDocument(text,d.encoding,d.crlf);AtomicWrite(path,bytes,exists);
        d.path=path;d.title=FileName(path);d.fingerprint=Fingerprint(bytes);d.dirty=false;SendMessageW(d.editor,EM_SETMODIFY,FALSE,0);notice=L"Saved";RefreshList();return true;
    }
    bool Confirm(Document& d) {if(!d.dirty)return true;auto result=MessageBoxW(window,(L"Save changes to "+d.title+L"?").c_str(),L"Unsaved changes",MB_YESNOCANCEL|MB_ICONQUESTION);return result==IDNO||(result==IDYES&&Save(d));}
    void CloseDocument() {auto* d=Current();if(!d||!Confirm(*d))return;if(d->editor)DestroyWindow(d->editor);documents.erase(documents.begin()+selected);selected=-1;++generation;SetRichText(preview,L"");if(!documents.empty())Select(0);else{RefreshList();Layout();}}
    void ExternalChange() {
        if(externalChecking||closing)return;auto* d=Current();if(!d||!d->loaded||d->path.empty())return;
        externalChecking=true;
        try {
            if(GetFileAttributesW(d->path.c_str())!=INVALID_FILE_ATTRIBUTES) {
                auto disk=ReadDocument(d->path);
                if(disk.fingerprint!=d->fingerprint) {
                    if(!d->dirty){loading=true;SetRichText(d->editor,EditorLines(disk.text));SendMessageW(d->editor,EM_EMPTYUNDOBUFFER,0,0);loading=false;d->fingerprint=disk.fingerprint;d->encoding=disk.encoding;d->crlf=disk.crlf;++d->revision;++generation;notice=L"Reloaded external changes";QueuePreview();}
                    else notice=L"File changed on disk · Your edits are preserved";
                    InvalidateRect(window,nullptr,FALSE);
                }
            }
        } catch(...) {notice=L"Could not check the file on disk";loading=false;}
        externalChecking=false;
    }
    void Change(HWND editor) {
        if(loading)return;
        for(size_t i=0;i<documents.size();++i)if(documents[i]->editor==editor){
            auto& item=*documents[i];bool first=!item.dirty;item.dirty=true;++item.revision;
            if(&item==Current()){
                ++generation;notice.clear();SetTimer(window,1,300,nullptr);
                if(first)SetWindowTextW(window,(item.title+L" * — simple-md").c_str());
                RECT client{};GetClientRect(window,&client);RECT status{0,client.bottom-P(30),client.right,client.bottom};InvalidateRect(window,&status,FALSE);
            }
            if(first){RECT row{};SendMessageW(list,LB_GETITEMRECT,i,(LPARAM)&row);InvalidateRect(list,&row,FALSE);}break;
        }
    }
    void QueuePreview() {
        KillTimer(window,1);auto* d=Current();if(!d||!d->loaded||mode==1)return;
        std::string source;
        GETTEXTLENGTHEX len{GTL_NUMCHARS,1200};auto count=SendMessageW(d->editor,EM_GETTEXTLENGTHEX,(WPARAM)&len,0);
        if(count>400000)source="# Large document\n\nPreview is paused above 400,000 characters to keep memory and typing responsive. Use **Edit** to read, search, and change the complete file.";
        else source=Utf8(ControlText(d->editor));
        {std::lock_guard<std::mutex> lock(renderMutex);pendingSource=std::move(source);pendingDoc=d->id;pendingGeneration=generation;renderPending=true;}renderCondition.notify_one();
    }
    void PreviewReady(Preview* raw) {
        std::unique_ptr<Preview> result(raw);auto* d=Current();if(!d||d->id!=result->document||generation!=result->generation||mode==1)return;
        POINT scroll{};SendMessageW(preview,EM_GETSCROLLPOS,0,(LPARAM)&scroll);SendMessageW(preview,WM_SETREDRAW,FALSE,0);
        Stream stream{&result->rtf,0};EDITSTREAM edit{(DWORD_PTR)&stream,0,RtfStream};SendMessageW(preview,EM_STREAMIN,SF_RTF,(LPARAM)&edit);
        SendMessageW(preview,EM_SETSCROLLPOS,0,(LPARAM)&scroll);SendMessageW(preview,WM_SETREDRAW,TRUE,0);InvalidateRect(preview,nullptr,TRUE);
    }
    void View(int next) {mode=next;++generation;if(mode==1){SetRichText(preview,L"");std::lock_guard<std::mutex> lock(renderMutex);renderPending=false;pendingSource.clear();pendingSource.shrink_to_fit();}Layout();QueuePreview();InvalidateRect(window,nullptr,FALSE);for(HWND b:buttons)InvalidateRect(b,nullptr,FALSE);if(auto* d=Current())SetFocus(mode==0?preview:d->editor);}
    void Find(bool show) {findVisible=show;Layout();if(show){SetFocus(findInput);SendMessageW(findInput,EM_SETSEL,0,-1);}else if(auto* d=Current())SetFocus(mode==0?preview:d->editor);}
    void FindNext() {
        auto query=PlainText(findInput);if(query.empty())return;auto* d=Current();if(!d)return;HWND target=mode==0?preview:d->editor;
        CHARRANGE selection{};SendMessageW(target,EM_EXGETSEL,0,(LPARAM)&selection);FINDTEXTEXW find{{selection.cpMax,-1},query.c_str(),{}};
        LRESULT found=SendMessageW(target,EM_FINDTEXTEXW,FR_DOWN,(LPARAM)&find);
        if(found<0){find.chrg={0,-1};found=SendMessageW(target,EM_FINDTEXTEXW,FR_DOWN,(LPARAM)&find);}
        if(found>=0){SendMessageW(target,EM_EXSETSEL,0,(LPARAM)&find.chrgText);SendMessageW(target,EM_SCROLLCARET,0,0);notice.clear();}else{notice=L"No matches";MessageBeep(MB_OK);}InvalidateRect(window,nullptr,FALSE);
    }
    void WrapSelection(const wchar_t* marker) {
        auto* d=Current();if(!d)return;if(mode==0)View(1);CHARRANGE range{};SendMessageW(d->editor,EM_EXGETSEL,0,(LPARAM)&range);
        std::wstring text((size_t)(range.cpMax-range.cpMin)+1,0);SendMessageW(d->editor,EM_GETSELTEXT,0,(LPARAM)text.data());text.resize(wcslen(text.c_str()));
        std::wstring replacement=marker+text+marker;SendMessageW(d->editor,EM_REPLACESEL,TRUE,(LPARAM)replacement.c_str());SetFocus(d->editor);
    }
    void McpMode(int next) {
        mcp=next;
        if(mcp)StartMcpPipe(window);else StopMcpPipe();
        SetWindowTextW(GetDlgItem(window,ID_MCP),mcp==0?L"MCP: Off":mcp==1?L"MCP: Read":L"MCP: Edit");
        notice=mcp==0?L"MCP disconnected":mcp==1?L"MCP can read open documents":L"MCP edits stay unsaved for your review";InvalidateRect(window,nullptr,FALSE);
    }
    Json Mcp(const Json& request) {
        try {
            if(!mcp||closing)throw std::runtime_error("MCP is disabled in simple-md.");
            auto name=request.at("name").get<std::string>();auto args=request.value("arguments",Json::object());if(!args.is_object())throw std::runtime_error("arguments must be an object.");
            Json result;
            if(name=="list_documents") {
                if(!args.empty())throw std::runtime_error("list_documents accepts no arguments.");
                result=Json::array();for(auto& d:documents)result.push_back({{"id",std::to_string(d->id)},{"title",Utf8(d->title)},{"path",Utf8(d->path)},{"loaded",d->loaded},{"unsaved",d->dirty},{"revision",d->revision},{"active",d.get()==Current()}});
            } else if(name=="read_document"||name=="update_document") {
                std::string id=args.at("id").get<std::string>();Document* d=nullptr;for(auto& item:documents)if(std::to_string(item->id)==id){d=item.get();break;}if(!d)throw std::runtime_error("Document is not open. Ask the user to open it first.");
                if(name=="read_document") {
                    for(auto& prop:args.items())if(prop.key()!="id"&&prop.key()!="offset"&&prop.key()!="limit")throw std::runtime_error("Unknown argument.");
                    Load(*d);d->touched=++clock;TrimEditors();auto text=ControlText(d->editor);
                    auto integer=[&](const char* key,int fallback){if(!args.contains(key))return fallback;if(!args[key].is_number_integer())throw std::runtime_error("offset/limit must be integers.");return args[key].get<int>();};
                    int offset=integer("offset",0),limit=integer("limit",100000);if(offset<0||limit<1||limit>200000)throw std::runtime_error("offset must be >= 0; limit must be 1..200000.");
                    size_t start=text.size(),end=text.size(),points=0;
                    for(size_t i=0;i<text.size();) {if(points==(size_t)offset)start=i;if(points==(size_t)offset+(size_t)limit)end=i;++points;wchar_t c=text[i++];if(c>=0xd800&&c<=0xdbff&&i<text.size())++i;}
                    if(offset==points)start=text.size();if((size_t)offset>points)throw std::runtime_error("offset is beyond the end of the document.");
                    result={{"id",id},{"revision",d->revision},{"content",Utf8(text.substr(start,end-start))},{"offset",offset},{"totalCharacters",points},{"hasMore",(size_t)offset+(size_t)limit<points},{"unsaved",d->dirty}};
                } else {
                    for(auto& prop:args.items())if(prop.key()!="id"&&prop.key()!="revision"&&prop.key()!="content")throw std::runtime_error("Unknown argument.");
                    if(mcp!=2)throw std::runtime_error("Enable MCP Edit in the app before modifying text.");
                    Load(*d);d->touched=++clock;if(!args.at("revision").is_number_unsigned()&&!args.at("revision").is_number_integer())throw std::runtime_error("revision must be an integer.");
                    if(args.at("revision").get<uint64_t>()!=d->revision)throw std::runtime_error("Revision conflict: read the document again before updating.");
                    auto content=args.at("content").get<std::string>();if(content.size()>1000000)throw std::runtime_error("MCP edits are limited to 1 MB of UTF-8.");
                    auto w=Wide(content);if(w.find(L'\0')!=std::wstring::npos)throw std::runtime_error("NUL characters are not allowed.");
                    SendMessageW(d->editor,EM_STOPGROUPTYPING,0,0);SendMessageW(d->editor,EM_SETSEL,0,-1);auto lines=EditorLines(w);SendMessageW(d->editor,EM_REPLACESEL,TRUE,(LPARAM)lines.c_str());SendMessageW(d->editor,EM_STOPGROUPTYPING,0,0);TrimEditors();
                    result={{"id",id},{"revision",d->revision},{"unsaved",true},{"message","Changes staged in the editor. The user must review and press Ctrl+S to save."}};
                    notice=L"MCP updated "+d->title+L" · Review before saving";InvalidateRect(window,nullptr,FALSE);
                }
            } else throw std::runtime_error("Unknown tool.");
            return {{"content",Json::array({{{"type","text"},{"text",result.dump()}}})},{"isError",false}};
        }catch(const std::exception& e){return {{"content",Json::array({{{"type","text"},{"text",e.what()}}})},{"isError",true}};}
    }
    void Setup() {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);Json claude={{"mcpServers",{{"simple-md",{{"command",Utf8(executable)},{"args",Json::array({"--mcp"})}}}}}};
        std::wstring config=L"# Codex — add to %USERPROFILE%\\.codex\\config.toml\r\n[mcp_servers.simple-md]\r\ncommand = '"+std::wstring(executable)+L"'\r\nargs = [\"--mcp\"]\r\n\r\n# Claude Desktop — merge into claude_desktop_config.json\r\n"+Wide(claude.dump(2))+L"\r\n\r\nOpen simple-md and select MCP: Read or MCP: Edit. Only open documents are shared. Edits are not saved automatically.";
        if(OpenClipboard(window)){EmptyClipboard();HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(config.size()+1)*2);if(memory){void* data=GlobalLock(memory);if(data){memcpy(data,config.c_str(),(config.size()+1)*2);GlobalUnlock(memory);if(!SetClipboardData(CF_UNICODETEXT,memory))GlobalFree(memory);}else GlobalFree(memory);}CloseClipboard();}
        MessageBoxW(window,L"MCP setup snippets were copied to the clipboard. Paste the Codex or Claude section into its configuration. Your existing configurations have not been changed.\n\nThen enable MCP Read or MCP Edit in simple-md.",L"MCP setup",MB_OK|MB_ICONINFORMATION);
    }
    void Menu(bool mcpMenu) {
        HMENU menu=CreatePopupMenu();
        if(mcpMenu){AppendMenuW(menu,MF_STRING|(mcp==0?MF_CHECKED:0),ID_MCP_OFF,L"Off");AppendMenuW(menu,MF_STRING|(mcp==1?MF_CHECKED:0),ID_MCP_READ,L"Read open documents");AppendMenuW(menu,MF_STRING|(mcp==2?MF_CHECKED:0),ID_MCP_EDIT,L"Edit open documents (unsaved)");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,ID_MCP_SETUP,L"Copy connection setup");}
        else{AppendMenuW(menu,MF_STRING,ID_SAVEAS,L"Save as…\tCtrl+Shift+S");AppendMenuW(menu,MF_STRING,ID_FIND,L"Find…\tCtrl+F");AppendMenuW(menu,MF_STRING,ID_CLOSE,L"Close document\tCtrl+W");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,ID_HELP,L"Keyboard shortcuts / About");}
        RECT r{};GetWindowRect(GetDlgItem(window,mcpMenu?ID_MCP:ID_MORE),&r);int choice=TrackPopupMenu(menu,TPM_RIGHTALIGN|TPM_TOPALIGN|TPM_RETURNCMD,r.right,r.bottom+4,0,window,nullptr);DestroyMenu(menu);if(choice)Command(choice);
    }
    void Command(int id) {
        switch(id){
        case ID_NEW:New();break;case ID_OPEN:OpenDialog();break;case ID_FOLDER:FolderDialog();break;
        case ID_SAVE:if(auto* d=Current())Save(*d);break;case ID_SAVEAS:if(auto* d=Current())Save(*d,true);break;
        case ID_CLOSE:CloseDocument();break;case ID_READ:View(0);break;case ID_EDIT:View(1);break;case ID_SPLIT:View(2);break;
        case ID_MCP:Menu(true);break;case ID_MORE:Menu(false);break;
        case ID_MCP_OFF:McpMode(0);break;case ID_MCP_READ:McpMode(1);break;case ID_MCP_EDIT:McpMode(2);break;case ID_MCP_SETUP:Setup();break;
        case ID_FIND:Find(true);break;case ID_FINDCLOSE:Find(false);break;case ID_FINDNEXT:FindNext();break;
        case ID_NEXTDOC:if(!documents.empty())Select((selected+1)%(int)documents.size());break;case ID_PREVDOC:if(!documents.empty())Select((selected+(int)documents.size()-1)%(int)documents.size());break;
        case ID_BOLD:WrapSelection(L"**");break;case ID_ITALIC:WrapSelection(L"*");break;
        case ID_HELP:MessageBoxW(window,L"simple-md 1.0\nNative Markdown. Local files. No animations.\n\nCtrl+O    Open files\nCtrl+N    New document\nCtrl+S    Save\nCtrl+Shift+S    Save as\nCtrl+W    Close document\nCtrl+Tab / Ctrl+Shift+Tab    Switch document\nCtrl+1 / 2 / 3    Read / Edit / Split\nCtrl+F / F3    Find / Next match\nCtrl+B / Ctrl+I    Markdown bold / italic\n\nPreview: headings, lists, tasks, emphasis, code, quotes, text tables. Images are shown as labels; HTML is kept as source text. File limit: 16 MB. Preview pauses above 400,000 characters. MCP defaults to Off each launch.",L"About simple-md",MB_OK);break;
        }
    }
    void Margin(HWND control,int width,int height,bool reading) {if(!control)return;int inset=P(reading?36:26);if(reading&&width>P(880))inset=(width-P(808))/2;RECT r{inset,P(24),std::max(inset+P(20),width-inset),std::max(P(24),height-P(20))};SendMessageW(control,EM_SETRECT,0,(LPARAM)&r);}
    void Layout() {
        RECT r{};GetClientRect(window,&r);int w=r.right,h=r.bottom,side=P(218),top=P(64),head=P(68),status=P(30),find=findVisible?P(48):0;
        auto place=[&](int id,int x,int y,int bw,int bh){MoveWindow(GetDlgItem(window,id),P(x),P(y),P(bw),P(bh),TRUE);};
        place(ID_NEW,234,15,50,32);place(ID_OPEN,290,15,56,32);place(ID_FOLDER,352,15,62,32);
        MoveWindow(GetDlgItem(window,ID_SAVE),w-P(268),P(15),P(62),P(32),TRUE);MoveWindow(GetDlgItem(window,ID_MCP),w-P(198),P(15),P(112),P(32),TRUE);MoveWindow(GetDlgItem(window,ID_MORE),w-P(78),P(15),P(62),P(32),TRUE);
        MoveWindow(list,P(12),top+P(40),side-P(24),std::max(P(20),h-top-P(86)),TRUE);
        MoveWindow(GetDlgItem(window,ID_READ),w-P(216),top+P(17),P(60),P(30),TRUE);MoveWindow(GetDlgItem(window,ID_EDIT),w-P(151),top+P(17),P(60),P(30),TRUE);MoveWindow(GetDlgItem(window,ID_SPLIT),w-P(86),top+P(17),P(60),P(30),TRUE);
        for(HWND b:{findInput,findNext,findClose})ShowWindow(b,findVisible?SW_SHOW:SW_HIDE);
        MoveWindow(findInput,side+P(25),top+head+P(8),std::max(P(100),w-side-P(220)),P(30),TRUE);MoveWindow(findNext,w-P(178),top+head+P(8),P(70),P(30),TRUE);MoveWindow(findClose,w-P(100),top+head+P(8),P(74),P(30),TRUE);
        int y=top+head+find,bh=std::max(P(20),h-y-status),content=w-side;auto* d=Current();
        if(d&&d->editor){int ew=mode==2?content/2:content;MoveWindow(d->editor,side,y,ew,bh,TRUE);Margin(d->editor,ew,bh,false);ShowWindow(d->editor,mode==0?SW_HIDE:SW_SHOW);}
        int px=mode==2?side+content/2+P(1):side,pw=w-px;MoveWindow(preview,px,y,pw,bh,TRUE);Margin(preview,pw,bh,true);ShowWindow(preview,d&&mode!=1?SW_SHOW:SW_HIDE);
        for(int id:{ID_SAVE,ID_READ,ID_EDIT,ID_SPLIT})EnableWindow(GetDlgItem(window,id),d!=nullptr);
        InvalidateRect(window,nullptr,TRUE);
    }
    void Paint() {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);Fill(dc,r,PAPER);
        int w=r.right,h=r.bottom,side=P(218),top=P(64);Fill(dc,{0,top,side,h-P(30)},RGB(252,252,252));
        Fill(dc,{0,top-1,w,top},LINE);Fill(dc,{side-1,top,side,h-P(30)},LINE);Fill(dc,{0,h-P(30),w,h-P(30)+1},LINE);
        Label(dc,bold,L"/m",{P(20),P(16),P(50),P(46)},INK);Label(dc,bold,L"simple-md",{P(58),P(16),side-P(16),P(46)},INK);
        Label(dc,smallFont,L"DOCUMENTS",{P(22),top+P(10),side-P(15),top+P(34)},MUTED);
        Label(dc,smallFont,documents.empty()?L"No files open":std::to_wstring(documents.size())+L" files · Local only",{P(22),h-P(70),side-P(12),h-P(40)},MUTED);
        auto* d=Current();
        if(d){Label(dc,bold,d->title+(d->dirty?L"  •":L""),{side+P(26),top+P(12),w-P(238),top+P(36)},INK);std::wstring path=d->path.empty()?L"Not saved yet":d->path.substr(0,d->path.find_last_of(L"\\/"));Label(dc,smallFont,path,{side+P(26),top+P(35),w-P(238),top+P(57)},MUTED);Fill(dc,{side,top+P(67),w,top+P(68)},LINE);
            if(mode==2)Fill(dc,{side+(w-side)/2,top+P(68),side+(w-side)/2+P(1),h-P(30)},LINE);
        } else {
            int x=side+P(64),y=std::max(top+P(110),(h-P(120))/2-P(60));
            Label(dc,smallFont,L"A LITTLE SPACE FOR YOUR WORDS",{x,y,w-P(40),y+P(24)},MUTED);
            Label(dc,heading,L"Open. Read. Write.",{x,y+P(36),w-P(40),y+P(88)},INK);
            Label(dc,ui,L"A quiet Markdown editor. Everything stays on your computer.",{x,y+P(100),w-P(40),y+P(132)},MUTED);
            Label(dc,ui,L"Ctrl+O  Open files     /     Ctrl+N  New document",{x,y+P(154),w-P(40),y+P(186)},INK);
            Label(dc,smallFont,L"Drop .md files here, or open a folder to browse.",{x,y+P(196),w-P(40),y+P(226)},MUTED);
        }
        std::wstring status=notice;
        if(status.empty()&&d){status=d->dirty?L"Unsaved changes":L"Saved";status+=L"   ·   ";status+=d->encoding==Encoding::Utf8?L"UTF-8":d->encoding==Encoding::Utf8Bom?L"UTF-8 BOM":d->encoding==Encoding::Utf16LE?L"UTF-16 LE":L"UTF-16 BE";status+=d->crlf?L"   ·   CRLF":L"   ·   LF";}
        if(status.empty())status=L"Ready";
        Label(dc,smallFont,status,{P(18),h-P(29),w-P(170),h-1},MUTED);Label(dc,smallFont,L"Native. Minimal. Local.",{w-P(172),h-P(29),w-P(18),h-1},MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        EndPaint(window,&ps);
    }
    void DrawItem(DRAWITEMSTRUCT* draw) {
        HDC dc=draw->hDC;RECT r=draw->rcItem;
        if(draw->CtlID==ID_FILES){if(draw->itemID==(UINT)-1)return;bool active=(draw->itemState&ODS_SELECTED)!=0;Fill(dc,r,active?RGB(238,238,238):RGB(252,252,252));if(draw->itemID<documents.size()){auto& d=*documents[draw->itemID];RECT name=r;name.left+=P(12);name.right-=P(12);name.bottom=r.top+P(27);Label(dc,active?bold:ui,d.title+(d.dirty?L"  •":L""),name,INK);RECT sub=r;sub.left+=P(12);sub.top+=P(25);sub.bottom-=P(3);Label(dc,smallFont,d.dirty?L"Unsaved":d.loaded?L"Markdown":L"Not loaded",sub,MUTED);}return;}
        int id=(int)draw->CtlID;bool selectedView=(id==ID_READ&&mode==0)||(id==ID_EDIT&&mode==1)||(id==ID_SPLIT&&mode==2);bool pressed=(draw->itemState&ODS_SELECTED)!=0,disabled=(draw->itemState&ODS_DISABLED)!=0;bool primary=id==ID_SAVE;
        Fill(dc,r,primary?(disabled?RGB(180,180,180):pressed?RGB(60,60,60):INK):pressed||selectedView?RGB(240,240,240):PAPER);
        HBRUSH border=CreateSolidBrush(primary?INK:LINE);FrameRect(dc,&r,border);DeleteObject(border);wchar_t text[128]{};GetWindowTextW(draw->hwndItem,text,128);Label(dc,selectedView?bold:ui,text,r,disabled?RGB(170,170,170):primary?PAPER:INK,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(draw->itemState&ODS_FOCUS){InflateRect(&r,-P(3),-P(3));DrawFocusRect(dc,&r);}
    }
    bool Close() {for(auto& d:documents)if(!Confirm(*d))return false;closing=true;mcp=0;StopMcpPipe();StopWorker();MSG msg;while(PeekMessageW(&msg,window,WM_PREVIEW_READY,WM_PREVIEW_READY,PM_REMOVE))delete (Preview*)msg.lParam;return true;}
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* app=(Application*)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE){app=(Application*)((CREATESTRUCTW*)l)->lpCreateParams;app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)app);}
    if(!app)return DefWindowProcW(window,message,w,l);
    try {
        switch(message) {
        case WM_CREATE:app->Create();return 0;
        case WM_SIZE:app->Layout();return 0;
        case WM_DPICHANGED:app->dpi=HIWORD(w);app->Fonts();{auto r=(RECT*)l;SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}app->Layout();return 0;
        case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize={app->P(860),app->P(520)};return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:app->Paint();return 0;
        case WM_DRAWITEM:app->DrawItem((DRAWITEMSTRUCT*)l);return TRUE;
        case WM_CTLCOLORLISTBOX:SetBkColor((HDC)w,RGB(252,252,252));SetDCBrushColor((HDC)w,RGB(252,252,252));return (LRESULT)GetStockObject(DC_BRUSH);
        case WM_CTLCOLOREDIT:SetBkColor((HDC)w,PAPER);SetTextColor((HDC)w,INK);return (LRESULT)app->white;
        case WM_COMMAND:
            if(LOWORD(w)==ID_FILES&&HIWORD(w)==LBN_SELCHANGE)app->Select((int)SendMessageW(app->list,LB_GETCURSEL,0,0));
            else if(HIWORD(w)==EN_CHANGE&&(HWND)l!=app->findInput)app->Change((HWND)l);
            else if(HIWORD(w)==BN_CLICKED||HIWORD(w)==1)app->Command(LOWORD(w));return 0;
        case WM_TIMER:if(w==1)app->QueuePreview();return 0;
        case WM_PREVIEW_READY:app->PreviewReady((Preview*)l);return 0;
        case WM_MCP_CALL:{auto* call=(McpCall*)l;call->result=app->Mcp(call->request);return TRUE;}
        case WM_ACTIVATEAPP:if(w)app->ExternalChange();return 0;
        case WM_DROPFILES:{HDROP drop=(HDROP)w;UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);for(UINT i=0;i<count;++i){UINT n=DragQueryFileW(drop,i,nullptr,0);std::wstring path(n+1,0);DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);try{app->Open(path);}catch(const std::exception& e){app->Error(e);}}DragFinish(drop);return 0;}
        case WM_COPYDATA:{auto* copy=(COPYDATASTRUCT*)l;if(copy->dwData==1&&copy->cbData>=2&&copy->cbData<=65536&&copy->cbData%2==0){auto* text=(const wchar_t*)copy->lpData;if(text[copy->cbData/2-1]==0)app->Open(text);}return TRUE;}
        case WM_CLOSE:if(app->Close())DestroyWindow(window);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
    } catch(const std::exception& e){app->loading=false;app->Error(e);return 0;}
    return DefWindowProcW(window,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1&&std::wstring(argv[1])==L"--mcp"){LocalFree(argv);try{return RunMcp();}catch(...){return 1;}}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        std::wstring mutexName=L"Local\\simple-md-"+UserSid();HANDLE mutex=CreateMutexW(nullptr,FALSE,mutexName.c_str());
        if(GetLastError()==ERROR_ALREADY_EXISTS){HWND running=FindWindowW(WINDOW_CLASS,nullptr);if(running){for(int i=1;i<argc;++i){auto path=AbsolutePath(argv[i]);COPYDATASTRUCT copy{1,(DWORD)((path.size()+1)*2),(void*)path.c_str()};SendMessageW(running,WM_COPYDATA,0,(LPARAM)&copy);}ShowWindow(running,SW_RESTORE);SetForegroundWindow(running);}CloseHandle(mutex);LocalFree(argv);CoUninitialize();return 0;}
        HMODULE rich=LoadLibraryW(L"Msftedit.dll");if(!rich)throw std::runtime_error("Windows RichEdit is unavailable.");
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=WINDOW_CLASS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
        Application app;int scale=(int)GetDpiForSystem();HWND window=CreateWindowExW(0,WINDOW_CLASS,L"simple-md",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1180,scale,96),MulDiv(790,scale,96),nullptr,nullptr,instance,&app);
        if(!window)throw std::runtime_error("Could not create the application window.");
        COLORREF caption=PAPER;DwmSetWindowAttribute(window,35,&caption,sizeof(caption));
        for(int i=1;i<argc;++i)try{app.Open(argv[i],false);}catch(const std::exception& e){app.Error(e);}LocalFree(argv);argv=nullptr;
        if(!app.documents.empty())try{app.Select(0);}catch(const std::exception& e){app.Error(e);}
        ShowWindow(window,show);UpdateWindow(window);
        ACCEL keys[]={
            {FVIRTKEY|FCONTROL,'O',ID_OPEN},{FVIRTKEY|FCONTROL,'N',ID_NEW},{FVIRTKEY|FCONTROL,'S',ID_SAVE},{FVIRTKEY|FCONTROL|FSHIFT,'S',ID_SAVEAS},
            {FVIRTKEY|FCONTROL,'W',ID_CLOSE},{FVIRTKEY|FCONTROL,'F',ID_FIND},{FVIRTKEY,VK_F3,ID_FINDNEXT},
            {FVIRTKEY|FCONTROL,'1',ID_READ},{FVIRTKEY|FCONTROL,'2',ID_EDIT},{FVIRTKEY|FCONTROL,'3',ID_SPLIT},
            {FVIRTKEY|FCONTROL,VK_TAB,ID_NEXTDOC},{FVIRTKEY|FCONTROL|FSHIFT,VK_TAB,ID_PREVDOC},{FVIRTKEY|FCONTROL,'B',ID_BOLD},{FVIRTKEY|FCONTROL,'I',ID_ITALIC}
        };HACCEL accelerators=CreateAcceleratorTableW(keys,(int)(sizeof(keys)/sizeof(keys[0])));
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
            if(message.message==WM_KEYDOWN&&app.findVisible&&message.wParam==VK_ESCAPE){app.Find(false);continue;}
            if(message.message==WM_KEYDOWN&&message.hwnd==app.findInput&&message.wParam==VK_RETURN){app.FindNext();continue;}
            if(!TranslateAcceleratorW(window,accelerators,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        DestroyAcceleratorTable(accelerators);CloseHandle(mutex);FreeLibrary(rich);
    }catch(const std::exception& e){MessageBoxW(nullptr,Wide(e.what()).c_str(),L"simple-md",MB_OK|MB_ICONERROR);if(argv)LocalFree(argv);CoUninitialize();return 1;}
    CoUninitialize();return 0;
}
