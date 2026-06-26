/**
 * @file httpd_capture.cpp
 * @brief 图像捕获处理模块
 * 
 * 本文件提供两个 HTTP 处理器：
 * 1. capture_handler - 捕获并返回 JPEG 格式的单帧图像
 * 2. bmp_handler - 捕获并返回 BMP 格式的单帧图像
 * 
 * 如果摄像头原始格式不是 JPEG，会自动进行格式转换。
 * 所有响应都包含 CORS 跨域头和时间戳。
 */

#include "camera_httpd.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp_log.h"

static const char *TAG = "httpd_capture";

// ── 辅助函数：设置通用响应头 ─────────────────────────────────────────────────

/**
 * @brief 设置图像捕获的通用 HTTP 响应头
 * 
 * 设置以下响应头：
 * - Content-Type: 内容类型
 * - Content-Disposition: 内联显示
 * - Access-Control-Allow-Origin: CORS 跨域支持
 * - X-Timestamp: 图像捕获时间戳
 * 
 * @param req HTTP 请求句柄
 * @param content_type 内容类型（如 "image/jpeg"）
 * @param filename 文件名
 * @param fb 摄像头帧缓冲（用于获取时间戳）
 */
static void set_capture_headers(httpd_req_t *req, const char *content_type,
                                const char *filename, camera_fb_t *fb)
{
    httpd_resp_set_type(req, content_type);

    // 设置内容处置头（内联显示）
    char disposition[64];
    snprintf(disposition, sizeof(disposition), "inline; filename=%s", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    
    // 设置 CORS 跨域头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // 设置时间戳头
    if (fb) {
        char ts[32];
        snprintf(ts, sizeof(ts), "%ld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
        httpd_resp_set_hdr(req, "X-Timestamp", ts);
    }
}

// ── JPEG 图像捕获处理器 ─────────────────────────────────────────────────────

/**
 * @brief JPEG 图像捕获处理器
 * 
 * 捕获摄像头当前帧并返回 JPEG 格式图像。
 * 如果摄像头原始格式不是 JPEG，会自动调用 frame2jpg_cb 进行转换。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t capture_handler(httpd_req_t *req)
{
    // 摄像头正在重新初始化时，返回错误
    if (camera_reinit_active) {
        return httpd_resp_send_500(req);
    }

    // 获取摄像头帧缓冲
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "摄像头捕获失败");
        return httpd_resp_send_500(req);
    }

    // 设置响应头
    set_capture_headers(req, "image/jpeg", "capture.jpg", fb);

    esp_err_t res;
    if (fb->format == PIXFORMAT_JPEG) {
        // 原始格式就是 JPEG，直接发送
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        // 需要转换为 JPEG 格式
        jpg_chunking_t jchunk = {req, 0};
        res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
        httpd_resp_send_chunk(req, NULL, 0);  // 结束分块传输
    }
    esp_camera_fb_return(fb);  // 释放帧缓冲
    return res;
}

// ── BMP 图像捕获处理器 ─────────────────────────────────────────────────────

/**
 * @brief BMP 图像捕获处理器
 * 
 * 捕获摄像头当前帧并返回 BMP 格式图像。
 * 会自动调用 frame2bmp 进行格式转换。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t bmp_handler(httpd_req_t *req)
{
    // 摄像头正在重新初始化时，返回错误
    if (camera_reinit_active) {
        return httpd_resp_send_500(req);
    }

    // 获取摄像头帧缓冲
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "摄像头捕获失败");
        return httpd_resp_send_500(req);
    }

    // 设置响应头
    set_capture_headers(req, "image/x-windows-bmp", "capture.bmp", fb);

    // 转换为 BMP 格式
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    bool converted = frame2bmp(fb, &buf, &buf_len);
    esp_camera_fb_return(fb);  // 释放帧缓冲

    if (!converted) {
        ESP_LOGE(TAG, "BMP 转换失败");
        return httpd_resp_send_500(req);
    }

    // 发送 BMP 数据
    esp_err_t res = httpd_resp_send(req, (const char *)buf, buf_len);
    free(buf);  // 释放转换后的缓冲区
    return res;
}
