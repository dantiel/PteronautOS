#pragma once
// RP2040 hardware PWM, not the Servo library's fixed 20 ms PIO programme.
// Set this to the actuator's specified input refresh rate before bench testing.
#ifndef YOSHI_SERVO_HZ
#define YOSHI_SERVO_HZ 333
#endif
#if defined(ARDUINO_ARCH_RP2040) && __has_include(<hardware/pwm.h>) && __has_include(<hardware/clocks.h>)
#define YOSHI_MOTION_CORE 1
#include <atomic>
#include <hardware/pwm.h>
#include <hardware/clocks.h>
static_assert(YOSHI_SERVO_HZ >= 50 && YOSHI_SERVO_HZ <= 333, "Servo refresh must be 50..333 Hz");
class MotionServo {
    std::atomic<int> pin{-1};
public:
    bool attached() const { return pin.load(std::memory_order_acquire)>=0; }
    int attach(int p,int,int) {
        pwm_config cfg=pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, clock_get_hz(clk_sys)/1000000.0f);
        pwm_config_set_wrap(&cfg, 1000000/YOSHI_SERVO_HZ-1);
        const uint slice=pwm_gpio_to_slice_num(p);
        pwm_init(slice,&cfg,true);
        pwm_set_gpio_level(p,1500);
        gpio_set_function(p,GPIO_FUNC_PWM);
        pin.store(p,std::memory_order_release);
        return p;
    }
    void detach() {
        const int p=pin.exchange(-1,std::memory_order_acq_rel);
        if(p>=0) { gpio_set_function(p,GPIO_FUNC_SIO); gpio_set_dir(p,true); gpio_put(p,false); }
    }
    void writeMicroseconds(int us) {
        const int p=pin.load(std::memory_order_acquire);
        if(p>=0) pwm_set_gpio_level(p,us<500?500:us>2500?2500:us);
    }
};
#else
#define YOSHI_MOTION_CORE 0
#endif
