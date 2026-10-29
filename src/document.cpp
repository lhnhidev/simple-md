#include "document.h"
#include <algorithm>
namespace {
struct Handle { HANDLE value=INVALID_HANDLE_VALUE; ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
}
std::wstring AbsolutePath(const std::wstring& path) {
    DWORD n=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Cannot resolve file path.");
    std::wstring full(n,0); auto used=GetFullPathNameW(path.c_str(),n,full.data(),nullptr); full.resize(used); return full;
}
std::wstring FileName(const std::wstring& path) { auto p=path.find_last_of(L"\\/"); return p==std::wstring::npos?path:path.substr(p+1); }
std::wstring NormalizeLf(const std::wstring& text) {
    std::wstring out; out.reserve(text.size());
    for(size_t i=0;i<text.size();++i) {
        if(text[i]==L'\r') { out+=L'\n'; if(i+1<text.size()&&text[i+1]==L'\n')++i; }
        else out+=text[i];
    } return out;
}
std::wstring EditorLines(const std::wstring& text) {
    auto lf=NormalizeLf(text); std::wstring out; out.reserve(lf.size()+lf.size()/30);
    for(auto c:lf) { if(c==L'\n')out+=L'\r'; out+=c; }return out;
}
uint64_t Fingerprint(const std::string& bytes) { uint64_t h=14695981039346656037ull; for(unsigned char c:bytes){h^=c;h*=1099511628211ull;}return h; }
DiskDocument ReadDocument(const std::wstring& path) {
    Handle file; file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open file. Check the path, permissions, or another application's file lock.");
    LARGE_INTEGER size{}; if(!GetFileSizeEx(file.value,&size)||size.QuadPart>MAX_FILE_BYTES)throw std::runtime_error("File exceeds the 16 MB editor limit.");
    std::string bytes((size_t)size.QuadPart,0); DWORD read=0;
    if(!bytes.empty()&&(!ReadFile(file.value,bytes.data(),(DWORD)bytes.size(),&read,nullptr)||read!=bytes.size()))throw std::runtime_error("Cannot read the complete file.");
    DiskDocument d; d.fingerprint=Fingerprint(bytes);
    if(bytes.size()>=2 && ((BYTE)bytes[0]==0xff&&(BYTE)bytes[1]==0xfe || (BYTE)bytes[0]==0xfe&&(BYTE)bytes[1]==0xff)) {
        bool le=(BYTE)bytes[0]==0xff; d.encoding=le?Encoding::Utf16LE:Encoding::Utf16BE;
        if(bytes.size()%2)throw std::runtime_error("Invalid UTF-16 file.");
        d.text.reserve((bytes.size()-2)/2);
        for(size_t i=2;i+1<bytes.size();i+=2) { unsigned a=(BYTE)bytes[i],b=(BYTE)bytes[i+1]; d.text+=(wchar_t)(le?a|(b<<8):(a<<8)|b); }
        Utf8(d.text); // Validate surrogate pairs before touching the editor.
    } else {
        size_t offset=0;
        if(bytes.size()>=3&&bytes.compare(0,3,"\xef\xbb\xbf")==0){d.encoding=Encoding::Utf8Bom;offset=3;}
        d.text=Wide(bytes.substr(offset));
    }
    if(d.text.find(L'\0')!=std::wstring::npos)throw std::runtime_error("This is a binary file or an unsupported text encoding.");
    auto lf=d.text.find(L'\n'); d.crlf=lf!=std::wstring::npos&&lf>0&&d.text[lf-1]==L'\r';
    d.text=NormalizeLf(d.text); return d;
}
std::string EncodeDocument(const std::wstring& text,Encoding encoding,bool crlf) {
    auto normalized=crlf?EditorLines(text):NormalizeLf(text);
    if(encoding==Encoding::Utf8)return Utf8(normalized);
    if(encoding==Encoding::Utf8Bom)return "\xef\xbb\xbf"+Utf8(normalized);
    Utf8(normalized);
    bool le=encoding==Encoding::Utf16LE; std::string out=le?"\xff\xfe":"\xfe\xff";
    out.reserve(2+normalized.size()*2);
    for(wchar_t c:normalized) { out+=(char)(le?c&255:c>>8); out+=(char)(le?c>>8:c&255); }return out;
}
void AtomicWrite(const std::wstring& path,const std::string& bytes,bool replace) {
    if(bytes.size()>MAX_FILE_BYTES)throw std::runtime_error("Saved content exceeds 16 MB.");
    auto directory=path.substr(0,path.find_last_of(L"\\/"));
    wchar_t temp[MAX_PATH]{};
    if(!GetTempFileNameW(directory.c_str(),L"smd",0,temp))throw std::runtime_error("Cannot create a temporary file next to this document. Check folder permissions or path length.");
    bool ok=false;
    {
        Handle file; file.value=CreateFileW(temp,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD written=0;
        if(file.value!=INVALID_HANDLE_VALUE)ok=WriteFile(file.value,bytes.data(),(DWORD)bytes.size(),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value);
    }
    if(ok) {
        if(replace)ok=ReplaceFileW(path.c_str(),temp,nullptr,REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr)!=FALSE;
        else ok=MoveFileExW(temp,path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
    }
    if(!ok) { DeleteFileW(temp); throw std::runtime_error("Save failed. The original file was not replaced. Check file permissions, disk space, or an external lock."); }
}
