/**
 * @file rtsp_stream.cpp
 * @brief RTSP 视频推流管理器实现
 *
 * 支持两种模式:
 * 1. 硬件 JPEG 传感器 (OV2640): 直接获取 JPEG 帧推流
 * 2. 非 JPEG 传感器 (GC2145 等): 获取 RGB565 帧, 软件 JPEG 编码后推流
 */

#include <Arduino.h>
#include <stdexcept>
#include "rtsp_stream.h"

RTSPStream::RTSPStream(CameraManager& cam, NetworkQuality& netQ,
                       WatchdogManager& wdt, LEDController& led)
    : _cam(cam), _netQ(netQ), _wdt(wdt), _led(led) {}

bool RTSPStream::begin() {
  Serial.println("\n========== RTSP 服务器启动 ==========");

  // 无认证
  _server.setCredentials("", "");
  _server.maxRTSPClients = RTSP_MAX_CLIENTS;

  // 使用 VIDEO_ONLY 传输, 避免 subtitles 流干扰
  if (_server.init(RTSPServer::VIDEO_ONLY)) {
    Serial.println("[RTSP] RTSP 服务器启动成功!");
    Serial.printf("[RTSP] 地址: rtsp://%s:554/\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("[RTSP] RTSP 服务器启动失败! 5 秒后重启...");
  _led.setState(LEDState::ERROR);
  delay(5000);
  ESP.restart();
  return false;
}

void RTSPStream::startVideoTask() {
  xTaskCreate(videoTaskEntry, "Video", VIDEO_TASK_STACK, this,
              VIDEO_TASK_PRIORITY, &_videoTaskHandle);
  Serial.println("[RTSP] 视频推流任务已创建");
}

RTSPServer& RTSPStream::getServer() {
  return _server;
}

// ============================
// 推流任务
// ============================

void RTSPStream::videoTaskEntry(void* pvParameters) {
  auto* self = static_cast<RTSPStream*>(pvParameters);
  self->videoLoop();
}

void RTSPStream::videoLoop() {
  Serial.println("[RTSP] 视频推流任务已启动");

  // 将推流任务加入看门狗监控
  _wdt.addCurrentTask();

  const bool hwJpeg = _cam.isJpegSupported();
  Serial.printf("[RTSP] 推流模式: %s\n", hwJpeg ? "硬件 JPEG" : "软件 JPEG 编码");

  // 帧率控制
  const TickType_t frameInterval = (TARGET_FPS > 0)
      ? pdMS_TO_TICKS(1000 / TARGET_FPS)
      : 0;
  TickType_t lastWakeTime = xTaskGetTickCount();

  // 统计
  uint32_t fbFailCount = 0;
  uint32_t encFailCount = 0;
  uint32_t frameCount = 0;
  uint32_t lastStatsTime = millis();
  uint32_t idleCount = 0;  // 空闲计数 (无客户端连接时)

  // 连续发送失败计数 (客户端断开竞态保护)
  uint32_t sendFailCount = 0;
  const uint32_t MAX_SEND_FAIL = 5;  // 连续失败 5 次触发服务器重置

  while (true) {
    // --- 推流 ---
    if (_server.readyToSendFrame()) {
      camera_fb_t* fb = _cam.getFrame();
      if (fb) {
        bool sendOk = false;

        // 用 try-catch 防护: 客户端断开时库内部可能访问已释放的 session 内存
        // (ESP32-RTSPServer 已知竞态: RTSP 任务删除 session 后, 视频任务仍尝试发送)
        try {
          if (hwJpeg) {
            _server.sendRTSPFrame(fb->buf, fb->len, _cam.getQuality(),
                                  fb->width, fb->height);
          } else {
            // 软件 JPEG 编码: RGB565 → JPEG
            uint8_t* jpegBuf = nullptr;
            size_t jpegLen = 0;
            if (_cam.encodeJpeg(fb, &jpegBuf, &jpegLen)) {
              if (jpegBuf && jpegLen > 0) {
                _server.sendRTSPFrame(jpegBuf, jpegLen, _cam.getQuality(),
                                      fb->width, fb->height);
                free(jpegBuf);
                encFailCount = 0;
              } else {
                Serial.println("[RTSP] 软件编码返回空数据!");
              }
            } else {
              encFailCount++;
              if (encFailCount % 10 == 1) {
                Serial.printf("[RTSP] 软件编码失败 (连续 %u 次), 堆=%u, PSRAM=%u\n",
                              encFailCount, ESP.getFreeHeap(), ESP.getFreePsram());
              }
            }
          }
          sendOk = true;
        } catch (const std::exception& e) {
          Serial.printf("[RTSP] 发送异常 (客户端可能已断开): %s\n", e.what());
        } catch (...) {
          Serial.println("[RTSP] 发送异常: 未知错误 (客户端可能已断开)");
        }

        _cam.returnFrame(fb);

        if (sendOk) {
          fbFailCount = 0;
          sendFailCount = 0;
          frameCount++;
        } else {
          sendFailCount++;
          if (sendFailCount >= MAX_SEND_FAIL) {
            Serial.printf("[RTSP] 连续 %u 次发送失败, 重置 RTSP 服务器...\n", sendFailCount);
            _server.deinit();
            delay(100);
            if (_server.init(RTSPServer::VIDEO_ONLY)) {
              Serial.println("[RTSP] 服务器重置成功, 等待新客户端连接");
            } else {
              Serial.println("[RTSP] 服务器重置失败! 10 秒后重启设备...");
              _led.setState(LEDState::ERROR);
              delay(10000);
              ESP.restart();
            }
            sendFailCount = 0;
            frameCount = 0;
            lastStatsTime = millis();
            continue;
          }
        }
      } else {
        fbFailCount++;
        if (fbFailCount % 30 == 1) {
          Serial.printf("[Camera] 获取帧失败 (连续 %u 次)\n", fbFailCount);
        }
      }
    } else {
      // 无客户端连接或未播放, 使用较长延迟减少 CPU 占用
      idleCount++;
      _wdt.feed();
      vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
      // 跳过下方的帧率控制, 已延迟
      uint32_t now2 = millis();
      if (now2 - lastStatsTime >= 10000) {
        Serial.printf("[RTSP] 心跳: 等待客户端连接... (空闲 %u 次) 堆=%u PSRAM=%u\n",
                      idleCount, ESP.getFreeHeap(), ESP.getFreePsram());
        idleCount = 0;
        lastStatsTime = now2;
      }
      _netQ.update(_server.rtpFps);
      continue;
    }

    // --- 动态码率调整 ---
    _netQ.update(_server.rtpFps);
    _cam.setQuality(_netQ.getRecommendedQuality());

    // --- 周期性统计 ---
    uint32_t now = millis();
    if (now - lastStatsTime >= 10000) {  // 每 10 秒
      if (frameCount > 0) {
        Serial.printf("[RTSP] 统计: 编码 %u 帧, FPS=%u, 质量=%d, 堆=%u\n",
                      frameCount, _server.rtpFps, _cam.getQuality(), ESP.getFreeHeap());
      }
      frameCount = 0;
      idleCount = 0;
      lastStatsTime = now;
    }

    // --- 喂看门狗 ---
    _wdt.feed();

    // --- 帧率控制 ---
    if (frameInterval > 0) {
      vTaskDelayUntil(&lastWakeTime, frameInterval);
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}
