/**
 * @file network_quality.cpp
 * @brief 网络质量监控与动态码率调整实现
 */

#include <Arduino.h>
#include "network_quality.h"

void NetworkQuality::begin() {
  _rssi = WiFi.RSSI();
  _recommendedQuality = CAMERA_JPEG_QUALITY;
  _lastUpdate = millis();
  Serial.println("[NetQuality] 网络质量监控已启动");
}

void NetworkQuality::update(uint32_t currentFps) {
  uint32_t now = millis();
  if (now - _lastUpdate < ADAPTIVE_QUALITY_INTERVAL_MS) return;
  _lastUpdate = now;

  _rssi = WiFi.RSSI();
  int newQuality = computeQuality(_rssi, currentFps);

  // 仅在推荐质量变化时输出日志
  if (newQuality != _recommendedQuality) {
    Serial.printf("[NetQuality] RSSI=%d dBm, FPS=%u, 质量 %d→%d\n",
                  _rssi, currentFps, _recommendedQuality, newQuality);
  }
  _recommendedQuality = newQuality;
}

int NetworkQuality::computeQuality(int rssi, uint32_t fps) const {
  int target;

  // 第一级: 根据 RSSI 决定基准质量
  if (rssi >= RSSI_EXCELLENT) {
    target = QUALITY_MIN;           // 信号极好, 最高质量
  } else if (rssi >= RSSI_GOOD) {
    target = CAMERA_JPEG_QUALITY;   // 信号良好, 默认质量
  } else if (rssi >= RSSI_FAIR) {
    target = 15;                    // 信号一般, 适度降质
  } else {
    target = QUALITY_MAX;           // 信号差, 最低质量
  }

  // 第二级: 如果 FPS 过低 (网络拥塞), 进一步降质
  if (fps > 0 && fps < FPS_LOW_THRESHOLD) {
    target += QUALITY_STEP;
    if (target > QUALITY_MAX) target = QUALITY_MAX;
  }

  return target;
}

int NetworkQuality::getRecommendedQuality() const {
  return _recommendedQuality;
}

int NetworkQuality::getRSSI() const {
  return _rssi;
}

bool NetworkQuality::isPoor() const {
  return _rssi < RSSI_FAIR;
}
