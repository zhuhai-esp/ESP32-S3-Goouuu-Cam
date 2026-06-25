/**
 * @file wifi_connector.cpp
 * @brief WiFi 连接管理器实现
 *
 * 重要: WiFiManager 必须是 begin() 中的局部变量, 不能是类成员.
 * 原因: WiFiManager 在 autoConnect() 期间会注册 WiFi 事件回调和创建
 *       WebServer (端口 80). 如果 WiFiManager 对象不析构, 这些资源
 *       不会被清理, 会与 RTSP 服务器的网络栈 (lwIP/IGMP) 冲突.
 *
 * 重要: 不使用 WiFi.onEvent() 事件回调, 改用 checkConnection() 轮询.
 *       WiFiManager 析构时 WiFi.removeEvent() 可能导致 lwIP 崩溃.
 */

#include <Arduino.h>
#include <WiFiManager.h>
#include "wifi_connector.h"

WiFiConnector::WiFiConnector(LEDController& led) : _led(led) {}

bool WiFiConnector::begin() {
  Serial.println("\n========== WiFi 配网 ==========");
  _led.setState(LEDState::WIFI_CONNECTING);

  // 生成 AP 名称: Goouuu-Cam-XXXX (MAC 后 4 位)
  uint64_t mac = ESP.getEfuseMac();
  char apName[32];
  snprintf(apName, sizeof(apName), "%s-%04X", WIFI_AP_BASE,
           (uint16_t)(mac & 0xFFFF));

  // WiFiManager — 局部变量! autoConnect 返回后析构, 清理事件回调和 WebServer
  WiFiManager wm;
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);
  wm.setMinimumSignalQuality(15);

  Serial.printf("[WiFi] AP 名称: %s (开放热点, 无需密码)\n", apName);
  Serial.println("[WiFi] 正在连接 WiFi...");

  bool res = wm.autoConnect(apName);

  // wm 在此处析构 → WiFi.removeEvent(wm_event_id) 被调用 → 清理 WebServer

  if (!res) {
    Serial.println("[WiFi] 配网失败! 超时即将重启...");
    _led.setState(LEDState::ERROR);
    delay(3000);
    ESP.restart();
    return false;
  }

  _wasConnected = true;

  Serial.println("[WiFi] WiFi 连接成功!");
  Serial.printf("[WiFi] SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("[WiFi] IP 地址: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] 信号强度: %d dBm\n", WiFi.RSSI());

  _led.setState(LEDState::WIFI_CONNECTED);
  return true;
}

bool WiFiConnector::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void WiFiConnector::checkConnection() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  // 每 WIFI_MONITOR_INTERVAL 秒检查一次
  if (now - lastCheck < (unsigned long)WIFI_MONITOR_INTERVAL * 1000) return;
  lastCheck = now;

  if (isConnected()) {
    // 恢复连接
    if (_wasDisconnected) {
      Serial.println("[WiFi] 已重新连接!");
      _wasDisconnected = false;
      _led.setState(LEDState::STREAMING);
    }
    return;
  }

  // 未连接
  if (!_wasConnected) return;  // 首次配网前不检测

  if (!_wasDisconnected) {
    // 刚断线
    _wasDisconnected = true;
    _disconnectTime = now;
    Serial.println("[WiFi] 连接断开! 等待自动重连...");
    _led.setState(LEDState::WIFI_DISCONNECTED);
    return;
  }

  // 已断线一段时间
  unsigned long elapsed = (now - _disconnectTime) / 1000;
  Serial.printf("[WiFi] 断线已 %lu 秒 (超时 %d 秒将重启)\n",
                elapsed, WIFI_RECONNECT_TIMEOUT);

  if (elapsed >= WIFI_RECONNECT_TIMEOUT) {
    Serial.println("[WiFi] 断线超时, 正在重启设备...");
    _led.setState(LEDState::ERROR);
    delay(1000);
    ESP.restart();
  }
}
