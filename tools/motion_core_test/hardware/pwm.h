#pragma once
#include <stdint.h>
using uint=unsigned;
constexpr int GPIO_FUNC_PWM=4, GPIO_FUNC_SIO=5;
inline unsigned pulse[32]={}, top[16]={};
struct pwm_config { unsigned wrap=0; };
inline pwm_config pwm_get_default_config() { return {}; }
inline void pwm_config_set_clkdiv(pwm_config*,float) {}
inline void pwm_config_set_wrap(pwm_config* c,unsigned n) { c->wrap=n; }
inline unsigned pwm_gpio_to_slice_num(unsigned p) { return p/2; }
inline void pwm_init(unsigned s,const pwm_config* c,bool) { top[s]=c->wrap; }
inline void pwm_set_gpio_level(unsigned p,unsigned v) { pulse[p]=v; }
inline void gpio_set_function(unsigned,int) {}
inline void gpio_set_dir(unsigned,bool) {}
inline void gpio_put(unsigned,bool) {}
