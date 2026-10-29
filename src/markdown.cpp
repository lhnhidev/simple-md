#include "common.h"
#include "md4c.h"

namespace {
std::string Escape(const std::string& text) {
    auto w = Wide(text); std::string out;
    for (wchar_t c : w) {
        if (c == L'\\' || c == L'{' || c == L'}') { out += '\\'; out += (char)c; }
        else if (c == L'\n') out += "\\line ";
        else if (c == L'\r') {}
        else if (c == L'\t') out += "\\tab ";
        else if (c >= 32 && c < 127) out += (char)c;
        else out += "\\u" + std::to_string((short)c) + "?";
    }
    return out;
}
struct Renderer { std::string out; std::vector<unsigned> lists; unsigned item=0; int image=0; bool code=false; };
int EnterBlock(MD_BLOCKTYPE type, void* detail, void* data) {
    auto& r = *(Renderer*)data;
    switch(type) {
    case MD_BLOCK_H: {
        unsigned level = ((MD_BLOCK_H_DETAIL*)detail)->level;
        r.out += "{\\pard\\sb240\\sa160\\b\\fs" + std::to_string(level==1?40:level==2?32:level==3?28:24) + " "; break;
    }
    case MD_BLOCK_P: r.out += "{\\pard\\sa160\\sl340\\slmult1 "; break;
    case MD_BLOCK_QUOTE: r.out += "{\\li360\\cf2 "; break;
    case MD_BLOCK_CODE: r.out += "{\\pard\\f1\\fs20\\cf2\\cb3\\li180\\sb100\\sa160 "; r.code=true; break;
    case MD_BLOCK_UL: r.lists.push_back(0); break;
    case MD_BLOCK_OL: r.lists.push_back(((MD_BLOCK_OL_DETAIL*)detail)->start); break;
    case MD_BLOCK_LI: {
        r.out += "{\\pard\\li360\\fi-240\\sa80 ";
        auto* d=(MD_BLOCK_LI_DETAIL*)detail;
        if (d->is_task) r.out += d->task_mark=='x'||d->task_mark=='X'?"[x] ":"[ ] ";
        else if (!r.lists.empty() && r.lists.back()) r.out += std::to_string(r.lists.back()++)+". ";
        else r.out += "\\u8226? ";
        break;
    }
    case MD_BLOCK_HR: r.out += "\\pard\\cf2 ________________________________________\\par\\cf1 "; break;
    case MD_BLOCK_TABLE: r.out += "{\\pard\\f1\\fs20\\sa100 "; break;
    case MD_BLOCK_TH: r.out += "{\\b "; break;
    default: break;
    }
    return 0;
}
int LeaveBlock(MD_BLOCKTYPE type, void*, void* data) {
    auto& r=*(Renderer*)data;
    switch(type) {
    case MD_BLOCK_H: case MD_BLOCK_P: case MD_BLOCK_CODE: case MD_BLOCK_LI: r.out += "\\par}"; r.code=false; break;
    case MD_BLOCK_QUOTE: case MD_BLOCK_TABLE: r.out += "}"; break;
    case MD_BLOCK_UL: case MD_BLOCK_OL: if(!r.lists.empty())r.lists.pop_back(); break;
    case MD_BLOCK_TR: r.out += "\\par "; break;
    case MD_BLOCK_TH: r.out += "}\\tab "; break;
    case MD_BLOCK_TD: r.out += "\\tab "; break;
    default: break;
    } return 0;
}
int EnterSpan(MD_SPANTYPE type, void*, void* data) {
    auto& r=*(Renderer*)data;
    switch(type) {
    case MD_SPAN_EM: r.out += "{\\i "; break;
    case MD_SPAN_STRONG: r.out += "{\\b "; break;
    case MD_SPAN_CODE: r.out += "{\\f1\\fs20\\cb3 "; break;
    case MD_SPAN_DEL: r.out += "{\\strike "; break;
    case MD_SPAN_A: r.out += "{\\ul\\cf2 "; break;
    case MD_SPAN_IMG: r.out += "{\\i\\cf2 [image: "; ++r.image; break;
    default: r.out += "{"; break;
    } return 0;
}
int LeaveSpan(MD_SPANTYPE type, void*, void* data) {
    auto& r=*(Renderer*)data;
    if(type==MD_SPAN_IMG) { r.out += "]"; --r.image; }
    r.out += "}"; return 0;
}
int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* data) {
    auto& r=*(Renderer*)data;
    if(type==MD_TEXT_BR) r.out += "\\line ";
    else if(type==MD_TEXT_SOFTBR) r.out += " ";
    else if(type==MD_TEXT_ENTITY) {
        std::string s(text,size);
        if(s=="&amp;")r.out+="&"; else if(s=="&lt;")r.out+="<"; else if(s=="&gt;")r.out+=">";
        else if(s=="&quot;")r.out+="\""; else if(s=="&apos;")r.out+="'"; else if(s=="&nbsp;")r.out+=" ";
        else if(s.size()>3 && s[1]=='#') {
            try { auto cp=std::stoul(s.substr(s[2]=='x'||s[2]=='X'?3:2),nullptr,s[2]=='x'||s[2]=='X'?16:10);
                if(cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) r.out+=Escape(s);
                else if(cp<=0xffff) r.out+="\\u"+std::to_string((short)cp)+"?";
                else { cp-=0x10000; r.out+="\\u"+std::to_string((short)(0xd800+(cp>>10)))+"?\\u"+std::to_string((short)(0xdc00+(cp&1023)))+"?"; }
            } catch(...) { r.out+=Escape(s); }
        } else r.out+=Escape(s);
    } else r.out += Escape(std::string(text,size));
    return 0;
}
}
std::string MarkdownRtf(const std::string& source) {
    Renderer r;
    r.out = "{\\rtf1\\ansi\\deff0\\uc1{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}{\\colortbl;\\red23\\green23\\blue23;\\red100\\green100\\blue100;\\red245\\green245\\blue245;}\\viewkind4\\f0\\fs23\\cf1 ";
    MD_PARSER p{}; p.flags=MD_DIALECT_GITHUB|MD_FLAG_NOHTML;
    p.enter_block=EnterBlock; p.leave_block=LeaveBlock; p.enter_span=EnterSpan; p.leave_span=LeaveSpan; p.text=Text;
    if(md_parse(source.data(),(MD_SIZE)source.size(),&p,&r)!=0) throw std::runtime_error("Markdown parsing failed.");
    r.out += "}";
    return r.out;
}
