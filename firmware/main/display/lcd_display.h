#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <memory>
#include <vector>

#define PREVIEW_IMAGE_DURATION_MS 5000
int PocketBatteryMillivolts();


class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    
    lv_draw_buf_t draw_buf_;
    bool standby_=false;
    bool interactive_refresh_=false;
    uint8_t standby_brightness_=50;
    bool idle_dimmed_=false;
    bool auto_standby_pending_=false;
    bool standby_invalidation_=false;
    uint8_t idle_brightness_=50;
    lv_timer_t* idle_timer_=nullptr;
    std::vector<lv_timer_t*> standby_timers_;
    lv_obj_t* standby_blocker_=nullptr;
    lv_obj_t* launcher_ = nullptr;
    lv_obj_t* return_home_button_ = nullptr;
    lv_obj_t* conversation_button_ = nullptr;
    lv_obj_t* conversation_button_label_ = nullptr;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles
    bool chat_ui_hidden_=false;
    std::vector<lv_obj_t*> chat_visible_objects_;
    void SetChatBackgroundVisible(bool visible);
    void CheckIdlePolicy();

    void InitializeLcdThemes();
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:
    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
    void SetStandby(bool sleeping);
    void ToggleStandby(){SetStandby(!standby_);}
    void SetInteractiveRefresh(bool enabled);
    void SetLauncherActive(bool active);
    std::string PowerDiagnostic();
    std::string LauncherDiagnostic();
    bool IsStandby()const{return standby_;}
    void ShowLauncher(bool home) override;
    void ShowLocalApp() override;
    void BenchmarkBackground(bool legacy);
    void SetChatControl(bool ready, bool active) override;
    lv_display_t* GetLvDisplay() const { return display_; }
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;
    
    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);
};

#endif // LCD_DISPLAY_H
