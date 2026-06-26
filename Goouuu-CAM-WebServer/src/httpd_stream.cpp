/**
 * @file httpd_stream.cpp
 * @brief MJPEG 实时视频流模块
 * 
 * 本文件实现 MJPEG 流媒体处理器，提供实时视频流功能。
 * 
 * 工作原理：
 * 1. 使用 multipart/x-mixed-replace 格式持续发送 JPEG 帧
 * 2. 每帧都包含时间戳和帧间隔信息
 * 3. 通过滑动平均滤波器计算并输出实时帧率
 * 4. 如果摄像头原始格式不是 JPEG，会自动进行格式转换
 * 
 * 流媒体协议：
 * - Content-Type: multipart/x-mixed-replace;boundary=<boundary>
 * - 每帧格式：--<boundary>\r\nContent-Type: image/jpeg\r\n...\r\n<jpeg_data>
 */

#include "camera_httpd.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "esp_log.h"

static const char *TAG = "httpd_stream";

// ── MJPEG 流媒体处理器 ───────────────────────────────────────────────────────

/**
 * @brief MJPEG 实时视频流处理器
 * 
 * 在一个无限循环中持续获取摄像头帧并发送给客户端。
 * 使用 multipart/x-mixed-replace 格式实现连续视频流。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 正常结束，ESP_FAIL 发生错误
 */
esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    struct timeval timestamp;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t *jpg_buf = NULL;
    char part_buf[128];

    // 记录第一帧时间（用于计算帧间隔）
    static int64_t last_frame = 0;
    if (!last_frame) last_frame = esp_timer_get_time();

    // 设置响应类型为 MJPEG 流
    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    // 设置响应头
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    // 主循环：持续获取并发送帧
    while (true) {
        // 摄像头正在重新初始化时，暂停获取帧
        if (camera_reinit_active) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 获取摄像头帧缓冲
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "摄像头捕获失败");
            res = ESP_FAIL;
        } else {
            // 记录时间戳
            timestamp.tv_sec  = fb->timestamp.tv_sec;
            timestamp.tv_usec = fb->timestamp.tv_usec;

            // 如果原始格式不是 JPEG，需要转换
            if (fb->format != PIXFORMAT_JPEG) {
                bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
                esp_camera_fb_return(fb);  // 释放原始帧缓冲
                fb = NULL;
                if (!converted) {
                    ESP_LOGE(TAG, "JPEG 压缩失败");
                    // 确保释放可能已分配的 jpg_buf
                    if (jpg_buf) {
                        free(jpg_buf);
                        jpg_buf = NULL;
                    }
                    res = ESP_FAIL;
                }
            } else {
                // 原始格式就是 JPEG，直接使用
                jpg_buf_len = fb->len;
                jpg_buf = fb->buf;
            }
        }

        // 发送帧边界
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        
        // 发送帧头（包含内容类型、长度和时间戳）
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART,
                                   jpg_buf_len, timestamp.tv_sec, timestamp.tv_usec);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        
        // 发送 JPEG 数据
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);

        // 释放资源
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            jpg_buf = NULL;
        } else if (jpg_buf) {
            free(jpg_buf);
            jpg_buf = NULL;
        }

        // 检查发送结果
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "发送帧失败");
            break;
        }

        // 计算帧率统计
        int64_t fr_end = esp_timer_get_time();
        int frame_time = (int)((fr_end - last_frame) / 1000);  // 当前帧间隔（毫秒）
        int avg_frame_time = ra_filter_run(&ra_filter, frame_time);  // 平均帧间隔
        last_frame = fr_end;

        // 输出帧率日志（防止除零错误）
        float fps = (frame_time > 0) ? (1000.0f / frame_time) : 0.0f;
        float avg_fps = (avg_frame_time > 0) ? (1000.0f / avg_frame_time) : 0.0f;
        ESP_LOGI(TAG, "MJPG: %uB %ums (%.1ffps), 平均: %ums (%.1ffps)",
                 (uint32_t)jpg_buf_len,
                 (uint32_t)frame_time, fps,
                 (uint32_t)avg_frame_time, avg_fps);
    }

    return res;
}
