/**
 * @file camera_manager.cpp
 * @brief 摄像头管理器 — 初始化、帧获取、软件 JPEG 编码
 */

#include <Arduino.h>
#include "camera_manager.h"

bool CameraManager::begin() {
  Serial.println("\n========== 摄像头初始化 ==========");

  camera_config_t config;
  memset(&config, 0, sizeof(config));

  // 引脚配置
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // 采集参数
  config.xclk_freq_hz  = CAMERA_XCLK_FREQ;
  config.jpeg_quality  = CAMERA_JPEG_QUALITY;
  config.frame_size    = CAMERA_FRAME_SIZE;
  config.fb_count      = CAMERA_FB_COUNT;
  config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

  // 像素格式: 由 config.h 中 CAMERA_PIXEL_FORMAT 决定
#if CAMERA_PIXEL_FORMAT == 1
  config.pixel_format = PIXFORMAT_YUV422;
#else
  config.pixel_format = PIXFORMAT_RGB565;
#endif

  if (psramFound()) {
    Serial.printf("[Camera] 检测到 PSRAM (%d MB)\n", ESP.getPsramSize() / 1024 / 1024);
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("[Camera] 未检测到 PSRAM, 降级为低配置");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count    = 2;
  }

  // 尝试首选格式, 失败则回退
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[Camera] 初始化失败 (0x%x), 尝试回退像素格式...\n", err);
    config.pixel_format = (config.pixel_format == PIXFORMAT_YUV422)
                          ? PIXFORMAT_RGB565 : PIXFORMAT_YUV422;
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("[Camera] 回退也失败 (0x%x), 请检查硬件连接\n", err);
      return false;
    }
  }
  _pixelFormat = config.pixel_format;

  _jpegSupported = false;
  _quality = CAMERA_JPEG_QUALITY;

  // 记录分辨率
  switch (CAMERA_FRAME_SIZE) {
    case FRAMESIZE_QVGA:  _width = 320;  _height = 240;  break;
    case FRAMESIZE_CIF:   _width = 352;  _height = 288;  break;
    case FRAMESIZE_VGA:   _width = 640;  _height = 480;  break;
    case FRAMESIZE_SVGA:  _width = 800;  _height = 600;  break;
    case FRAMESIZE_HD:    _width = 1280; _height = 720;  break;
    case FRAMESIZE_UXGA:  _width = 1600; _height = 1200; break;
    default:              _width = 320;  _height = 240;  break;
  }

  sensor_t* s = esp_camera_sensor_get();
  Serial.println("[Camera] 摄像头初始化成功!");
  if (s) Serial.printf("[Camera] 传感器 PID: 0x%04X\n", s->id.PID);
  Serial.printf("[Camera] 像素格式: %s, 分辨率: %dx%d\n",
                getPixelFormatStr(), _width, _height);
  Serial.printf("[Camera] JPEG: 软件编码, 质量: %d, 帧缓冲区: %d\n",
                config.jpeg_quality, config.fb_count);

  return true;
}

camera_fb_t* CameraManager::getFrame() {
  return esp_camera_fb_get();
}

void CameraManager::returnFrame(camera_fb_t* fb) {
  esp_camera_fb_return(fb);
}

bool CameraManager::encodeJpeg(camera_fb_t* fb, uint8_t** out, size_t* outLen) {
  // frame2jpg() 质量范围 1-100 (越高越好, 与硬件 JPEG 的 0-63 相反)
  int swQuality = map(_quality, QUALITY_MIN, QUALITY_MAX,
                      SW_JPEG_QUALITY_MAX, SW_JPEG_QUALITY_MIN);

  // GC2145 QVGA 实际输出 320x216, 需要补黑边到 320x240
  if (fb->width == 320 && fb->height != 240) {
    const size_t bpp = 2;  // RGB565 和 YUV422 都是 2 字节/像素
    const size_t targetLen = 320 * 240 * bpp;

    uint8_t* paddedBuf = (uint8_t*)malloc(targetLen);
    if (!paddedBuf) {
      return frame2jpg(fb, swQuality, out, outLen);
    }

    // RGB565 黑色 = 0x0000; YUV422 黑色 = Y:0 U:128 V:128
    memset(paddedBuf, 0, targetLen);
    if (fb->format == PIXFORMAT_YUV422) {
      memset(paddedBuf + 1, 128, targetLen / 2);
    }

    // 复制原始帧到顶部
    size_t lineBytes = fb->width * bpp;
    for (size_t y = 0; y < fb->height; y++) {
      memcpy(paddedBuf + y * lineBytes, fb->buf + y * lineBytes, lineBytes);
    }

    camera_fb_t paddedFb = {
      .buf = paddedBuf, .len = targetLen,
      .width = 320, .height = 240,
      .format = fb->format, .timestamp = fb->timestamp
    };
    bool result = frame2jpg(&paddedFb, swQuality, out, outLen);
    free(paddedBuf);
    return result;
  }

  return frame2jpg(fb, swQuality, out, outLen);
}

sensor_t* CameraManager::getSensor() {
  return esp_camera_sensor_get();
}

void CameraManager::setQuality(int quality) {
  // 钳位到有效范围
  if (quality < QUALITY_MIN) quality = QUALITY_MIN;
  if (quality > QUALITY_MAX) quality = QUALITY_MAX;

  if (quality == _quality) return;

  // 硬件 JPEG 模式: 直接设置传感器
  if (_jpegSupported) {
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
      s->set_quality(s, quality);
    }
  }
  // 软件 JPEG 模式: 仅更新参数, 编码时使用
  _quality = quality;
  // 仅在非 JPEG 模式下打印 (硬件模式每次都打印太频繁)
  if (!_jpegSupported) {
    Serial.printf("[Camera] 质量调整 → %d (软件, 映射品质=%d)\n",
                  quality, map(quality, QUALITY_MIN, QUALITY_MAX,
                               SW_JPEG_QUALITY_MAX, SW_JPEG_QUALITY_MIN));
  }
}

int CameraManager::getQuality() const {
  return _quality;
}

void CameraManager::getResolution(int& width, int& height) const {
  width = _width;
  height = _height;
}

bool CameraManager::isJpegSupported() const {
  return _jpegSupported;
}

const char* CameraManager::getPixelFormatStr() const {
  switch (_pixelFormat) {
    case PIXFORMAT_JPEG:     return "JPEG";
    case PIXFORMAT_RGB565:   return "RGB565";
    case PIXFORMAT_YUV422:   return "YUV422";
    case PIXFORMAT_GRAYSCALE:return "GRAYSCALE";
    default:                 return "UNKNOWN";
  }
}
