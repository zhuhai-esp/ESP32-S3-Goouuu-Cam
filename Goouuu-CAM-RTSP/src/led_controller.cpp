/**
 * @file led_controller.cpp
 * @brief NeoPixel LED 状态指示控制器实现
 */

#include <Arduino.h>
#include "led_controller.h"

void LEDController::begin() {
  _pixels = Adafruit_NeoPixel(PIX_NUM, PIN_PIXS, NEO_GRB + NEO_KHZ800);
  _pixels.begin();
  _pixels.setBrightness(LED_BRIGHTNESS);
  _pixels.clear();
  _pixels.show();
  Serial.println("[LED] NeoPixel 初始化完成");
}

void LEDController::setColor(uint8_t r, uint8_t g, uint8_t b) {
  _pixels.setPixelColor(0, _pixels.Color(r, g, b));
  _pixels.show();
}

void LEDController::setState(LEDState state) {
  _state = state;
  // 立即设置颜色, update() 会处理闪烁
  switch (state) {
    case LEDState::OFF:              setColor(0, 0, 0); break;
    case LEDState::BOOTING:          setColor(0, 0, 64); break;
    case LEDState::WIFI_CONNECTING:  setColor(0, 0, 64); break;
    case LEDState::WIFI_CONNECTED:   setColor(0, 64, 0); break;
    case LEDState::WIFI_DISCONNECTED:setColor(64, 16, 0); break;
    case LEDState::STREAMING:        setColor(0, 128, 0); break;
    case LEDState::ERROR:            setColor(64, 0, 0); break;
  }
}

void LEDController::update() {
  uint32_t now = millis();
  if (now - _lastBlinkTime < BLINK_INTERVAL_MS) return;
  _lastBlinkTime = now;

  // 仅对特定状态执行闪烁
  switch (_state) {
    case LEDState::WIFI_CONNECTING:
      _blinkOn = !_blinkOn;
      setColor(0, 0, _blinkOn ? 64 : 0);
      break;
    case LEDState::WIFI_DISCONNECTED:
      _blinkOn = !_blinkOn;
      setColor(_blinkOn ? 64 : 0, _blinkOn ? 16 : 0, 0);
      break;
    case LEDState::ERROR:
      _blinkOn = !_blinkOn;
      setColor(_blinkOn ? 64 : 0, 0, 0);
      break;
    default:
      break;  // 非闪烁状态无需处理
  }
}
