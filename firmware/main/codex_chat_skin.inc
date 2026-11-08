// Native LVGL skin: rectangles, pixel glyphs and text; no framebuffer/background asset.
LV_FONT_DECLARE(codex_pixel_28);
static lv_font_t CodexChatTitleFont;

static lv_obj_t* CodexChatText(lv_obj_t* parent,const char* text,int x,int y,int width,uint32_t color=0xe8f9ff){
    auto l=lv_label_create(parent);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,width);
    lv_label_set_text(l,text);lv_obj_set_style_text_color(l,lv_color_hex(color),0);
    lv_obj_set_style_text_opa(l,LV_OPA_COVER,0);lv_obj_set_style_text_line_space(l,6,0);
    lv_obj_remove_flag(l,LV_OBJ_FLAG_CLICKABLE);return l;
}

static void CodexChatFrame(lv_obj_t* o,uint32_t accent,uint32_t bg=0x061b2c){
    lv_obj_remove_style_all(o);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_radius(o,0,0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(bg),0);
    // Draw the frame ourselves: LVGL borders must not shift child coordinates.
    lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_border_color(o,lv_color_hex(accent),0);
    lv_obj_set_style_bg_color(o,lv_color_hex(bg==0xffc332?0xffdd72:0x17445b),LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x0c2434),LV_STATE_DISABLED);
    lv_obj_set_style_border_color(o,lv_color_hex(0x29495b),LV_STATE_DISABLED);
    lv_obj_set_style_opa(o,LV_OPA_COVER,LV_STATE_DISABLED);
    lv_obj_add_event_cb(o,[](lv_event_t* e){
        auto o=lv_event_get_target_obj(e);lv_area_t b;lv_obj_get_coords(o,&b);auto layer=lv_event_get_layer(e);
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_COVER;
        auto rect=[&](int x,int y,int w,int h,uint32_t c){d.bg_color=lv_color_hex(c);lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);};
        uint32_t c=lv_color_to_u32(lv_obj_get_style_border_color(o,LV_PART_MAIN))&0xffffff;
        rect(b.x1+6,b.y1,b.x2-b.x1-11,3,c);rect(b.x1+6,b.y2-2,b.x2-b.x1-11,3,c);
        rect(b.x1,b.y1+6,3,b.y2-b.y1-11,c);rect(b.x2-2,b.y1+6,3,b.y2-b.y1-11,c);
        for(int i=0;i<4;i++){
            bool r=i&1,bt=i&2;rect(r?b.x2-5:b.x1,bt?b.y2-5:b.y1,6,6,0x080d1b);
            rect(r?b.x2-9:b.x1+6,bt?b.y2-5:b.y1+3,4,3,c);
            rect(r?b.x2-5:b.x1+3,bt?b.y2-9:b.y1+6,3,4,c);
        }
    },LV_EVENT_DRAW_MAIN_END,nullptr);
}

static void CodexChatCenterButton(lv_obj_t* button,int gap=10){
    // A style width change is deferred; resolve it before using object coordinates.
    lv_obj_update_layout(button);
    auto label=lv_obj_get_child(button,0);auto icon=lv_obj_get_child(button,1);if(!label||!icon)return;
    lv_point_t text;lv_text_get_size(&text,lv_label_get_text(label),lv_obj_get_style_text_font(label,LV_PART_MAIN),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    int icon_width=lv_obj_get_width(icon),width=lv_obj_get_width(button);
    int text_width=std::min(int(text.x),std::max(1,width-icon_width-gap-16));
    int left=(width-icon_width-gap-text_width)/2;
    lv_obj_set_align(label,LV_ALIGN_LEFT_MID);lv_obj_set_x(label,left+icon_width+gap);lv_obj_set_y(label,0);lv_obj_set_width(label,text_width);
    for(unsigned i=1;i<lv_obj_get_child_count(button);i++){auto image=lv_obj_get_child(button,i);lv_obj_set_align(image,LV_ALIGN_LEFT_MID);lv_obj_set_x(image,left);lv_obj_set_y(image,0);}
}

static lv_obj_t* CodexChatIcon(lv_obj_t* parent,int x,int y,int kind,int scale,uint32_t color){
    // 12x12, each glyph is drawn in one object, without a widget per pixel.
    static const uint16_t bits[][12]={
        {0x3fc,0x402,0x801,0x801,0x949,0x949,0x801,0x801,0x7fe,0x120,0x140,0x180}, // chat
        {0x0f0,0x108,0x108,0x108,0x108,0x108,0x318,0x210,0x1e0,0x040,0x040,0x1f0}, // mic
        {0x001,0x007,0x01f,0x07e,0x1fc,0x7f8,0xff0,0x0e0,0x1c0,0x180,0x100,0x000}, // send
        {0x000,0x000,0x3fc,0x3fc,0x3fc,0x3fc,0x3fc,0x3fc,0x3fc,0x3fc,0x000,0x000}, // stop
        {0x000,0x080,0x040,0x020,0x010,0x008,0x010,0x020,0x040,0x080,0x000,0x000}, // right
        {0x0f0,0x1f8,0x3fc,0x7fe,0x7be,0x7be,0x7be,0x7fe,0x7be,0x3fc,0x1f8,0x0f0}, // alert
        {0x006,0x00f,0x01e,0x03c,0x078,0x0f0,0x1e0,0x3c0,0x780,0xf00,0xe00,0x000}, // pencil
        {0xfff,0x801,0x801,0x801,0x801,0x801,0x801,0x801,0x801,0x801,0x801,0xfff}, // unchecked
        {0xfff,0x801,0x803,0x807,0x80d,0xc19,0xe31,0xb61,0x9c1,0x881,0x801,0xfff}, // checked
        {0x000,0x000,0x204,0x108,0x090,0x060,0x060,0x090,0x108,0x204,0x000,0x000} // close
    };
    auto o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,12*scale,12*scale);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_add_event_cb(o,[](lv_event_t* e){
        auto o=lv_event_get_target_obj(e);int kind=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
        lv_area_t b;lv_obj_get_coords(o,&b);int s=lv_obj_get_width(o)/12;auto layer=lv_event_get_layer(e);
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_obj_get_style_text_color(o,LV_PART_MAIN);d.bg_opa=LV_OPA_COVER;
        for(int y=0;y<12;y++)for(int x=0;x<12;x++)if(bits[kind][y]&(1<<(11-x))){lv_area_t a={b.x1+x*s,b.y1+y*s,b.x1+(x+1)*s-1,b.y1+(y+1)*s-1};lv_draw_rect(layer,&d,&a);}
    },LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(intptr_t(kind)));
    return o;
}

void PocketApps::RenderCodexMessages(){
    const std::string& key=codex_reply_;
    if(key==codex_rendered_messages_)return;
    codex_rendered_messages_=key;
    bool first=lv_obj_get_child_count(codex_reply_panel_)==0;
    bool follow=first||lv_obj_get_scroll_bottom(codex_reply_panel_)<32;
    int scroll=lv_obj_get_scroll_y(codex_reply_panel_);
    lv_obj_clean(codex_reply_panel_);codex_reply_label_=nullptr;
    auto tag=CodexChatText(codex_reply_panel_,"助手",0,0,56,0x27e4c5);
    lv_obj_set_style_bg_color(tag,lv_color_hex(0x043c37),0);lv_obj_set_style_bg_opa(tag,LV_OPA_COVER,0);lv_obj_set_style_pad_all(tag,3,0);
    codex_reply_label_=CodexChatText(codex_reply_panel_,codex_reply_.c_str(),0,0,394);
    lv_obj_update_layout(codex_reply_panel_);
    if(codex_cached_scroll_pending_){lv_obj_scroll_to_y(codex_reply_panel_,codex_cached_scroll_,LV_ANIM_OFF);codex_cached_scroll_pending_=false;}
    else if(follow)lv_obj_scroll_to_y(codex_reply_panel_,LV_COORD_MAX,LV_ANIM_OFF);
    else lv_obj_scroll_to_y(codex_reply_panel_,scroll,LV_ANIM_OFF);
}

void PocketApps::LayoutCodexChat(bool question,bool draft){
    int layout=(question?1:0)|(draft?2:0);if(layout==codex_chat_layout_)return;int previous=codex_chat_layout_;codex_chat_layout_=layout;
    if(question)lv_obj_remove_flag(codex_question_button_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(codex_question_button_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(codex_status_,question?264:424);
    // Question presence only changes this fixed status row, never the chat or
    // compose geometry. A newly arrived question must not move the user's text.
    if(previous>=0&&((previous^layout)&2)==0)return;
    int top=150;
    int draft_top=draft?552:602;
    int actions_top=draft_top-62;
    int height=actions_top-12-top;
    lv_obj_set_pos(codex_reply_frame_,24,top);lv_obj_set_size(codex_reply_frame_,432,height);
    lv_obj_set_pos(codex_reply_panel_,14,14);lv_obj_set_size(codex_reply_panel_,404,height-28);
    lv_obj_set_pos(codex_state_panel_,24,actions_top);
    int frame_height=668-draft_top;
    lv_obj_set_pos(codex_draft_panel_,24,draft_top);lv_obj_set_size(codex_draft_panel_,432,frame_height);
    lv_obj_set_pos(codex_draft_body_,14,50);lv_obj_set_size(codex_draft_body_,404,std::max(1,frame_height-64));
}

void PocketApps::LayoutCodexActions(bool running,bool compose){
    int layout=(running?1:0)|(compose?2:0);if(layout==codex_actions_layout_)return;codex_actions_layout_=layout;
    // Guide and normal send have fixed parents and sizes, including while recording.
    if(running)lv_obj_remove_flag(codex_buttons_[2],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(codex_buttons_[2],LV_OBJ_FLAG_HIDDEN);
    if(!running&&compose)lv_obj_remove_flag(codex_send_button_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(codex_send_button_,LV_OBJ_FLAG_HIDDEN);
    if(running)lv_obj_remove_flag(codex_stop_button_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(codex_stop_button_,LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<2;i++){lv_obj_set_width(codex_buttons_[i],236);CodexChatCenterButton(codex_buttons_[i]);}
    CodexChatCenterButton(codex_buttons_[2],6);CodexChatCenterButton(codex_stop_button_,6);
    CodexChatCenterButton(codex_continue_button_,6);CodexChatCenterButton(codex_send_button_,6);
}

void PocketApps::CodexDraftTouch(lv_event_t* e){
    auto code=lv_event_get_code(e);
    if(code==LV_EVENT_PRESSED){
        auto input=lv_event_get_indev(e);codex_draft_touch_=input!=nullptr;codex_draft_moved_=false;
        if(input)lv_indev_get_point(input,&codex_draft_press_point_);
    }else if(code==LV_EVENT_PRESSING&&codex_draft_touch_){
        auto input=lv_event_get_indev(e);lv_point_t point;
        if(input){lv_indev_get_point(input,&point);if(abs(point.x-codex_draft_press_point_.x)>10||abs(point.y-codex_draft_press_point_.y)>10)codex_draft_moved_=true;}
    }else if(code==LV_EVENT_SCROLL_BEGIN||code==LV_EVENT_LONG_PRESSED){codex_draft_moved_=true;
    }else if(code==LV_EVENT_PRESS_LOST){codex_draft_touch_=false;codex_draft_moved_=true;
    }else if(code==LV_EVENT_SHORT_CLICKED){
        bool edit=codex_draft_touch_&&!codex_draft_moved_;codex_draft_touch_=false;
        if(edit)EditCodex();
    }
}
