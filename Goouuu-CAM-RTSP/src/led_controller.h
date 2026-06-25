/**
 * @file led_controller.h
 * @brief NeoPixel LED 状态指示控制器
 */

#pragma once
#include <Adafruit_NeoPixel.h>
#include "config.h"

/**
 * @brief LED 状态枚举, 每种状态对应一种颜色
 */
enum class LEDState {
  OFF,           // 关闭
  BOOTING,       // 启动中 (蓝色)
  WIFI_CONNECTING, // WiFi 配网中 (蓝色闪烁)
  WIFI_CONNECTED,  // WiFi 已连接 (绿色)
  WIFI_DISCONNECTED, // WiFi 断线 (橙色)
  STREAMING,     // 推流中 (亮绿色)
  ERROR,         // 错误 (红色)
};

class LEDController {
public:
  void begin();
  void setState(LEDState state);
  void setColor(uint8_t r, uint8_t g, uint8_t b);
  /** @brief 周期调用, 实现闪烁效果 */
  void update();

private:
  Adafruit_NeoPixel _pixels;
  LEDState _state = LEDState::OFF;
  bool _blinkOn = false;
  uint32_t _lastBlinkTime = 0;
  static constexpr uint32_t BLINK_INTERVAL_MS = 500;
};
