#pragma once
#include <string>
#include <cstdint>
namespace PocketInput {
constexpr unsigned MeterFloor=85;
constexpr uint32_t MinimumSignalSamples=1600; // 100 ms at the existing 16 kHz capture rate.
inline bool Audible(uint32_t samples){return samples>=MinimumSignalSamples;}
inline uint32_t Characters(const char* text){uint32_t n=0;for(auto p=reinterpret_cast<const unsigned char*>(text);*p;++p)if((*p&0xc0)!=0x80)++n;return n;}
inline bool HasText(const std::string& text){
    for(size_t i=0;i<text.size();){
        unsigned char c=text[i];if(c<=0x20){++i;continue;}
        if(text.compare(i,2,"\xc2\xa0")==0){i+=2;continue;}
        if(text.compare(i,3,"\xe3\x80\x80")==0||text.compare(i,3,"\xef\xbb\xbf")==0||text.compare(i,3,"\xe2\x80\x8b")==0){i+=3;continue;}
        return true;
    }return false;
}
}
