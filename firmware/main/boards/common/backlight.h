#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include <driver/gpio.h>
#include <esp_timer.h>


class Backlight {
public:
    Backlight();
    ~Backlight();

    void RestoreBrightness();
    void SetBrightness(uint8_t brightness, bool permanent = false);
    inline uint8_t brightness() const { std::lock_guard<std::mutex> lock(transition_mutex_);return brightness_; }
    inline uint8_t target_brightness() const { std::lock_guard<std::mutex> lock(transition_mutex_);return target_brightness_; }

protected:
    void OnTransitionTimer();
    virtual void SetBrightnessImpl(uint8_t brightness) = 0;

    mutable std::mutex transition_mutex_;
    esp_timer_handle_t transition_timer_ = nullptr;
    uint8_t brightness_ = 0;
    uint8_t target_brightness_ = 0;
};


class PwmBacklight : public Backlight {
public:
    PwmBacklight(gpio_num_t pin, bool output_invert = false, uint32_t freq_hz = 25000);
    ~PwmBacklight();

    void SetBrightnessImpl(uint8_t brightness) override;
};
