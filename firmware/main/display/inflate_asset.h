#pragma once
#include "miniz.h"
#include "esp_heap_caps.h"
static bool InflateAsset(uint8_t* output,size_t length,const uint8_t* input,size_t input_length){
    auto state=(tinfl_decompressor*)heap_caps_calloc(1,sizeof(tinfl_decompressor),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!state)return false;
    tinfl_init(state);size_t used=input_length,written=length;
    auto status=tinfl_decompress(state,input,&used,output,output,&written,TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);return status==TINFL_STATUS_DONE&&written==length;
}
