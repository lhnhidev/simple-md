#pragma once
#include "common.h"
#include <cstdint>
enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE };
struct DiskDocument {
    std::wstring text;
    Encoding encoding=Encoding::Utf8;
    bool crlf=false;
    uint64_t fingerprint=0;
};
std::wstring NormalizeLf(const std::wstring& text);
std::wstring EditorLines(const std::wstring& text);
uint64_t Fingerprint(const std::string& bytes);
DiskDocument ReadDocument(const std::wstring& path);
std::string EncodeDocument(const std::wstring& text,Encoding encoding,bool crlf);
void AtomicWrite(const std::wstring& path,const std::string& bytes,bool replace);
std::wstring AbsolutePath(const std::wstring& path);
std::wstring FileName(const std::wstring& path);
