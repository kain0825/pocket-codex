#include "esp_memory_utils.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "codex_flat_cache.inc"

void PocketApps::ReleaseCodexProjectCaches(){
    if(codex_flat_cache_){lv_image_cache_drop(codex_flat_cache_);lv_draw_buf_destroy(codex_flat_cache_);}
    codex_flat_cache_=nullptr;codex_flat_image_=nullptr;
    for(auto& cache:codex_group_caches_){lv_image_cache_drop(cache.buffer);lv_draw_buf_destroy(cache.buffer);}
    codex_group_caches_.clear();codex_project_cache_bytes_=0;
}
void PocketApps::CacheCodexProjectGroups(){
    if(codex_project_draw_mode_<6)return;
    for(size_t i=0;i<codex_projects_.size();i++){
        auto group=lv_obj_get_child(codex_task_list_,i);
        lv_obj_set_style_bg_color(group,lv_color_hex(0x080d1b),0);lv_obj_set_style_bg_opa(group,LV_OPA_COVER,0);
        auto buffer=lv_snapshot_take(group,LV_COLOR_FORMAT_RGB565);
        // Never consume scarce internal SRAM for the visual cache.
        if(!buffer)continue;
        if(!esp_ptr_external_ram(buffer->data)){lv_draw_buf_destroy(buffer);continue;}
        CodexGroupCache cache={i,group,nullptr,buffer,{},{}};
        for(uint32_t j=0;j<lv_obj_get_child_count(group);j++){
            auto child=lv_obj_get_child(group,j);cache.children.push_back(child);cache.opacities.push_back(lv_obj_get_style_opa_layered(child,LV_PART_MAIN));
            // Layer opacity exits lv_obj_refr before native draw events, while preserving hit tests.
            lv_obj_set_style_opa_layered(child,LV_OPA_TRANSP,0);lv_obj_set_style_opa_layered(child,LV_OPA_TRANSP,LV_STATE_DISABLED);
        }
        auto image=lv_image_create(group);cache.image=image;lv_image_set_src(image,buffer);lv_obj_set_pos(image,0,0);
        lv_obj_remove_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(image,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(group,LV_OBJ_FLAG_USER_2);codex_project_cache_bytes_+=buffer->data_size;codex_group_caches_.push_back(std::move(cache));
    }
    if(codex_project_draw_mode_==7)BuildCodexFlatCache();
}
bool PocketApps::RefreshCodexProjectCache(size_t index){
    for(auto& cache:codex_group_caches_)if(cache.project==index){
        bool flat=codex_flat_cache_&&codex_flat_image_&&!lv_obj_has_flag(codex_flat_image_,LV_OBJ_FLAG_HIDDEN);
        if(flat)lv_obj_set_style_opa_layered(cache.group,LV_OPA_COVER,0);
        lv_obj_remove_flag(cache.group,LV_OBJ_FLAG_USER_2);lv_obj_add_flag(cache.image,LV_OBJ_FLAG_HIDDEN);
        for(size_t j=0;j<cache.children.size();j++){
            lv_obj_set_style_opa_layered(cache.children[j],cache.opacities[j],0);lv_obj_set_style_opa_layered(cache.children[j],cache.opacities[j],LV_STATE_DISABLED);
        }
        lv_obj_update_layout(cache.group);
        bool ok=lv_snapshot_take_to_draw_buf(cache.group,LV_COLOR_FORMAT_RGB565,cache.buffer)==LV_RESULT_OK;
        if(ok){
            lv_image_cache_drop(cache.buffer);lv_image_set_src(cache.image,cache.buffer);
            for(auto child:cache.children){lv_obj_set_style_opa_layered(child,LV_OPA_TRANSP,0);lv_obj_set_style_opa_layered(child,LV_OPA_TRANSP,LV_STATE_DISABLED);}
            lv_obj_add_flag(cache.group,LV_OBJ_FLAG_USER_2);lv_obj_remove_flag(cache.image,LV_OBJ_FLAG_HIDDEN);++codex_project_cache_refreshes_;
            if(flat){CopyCodexFlatGroup(cache);lv_obj_set_style_opa_layered(cache.group,LV_OPA_TRANSP,0);lv_image_cache_drop(codex_flat_cache_);lv_obj_invalidate(codex_flat_image_);}
        }
        else if(flat)DisableCodexFlatCache();
        return ok;
    }
    return false;
}
