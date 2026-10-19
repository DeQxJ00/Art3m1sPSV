#pragma once
#include "game_platform.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace direct {
struct TableResolution {unsigned width=0,height=0;bool known()const{return width&&height;}};
// Read literal canvas assignments, never execute Lua or interpret game_scale.
// Conflicting assignments and expressions are ambiguous, not confirmed sizes.
inline TableResolution table_resolution(std::string_view source){
    struct Token {std::string_view text;bool newline=false;};
    std::vector<Token> tokens;bool newline=false;
    auto word=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';};
    auto long_end=[&](size_t p)->size_t{
        if(source[p]!='[')return p;
        size_t q=p+1;while(q<source.size()&&source[q]=='=')++q;
        if(q>=source.size()||source[q]!='[')return p;
        std::string close="]"+std::string(q-p-1,'=')+"]";
        auto end=source.find(close,q+1);return end==source.npos?source.size():end+close.size();
    };
    for(size_t i=0;i<source.size();){
        char c=source[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n'){newline|=c=='\r'||c=='\n';++i;continue;}
        if(c=='-'&&i+1<source.size()&&source[i+1]=='-'){
            i+=2;size_t end=i<source.size()?long_end(i):i;
            if(end!=i)i=end;else {while(i<source.size()&&source[i]!='\n'&&source[i]!='\r')++i;}
            continue;
        }
        size_t begin=i,end=long_end(i);
        if(c=='\''||c=='"'){
            ++i;while(i<source.size()){if(source[i]=='\\'){i=std::min(i+2,source.size());continue;}if(source[i++]==c)break;}
            tokens.push_back({{},newline});newline=false;continue;
        }
        if(end!=i){i=end;tokens.push_back({{},newline});newline=false;continue;}
        if(word(c)){while(i<source.size()&&word(source[i]))++i;}else ++i;
        tokens.push_back({source.substr(begin,i-begin),newline});newline=false;
    }
    TableResolution result;bool badWidth=false,badHeight=false;
    for(size_t i=0;i+1<tokens.size();++i){
        bool width=tokens[i].text=="game_width",height=tokens[i].text=="game_height";
        if((!width&&!height)||tokens[i+1].text!="=")continue;
        unsigned value=0;bool valid=i+2<tokens.size()&&!tokens[i+2].text.empty();
        if(valid)for(char c:tokens[i+2].text){if(c<'0'||c>'9'||value>8192){valid=false;break;}value=value*10+unsigned(c-'0');}
        if(i+3<tokens.size()){
            auto next=tokens[i+3];bool delimiter=next.text==","||next.text==";"||next.text=="}"||next.text=="end";
            bool statement=next.newline&&!next.text.empty()&&((next.text[0]>='a'&&next.text[0]<='z')||(next.text[0]>='A'&&next.text[0]<='Z'))&&next.text!="and"&&next.text!="or";
            valid&=delimiter||statement;
        }
        valid&=value>0&&value<=8192;
        unsigned& old=width?result.width:result.height;bool& bad=width?badWidth:badHeight;
        if(!valid||(old&&old!=value))bad=true;else old=value;
    }
    if(badWidth)result.width=0;if(badHeight)result.height=0;
    return result;
}
}
