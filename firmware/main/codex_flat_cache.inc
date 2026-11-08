// A single opaque scrolling surface lets LVGL skip covered backgrounds and source widgets.
// Source widgets remain available to native hit testing. Pixel storage is PSRAM only.
void PocketApps::CopyCodexFlatGroup(const CodexGroupCache& cache){
    if(!codex_flat_cache_)return;
    int y=lv_obj_get_y(cache.group),h=cache.buffer->header.h;
    if(y<0||y+h>int(codex_flat_cache_->header.h))return;
    for(int row=0;row<h;++row)
        memcpy(codex_flat_cache_->data+(y+row)*codex_flat_cache_->header.stride,
               cache.buffer->data+row*cache.buffer->header.stride,cache.buffer->header.w*2);
}
void PocketApps::DisableCodexFlatCache(){
    if(codex_flat_image_)lv_obj_add_flag(codex_flat_image_,LV_OBJ_FLAG_HIDDEN);
    for(auto& cache:codex_group_caches_)lv_obj_set_style_opa_layered(cache.group,LV_OPA_COVER,0);
}
void PocketApps::BuildCodexFlatCache(){
    if(codex_projects_.empty()||codex_group_caches_.size()!=codex_projects_.size())return;
    auto last=codex_group_caches_.back().group;
    int height=std::max(int(lv_obj_get_height(codex_task_list_)),int(lv_obj_get_y(last)+lv_obj_get_height(last)));
    // Bounded extra memory; larger lists continue using the tested per-group cache.
    if(height<=0||size_t(height)*432*2>3*1024*1024)return;
    auto buffer=lv_draw_buf_create(432,height,LV_COLOR_FORMAT_RGB565,LV_STRIDE_AUTO);
    if(!buffer)return;
    if(!esp_ptr_external_ram(buffer->data)){lv_draw_buf_destroy(buffer);return;}
    codex_flat_cache_=buffer;
    uint16_t color=lv_color_to_u16(lv_color_hex(0x080d1b));
    auto pixels=reinterpret_cast<uint16_t*>(buffer->data);
    for(size_t i=0;i<buffer->data_size/2;++i)pixels[i]=color;
    for(const auto& cache:codex_group_caches_)CopyCodexFlatGroup(cache);
    auto image=lv_image_create(codex_task_list_);codex_flat_image_=image;
    lv_obj_add_flag(image,LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(image,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(image,0,0);lv_image_set_src(image,buffer);
    for(auto& cache:codex_group_caches_)lv_obj_set_style_opa_layered(cache.group,LV_OPA_TRANSP,0);
    codex_project_cache_bytes_+=buffer->data_size;
}
