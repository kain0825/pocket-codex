#pragma once
#include "settings.h"
#include <lvgl.h>
inline bool PocketNeon(){Settings s("pocket_skin");return s.GetBool("neon",true);}
inline void PocketPanel(lv_obj_t* o,uint32_t accent=0x32E6CF){
 lv_obj_set_style_radius(o,0,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x101B30),0);
 lv_obj_set_style_border_width(o,2,0);lv_obj_set_style_border_color(o,lv_color_hex(accent),0);
 lv_obj_set_style_shadow_width(o,0,0);
 lv_obj_set_style_bg_color(o,lv_color_hex(0x25425A),LV_STATE_PRESSED);
}
inline void PocketPixel(lv_obj_t* parent,int x,int y,int w,int h,uint32_t color){
 auto o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
 lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
 lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
}
inline void PocketIcon(lv_obj_t* parent,int x,int y,int which,uint32_t color,int scale=4){
 static const unsigned char icons[6][8]={
 {0x7e,0x42,0x5a,0x42,0x5a,0x42,0x7e,0x00},
 {0x18,0x3c,0x3c,0x3c,0x5a,0x42,0x3c,0x18},
 {0x3e,0x62,0x42,0x5a,0x5a,0x42,0x7e,0x00},
 {0x7e,0x42,0x42,0x42,0x7e,0x18,0x3c,0x00},
 {0x18,0x7e,0x42,0x5a,0x42,0x7e,0x24,0x00},
 {0x18,0x5a,0x7e,0xe7,0xe7,0x7e,0x5a,0x18}}; // device gear
 if(which<0||which>=6||scale<1)return;
 auto icon=lv_obj_create(parent);lv_obj_remove_style_all(icon);
 lv_obj_set_pos(icon,x,y);lv_obj_set_size(icon,8*scale,8*scale);
 lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(icon,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_set_style_text_color(icon,lv_color_hex(color),0);
 // One LVGL object per icon. Merge adjacent pixels in each row into spans;
 // this preserves the original 8x8 shape without one widget per lit pixel.
 lv_obj_add_event_cb(icon,[](lv_event_t* e){
  auto object=lv_event_get_target_obj(e);lv_area_t bounds;lv_obj_get_coords(object,&bounds);
  const int kind=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  const int pixel=lv_obj_get_width(object)/8;
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
  d.bg_color=lv_obj_get_style_text_color(object,LV_PART_MAIN);d.bg_opa=LV_OPA_COVER;
  for(int row=0;row<8;row++)for(int col=0;col<8;){
   if(!(icons[kind][row]&(1<<(7-col)))){++col;continue;}
   const int start=col;while(col<8&&(icons[kind][row]&(1<<(7-col))))++col;
   lv_area_t span={bounds.x1+start*pixel,bounds.y1+row*pixel,bounds.x1+col*pixel-1,bounds.y1+(row+1)*pixel-1};
   lv_draw_rect(lv_event_get_layer(e),&d,&span);
  }
 },LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(intptr_t(which)));
}
