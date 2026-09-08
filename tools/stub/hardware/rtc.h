#pragma once
typedef struct {
  int year, month, day, dotw, hour, min, sec;
} datetime_t;
void rtc_init(void);
void rtc_get_datetime(datetime_t *t);
