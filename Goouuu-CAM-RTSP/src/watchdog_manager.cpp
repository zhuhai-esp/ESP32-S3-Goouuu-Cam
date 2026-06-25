/**
 * @file watchdog_manager.cpp
 * @brief 硬件看门狗管理器实现
 *
 * 注意: Arduino-ESP32 2.x (ESP-IDF 4.4) 在 app_main() 中已默认初始化
 *       Task Watchdog (5 秒超时, 监控两个核心的空闲任务).
 *       再次调用 esp_task_wdt_init() 会返回 ESP_ERR_INVALID_STATE.
 *       因此我们不重新初始化, 而是直接将推流任务加入现有看门狗.
 */

#include <Arduino.h>
#include "watchdog_manager.h"
#include "esp_idf_version.h"
#include "esp_log.h"

static const char* WDT_TAG = "WDT";

void WatchdogManager::begin() {
#if ESP_IDF_VERSION_MAJOR >= 5
  // ESP-IDF 5.x (Arduino-ESP32 3.x): 可以安全地重新配置
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = 0,        // 不监控空闲任务
    .trigger_panic = true,      // 超时触发 panic → 重启
  };
  esp_task_wdt_deinit();
  esp_task_wdt_init(&wdt_config);
  Serial.printf("[WDT] 看门狗已重新配置, 超时: %d 秒\n", WDT_TIMEOUT_SEC);
#else
  // ESP-IDF 4.x: 看门狗已被系统初始化 (5 秒超时)
  // 尝试重新初始化, 失败则使用系统默认值
  esp_err_t err = esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  if (err == ESP_OK) {
    Serial.printf("[WDT] 看门狗已重新配置, 超时: %d 秒\n", WDT_TIMEOUT_SEC);
  } else {
    Serial.println("[WDT] 使用系统默认看门狗 (5 秒超时)");
  }
#endif
}

void WatchdogManager::addCurrentTask() {
  esp_err_t err = esp_task_wdt_add(NULL);
  if (err == ESP_OK) {
    Serial.println("[WDT] 当前任务已加入看门狗监控");
  } else if (err == ESP_ERR_INVALID_STATE) {
    // 任务可能已经加入了
    Serial.println("[WDT] 当前任务已在看门狗监控中");
  } else {
    ESP_LOGW(WDT_TAG, "加入看门狗失败: %s", esp_err_to_name(err));
  }
}

void WatchdogManager::feed() {
  esp_task_wdt_reset();
}
