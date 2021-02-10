#include "ds3231.h"
#include "esphome/core/log.h"

// Datasheet:
// - https://datasheets.maximintegrated.com/en/ds/DS3231.pdf

namespace esphome {
namespace ds3231 {

static const char *TAG = "ds3231";

// DS3232 Register Addresses
static const uint32_t DS3231_RTC_ADDRESS = 0x00;
static const uint32_t DS3231_ALARM_1_ADDRESS = 0x07;
static const uint32_t DS3231_ALARM_2_ADDRESS = 0x0B;
static const uint32_t DS3231_CONTROL_ADDRESS = 0x0E;
static const uint32_t DS3231_STATUS_ADDRESS = 0x0F;

// DS3231 Alarm Type Bit Masks
static const uint32_t DS3231_ALARM_TYPE_M1 = 0x01;
static const uint32_t DS3231_ALARM_TYPE_M2 = 0x02;
static const uint32_t DS3231_ALARM_TYPE_M3 = 0x04;
static const uint32_t DS3231_ALARM_TYPE_M4 = 0x08;
static const uint32_t DS3231_ALARM_TYPE_DAY_MODE = 0x10;
static const uint32_t DS3231_ALARM_TYPE_INTERUPT = 0x40;
static const uint32_t DS3231_ALARM_TYPE_ALARM_NUMBER = 0x80;

void DS3231Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up DS3231...");
  if (!this->read_rtc_()) {
    this->mark_failed();
  }
  if(!this->read_alrm_1_()) {
    this->mark_failed();
  }
  if(!this->read_alrm_2_()) {
    this->mark_failed();
  }
  if(!this->read_ctrl_()) {
    this->mark_failed();
  }
  if(!this->read_stat_()) {
    this->mark_failed();
  }
}

void DS3231Component::update() { this->read_time(); }

void DS3231Component::dump_config() {
  ESP_LOGCONFIG(TAG, "DS3231:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Communication with DS3231 failed!");
  }
  ESP_LOGCONFIG(TAG, "  Timezone: '%s'", this->timezone_.c_str());
}

float DS3231Component::get_setup_priority() const { return setup_priority::DATA; }

void DS3231Component::read_time() {
  if (!this->read_stat_()) {
    return;
  }
  if (ds3231_.stat.reg.osc_stop) {
    ESP_LOGW(TAG, "RTC halted, not syncing to system clock.");
    return;
  }
  if (!this->read_rtc_()) {
    return;
  }
  time::ESPTime rtc_time{.second = uint8_t(ds3231_.rtc.reg.second + 10 * ds3231_.rtc.reg.second_10),
                         .minute = uint8_t(ds3231_.rtc.reg.minute + 10u * ds3231_.rtc.reg.minute_10),
                         .hour = uint8_t(ds3231_.rtc.reg.hour + 10u * ds3231_.rtc.reg.hour_10),
                         .day_of_week = uint8_t(ds3231_.rtc.reg.weekday),
                         .day_of_month = uint8_t(ds3231_.rtc.reg.day + 10u * ds3231_.rtc.reg.day_10),
                         .day_of_year = 1,  // ignored by recalc_timestamp_utc(false)
                         .month = uint8_t(ds3231_.rtc.reg.month + 10u * ds3231_.rtc.reg.month_10),
                         .year = uint16_t(ds3231_.rtc.reg.year + 10u * ds3231_.rtc.reg.year_10 + 2000)};
  rtc_time.recalc_timestamp_utc(false);
  if (!rtc_time.is_valid()) {
    ESP_LOGE(TAG, "Invalid RTC time, not syncing to system clock.");
    return;
  }
  time::RealTimeClock::synchronize_epoch_(rtc_time.timestamp);
}

void DS3231Component::write_time() {
  auto now = time::RealTimeClock::utcnow();
  if (!now.is_valid()) {
    ESP_LOGE(TAG, "Invalid system time, not syncing to RTC.");
    return;
  }
  this->read_stat_();
  if (ds3231_.stat.reg.osc_stop) {
    ds3231_.stat.reg.osc_stop = false;
    this->write_stat_();
  }
  ds3231_.rtc.reg.second = now.second % 10;
  ds3231_.rtc.reg.second_10 = now.second / 10;
  ds3231_.rtc.reg.minute = now.minute % 10;
  ds3231_.rtc.reg.minute_10 = now.minute / 10;
  ds3231_.rtc.reg.hour = now.hour % 10;
  ds3231_.rtc.reg.hour_10 = now.hour / 10;
  ds3231_.rtc.reg.weekday = now.day_of_week;
  ds3231_.rtc.reg.day = now.day_of_month % 10;
  ds3231_.rtc.reg.day_10 = now.day_of_month / 10;
  ds3231_.rtc.reg.month = now.month % 10;
  ds3231_.rtc.reg.month_10 = now.month / 10;
  ds3231_.rtc.reg.year = (now.year - 2000) % 10;
  ds3231_.rtc.reg.year_10 = (now.year - 2000) / 10 % 10;
  this->write_rtc_();
}

void DS3231Component::set_alarm(DS3231AlarmType alarm_type, uint8_t second, uint8_t minute, uint8_t hour, uint8_t day) {
  this->read_ctrl_();
  // Alarm 1
  if (!(alarm_type & DS3231_ALARM_TYPE_ALARM_NUMBER)) {
    this->read_alrm_1_();
    ds3231_.alrm_1.reg.second = second % 10;
    ds3231_.alrm_1.reg.second_10 = second / 10;
    ds3231_.alrm_1.reg.m1 = alarm_type & DS3231_ALARM_TYPE_M1;
    ds3231_.alrm_1.reg.minute = minute % 10;
    ds3231_.alrm_1.reg.minute_10 = minute / 10;
    ds3231_.alrm_1.reg.m2 = alarm_type & DS3231_ALARM_TYPE_M2;
    ds3231_.alrm_1.reg.hour = hour % 10;
    ds3231_.alrm_1.reg.hour_10 = hour / 10;
    ds3231_.alrm_1.reg.m3 = alarm_type & DS3231_ALARM_TYPE_M3;
    ds3231_.alrm_1.reg.day = day % 10;
    ds3231_.alrm_1.reg.day_10 = day / 10;
    ds3231_.alrm_1.reg.day_mode = alarm_type & DS3231_ALARM_TYPE_DAY_MODE;
    ds3231_.alrm_1.reg.m4 = alarm_type & DS3231_ALARM_TYPE_M4;
    this->write_alrm_1_();
    if (ds3231_.ctrl.reg.alrm_1_int != bool(alarm_type & DS3231_ALARM_TYPE_INTERUPT)) {
      ds3231_.ctrl.reg.alrm_1_int = bool(alarm_type & DS3231_ALARM_TYPE_INTERUPT);
      this->write_ctrl_();
    }
  }
  // Alarm 2
  else {
    this->read_alrm_2_();
    ds3231_.alrm_2.reg.minute = minute % 10;
    ds3231_.alrm_2.reg.minute_10 = minute / 10;
    ds3231_.alrm_2.reg.m2 = alarm_type & DS3231_ALARM_TYPE_M2;
    ds3231_.alrm_2.reg.hour = hour % 10;
    ds3231_.alrm_2.reg.hour_10 = hour / 10;
    ds3231_.alrm_2.reg.m3 = alarm_type & DS3231_ALARM_TYPE_M3;
    ds3231_.alrm_2.reg.day = day % 10;
    ds3231_.alrm_2.reg.day_10 = day / 10;
    ds3231_.alrm_2.reg.day_mode = alarm_type & DS3231_ALARM_TYPE_DAY_MODE;
    ds3231_.alrm_2.reg.m4 = alarm_type & DS3231_ALARM_TYPE_M4;
    this->write_alrm_2_();
    if (ds3231_.ctrl.reg.alrm_2_int != bool(alarm_type & DS3231_ALARM_TYPE_INTERUPT)) {
      ds3231_.ctrl.reg.alrm_2_int = bool(alarm_type & DS3231_ALARM_TYPE_INTERUPT);
      this->write_ctrl_();
    }
  }
}

void DS3231Component::set_sqw_mode(DS3231SquareWaveMode mode) {
  this->read_ctrl_();
  if (mode == DS3231SquareWaveMode::ALARM_INTERUPT && !ds3231_.ctrl.reg.int_ctrl) {
    ds3231_.ctrl.reg.int_ctrl = true;
    this->write_ctrl_();
  }
  else if (ds3231_.ctrl.reg.int_ctrl || ds3231_.ctrl.reg.rs != mode) {
    ds3231_.ctrl.reg.int_ctrl = false;
    ds3231_.ctrl.reg.rs = mode;
    this->write_ctrl_();
  }
}

void DS3231Component::reset_alarm(DS3231AlarmNumber alarm_number) {
  read_stat_();
  if (alarm_number == DS3231AlarmNumber::ALARM_1 && ds3231_.stat.reg.alrm_1_act) {
    ds3231_.stat.reg.alrm_1_act = false;
  } else if (alarm_number == DS3231AlarmNumber::ALARM_2 && ds3231_.stat.reg.alrm_2_act) {
    ds3231_.stat.reg.alrm_2_act = false;
  }
  write_stat_();
}

bool DS3231Component::read_rtc_() {
  if (!this->read_bytes(DS3231_RTC_ADDRESS, this->ds3231_.rtc.raw, sizeof(this->ds3231_.rtc.raw))) {
    ESP_LOGE(TAG, "Can't read I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Read  %0u%0u:%0u%0u:%0u%0u 20%0u%0u-%0u%0u-%0u%0u",
           ds3231_.rtc.reg.hour_10, ds3231_.rtc.reg.hour,
           ds3231_.rtc.reg.minute_10, ds3231_.rtc.reg.minute,
           ds3231_.rtc.reg.second_10, ds3231_.rtc.reg.second,
           ds3231_.rtc.reg.year_10, ds3231_.rtc.reg.year,
           ds3231_.rtc.reg.month_10, ds3231_.rtc.reg.month,
           ds3231_.rtc.reg.day_10, ds3231_.rtc.reg.day);
  return true;
}

bool DS3231Component::write_rtc_() {
  if (!this->write_bytes(DS3231_RTC_ADDRESS, this->ds3231_.rtc.raw, sizeof(this->ds3231_.rtc.raw))) {
    ESP_LOGE(TAG, "Can't write I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Write %0u%0u:%0u%0u:%0u%0u 20%0u%0u-%0u%0u-%0u%0u",
           ds3231_.rtc.reg.hour_10, ds3231_.rtc.reg.hour,
           ds3231_.rtc.reg.minute_10, ds3231_.rtc.reg.minute,
           ds3231_.rtc.reg.second_10, ds3231_.rtc.reg.second,
           ds3231_.rtc.reg.year_10, ds3231_.rtc.reg.year,
           ds3231_.rtc.reg.month_10, ds3231_.rtc.reg.month,
           ds3231_.rtc.reg.day_10, ds3231_.rtc.reg.day);
  return true;
}

bool DS3231Component::read_alrm_1_() {
  if (!this->read_bytes(DS3231_ALARM_1_ADDRESS, this->ds3231_.alrm_1.raw, sizeof(this->ds3231_.alrm_1.raw))) {
    ESP_LOGE(TAG, "Can't read I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Read  Alarm1 - %0u%0u:%0u%0u:%0u%0u %s:%0u%0u M1:%0u M2:%0u M3:%0u M4:%0u",
           ds3231_.alrm_1.reg.hour_10, ds3231_.alrm_1.reg.hour,
           ds3231_.alrm_1.reg.minute_10, ds3231_.alrm_1.reg.minute,
           ds3231_.alrm_1.reg.second_10, ds3231_.alrm_1.reg.second,
           ds3231_.alrm_1.reg.day_mode == 0 ? "DoM" : "DoW",
           ds3231_.alrm_1.reg.day_10, ds3231_.alrm_1.reg.day,
           ds3231_.alrm_1.reg.m1, ds3231_.alrm_1.reg.m2, ds3231_.alrm_1.reg.m3, ds3231_.alrm_1.reg.m4);
  return true;
}

bool DS3231Component::write_alrm_1_() {
  if (!this->write_bytes(DS3231_ALARM_1_ADDRESS, this->ds3231_.alrm_1.raw, sizeof(this->ds3231_.alrm_1.raw))) {
    ESP_LOGE(TAG, "Can't write I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Write Alarm1 - %0u%0u:%0u%0u:%0u%0u %s:%0u%0u M1:%0u M2:%0u M3:%0u M4:%0u",
           ds3231_.alrm_1.reg.hour_10, ds3231_.alrm_1.reg.hour,
           ds3231_.alrm_1.reg.minute_10, ds3231_.alrm_1.reg.minute,
           ds3231_.alrm_1.reg.second_10, ds3231_.alrm_1.reg.second,
           ds3231_.alrm_1.reg.day_mode == 0 ? "DoM" : "DoW",
           ds3231_.alrm_1.reg.day_10, ds3231_.alrm_1.reg.day,
           ds3231_.alrm_1.reg.m1, ds3231_.alrm_1.reg.m2, ds3231_.alrm_1.reg.m3, ds3231_.alrm_1.reg.m4);
  return true;
}

bool DS3231Component::read_alrm_2_() {
  if (!this->read_bytes(DS3231_ALARM_1_ADDRESS, this->ds3231_.alrm_2.raw, sizeof(this->ds3231_.alrm_2.raw))) {
    ESP_LOGE(TAG, "Can't read I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Read  Alarm2 - %0u%0u:%0u%0u %s:%0u%0u M2:%0u M3:%0u M4:%0u",
           ds3231_.alrm_2.reg.hour_10, ds3231_.alrm_2.reg.hour,
           ds3231_.alrm_2.reg.minute_10, ds3231_.alrm_2.reg.minute,
           ds3231_.alrm_2.reg.day_mode == 0 ? "DoM" : "DoW",
           ds3231_.alrm_2.reg.day_10, ds3231_.alrm_2.reg.day,
           ds3231_.alrm_2.reg.m2, ds3231_.alrm_2.reg.m3, ds3231_.alrm_2.reg.m4);
  return true;
}

bool DS3231Component::write_alrm_2_() {
  if (!this->write_bytes(DS3231_ALARM_1_ADDRESS, this->ds3231_.alrm_2.raw, sizeof(this->ds3231_.alrm_2.raw))) {
    ESP_LOGE(TAG, "Can't write I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Write Alarm2 - %0u%0u:%0u%0u %s:%0u%0u M2:%0u M3:%0u M4:%0u",
           ds3231_.alrm_2.reg.hour_10, ds3231_.alrm_2.reg.hour,
           ds3231_.alrm_2.reg.minute_10, ds3231_.alrm_2.reg.minute,
           ds3231_.alrm_2.reg.day_mode == 0 ? "DoM" : "DoW",
           ds3231_.alrm_2.reg.day_10, ds3231_.alrm_2.reg.day,
           ds3231_.alrm_2.reg.m2, ds3231_.alrm_2.reg.m3, ds3231_.alrm_2.reg.m4);
  return true;
}

bool DS3231Component::read_ctrl_() {
  if (!this->read_bytes(DS3231_CONTROL_ADDRESS, this->ds3231_.ctrl.raw, sizeof(this->ds3231_.ctrl.raw))) {
    ESP_LOGE(TAG, "Can't read I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Read  A1I:%s A2I:%s INT_SQW:%s RS:%0u CT:%s BSQW:%s OSC:%s",
           ONOFF(ds3231_.ctrl.reg.alrm_1_int),
           ONOFF(ds3231_.ctrl.reg.alrm_2_int),
           ds3231_.ctrl.reg.int_ctrl ? "INT" : "SQW",
           ds3231_.ctrl.reg.rs,
           ONOFF(ds3231_.ctrl.reg.conv_tmp),
           ONOFF(ds3231_.ctrl.reg.bat_sqw),
           ONOFF(!ds3231_.ctrl.reg.osc_dis));
  return true;
}

bool DS3231Component::write_ctrl_() {
  if (!this->write_bytes(DS3231_CONTROL_ADDRESS, this->ds3231_.ctrl.raw, sizeof(this->ds3231_.ctrl.raw))) {
    ESP_LOGE(TAG, "Can't write I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Write A1I:%s A2I:%s INT_SQW:%s RS:%0u CT:%s BSQW:%s OSC:%s",
           ONOFF(ds3231_.ctrl.reg.alrm_1_int),
           ONOFF(ds3231_.ctrl.reg.alrm_2_int),
           ds3231_.ctrl.reg.int_ctrl ? "INT" : "SQW",
           ds3231_.ctrl.reg.rs,
           ONOFF(ds3231_.ctrl.reg.conv_tmp),
           ONOFF(ds3231_.ctrl.reg.bat_sqw),
           ONOFF(!ds3231_.ctrl.reg.osc_dis));
  return true;
}

bool DS3231Component::read_stat_() {
  if (!this->read_bytes(DS3231_STATUS_ADDRESS, this->ds3231_.stat.raw, sizeof(this->ds3231_.stat.raw))) {
    ESP_LOGE(TAG, "Can't read I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Read  A1:%s A2:%s BSY:%s 32K:%s OSC:%s",
           ONOFF(ds3231_.stat.reg.alrm_1_act),
           ONOFF(ds3231_.stat.reg.alrm_2_act),
           YESNO(ds3231_.stat.reg.busy),
           ONOFF(ds3231_.stat.reg.en32khz),
           ONOFF(!ds3231_.stat.reg.osc_stop));
  return true;
}

bool DS3231Component::write_stat_() {
  if (!this->write_bytes(DS3231_STATUS_ADDRESS, this->ds3231_.stat.raw, sizeof(this->ds3231_.stat.raw))) {
    ESP_LOGE(TAG, "Can't write I2C data.");
    return false;
  }
  ESP_LOGD(TAG, "Write A1:%s A2:%s BSY:%s 32K:%s OSC:%s",
           ONOFF(ds3231_.stat.reg.alrm_1_act),
           ONOFF(ds3231_.stat.reg.alrm_2_act),
           YESNO(ds3231_.stat.reg.busy),
           ONOFF(ds3231_.stat.reg.en32khz),
           ONOFF(!ds3231_.stat.reg.osc_stop));
  return true;
}
}  // namespace ds3231
}  // namespace esphome
