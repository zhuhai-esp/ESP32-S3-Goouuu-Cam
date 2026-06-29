/**
 * @file httpd_ws.cpp
 * @brief WebSocket 双向通信模块
 * 
 * 本文件实现完整的 WebSocket 功能：
 * 1. 客户端连接管理（最多 4 个并发客户端）
 * 2. 消息协议解析（支持 4 种命令）
 * 3. 单客户端文本帧发送和全部客户端广播
 * 4. 定时状态推送（每 10 秒自动推送最新状态）
 * 
 * WebSocket 消息协议：
 * - 客户端 -> 服务器：{"cmd":"set","var":"brightness","val":1}
 * - 客户端 -> 服务器：{"cmd":"get_status"}
 * - 客户端 -> 服务器：{"cmd":"get_wifi"}
 * - 客户端 -> 服务器：{"cmd":"restart"}
 * - 服务器 -> 客户端：{"type":"status","data":{...}}
 * - 服务器 -> 客户端：{"type":"wifi",...}
 * - 服务器 -> 客户端：{"type":"ack","var":"brightness","val":1,"ok":true}
 * - 服务器 -> 客户端：{"type":"update","status":{...},"wifi":{...}}
 */

#include "camera_httpd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <WiFi.h>
#include "freertos/portmacro.h"
#include <Preferences.h>

static const char *TAG = "httpd_ws";

// ── 设置保存 ─────────────────────────────────────────────────────────────────
static Preferences preferences;  // 用于保存设置到 NVS

// ── 客户端连接管理 ───────────────────────────────────────────────────────────

#define WS_MAX_CLIENTS 4  // 最大并发客户端数量

static int ws_fds[WS_MAX_CLIENTS];  // 客户端文件描述符数组
static int ws_count = 0;            // 当前连接的客户端数量
static portMUX_TYPE ws_mux = portMUX_INITIALIZER_UNLOCKED;  // 自旋锁保护并发访问

/**
 * @brief 添加 WebSocket 客户端
 * @param fd 客户端文件描述符
 */
static void ws_add_client(int fd)
{
    portENTER_CRITICAL(&ws_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_fds[i] == -1) {
            ws_fds[i] = fd;
            ws_count++;
            ESP_LOGI(TAG, "客户端已连接 (fd=%d, 总数=%d)", fd, ws_count);
            portEXIT_CRITICAL(&ws_mux);
            return;
        }
    }
    portEXIT_CRITICAL(&ws_mux);
    ESP_LOGW(TAG, "客户端表已满，拒绝连接 fd=%d", fd);
}

/**
 * @brief 移除 WebSocket 客户端
 * @param fd 客户端文件描述符
 */
static void ws_remove_client(int fd)
{
    portENTER_CRITICAL(&ws_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_fds[i] == fd) {
            ws_fds[i] = -1;
            ws_count--;
            ESP_LOGI(TAG, "客户端已断开 (fd=%d, 总数=%d)", fd, ws_count);
            portEXIT_CRITICAL(&ws_mux);
            return;
        }
    }
    portEXIT_CRITICAL(&ws_mux);
}

// ── 辅助函数：发送文本帧 ─────────────────────────────────────────────────────

/**
 * @brief 向单个客户端发送文本帧
 * @param req HTTP 请求句柄
 * @param text 文本内容
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
static esp_err_t ws_send_text(httpd_req_t *req, const char *text)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = (uint8_t *)text;
    ws_pkt.len = strlen(text);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    return httpd_ws_send_frame(req, &ws_pkt);
}

// ── 辅助函数：广播文本帧 ─────────────────────────────────────────────────────

/**
 * @brief 向所有连接的客户端广播文本帧
 * 
 * 使用异步方式发送，避免阻塞。
 * 如果发送失败，会自动移除失败的客户端。
 * 
 * @param hd HTTP 服务器句柄
 * @param text 文本内容
 */
static void ws_broadcast_text(httpd_handle_t hd, const char *text)
{
    // 构造 WebSocket 帧
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = (uint8_t *)text;
    ws_pkt.len = strlen(text);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // 复制客户端列表以避免长时间持有锁
    int fds_copy[WS_MAX_CLIENTS];
    int count_copy = 0;

    portENTER_CRITICAL(&ws_mux);
    if (ws_count == 0) {
        portEXIT_CRITICAL(&ws_mux);
        return;
    }
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (ws_fds[i] != -1) {
            fds_copy[count_copy++] = ws_fds[i];
        }
    }
    portEXIT_CRITICAL(&ws_mux);

    // 向所有客户端发送
    for (int i = 0; i < count_copy; i++) {
        esp_err_t ret = httpd_ws_send_frame_async(hd, fds_copy[i], &ws_pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "发送到 fd=%d 失败: %d，移除客户端", fds_copy[i], ret);
            // 从客户端表中移除失败的客户端
            portENTER_CRITICAL(&ws_mux);
            for (int j = 0; j < WS_MAX_CLIENTS; j++) {
                if (ws_fds[j] == fds_copy[i]) {
                    ws_fds[j] = -1;
                    ws_count--;
                    break;
                }
            }
            portEXIT_CRITICAL(&ws_mux);
        }
    }
}

// ── 辅助函数：构建 WiFi JSON ─────────────────────────────────────────────────

/**
 * @brief 构建 WiFi 信息的 JSON 字符串
 * 
 * 包含以下字段：
 * - ssid: WiFi 名称
 * - ip: 设备 IP 地址
 * - rssi: 信号强度（dBm）
 * - quality: 信号质量（0-100）
 * - mac: 设备 MAC 地址
 * 
 * @param buf 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @return JSON 字符串长度
 */
static int build_wifi_json(char *buf, size_t max_len)
{
    int rssi = WiFi.RSSI();
    // 计算信号质量百分比
    int quality = (rssi <= -100) ? 0 : (rssi >= -50) ? 100 : 2 * (rssi + 100);
    return snprintf(buf, max_len,
                    "\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"quality\":%d,\"mac\":\"%s\"",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                    rssi, quality, WiFi.macAddress().c_str());
}

// ── 命令分发器 ───────────────────────────────────────────────────────────────

/**
 * @brief 处理 WebSocket 消息
 * 
 * 解析客户端发送的 JSON 消息，并根据命令类型进行处理。
 * 支持的命令：
 * - set: 设置摄像头参数
 * - get_status: 获取摄像头状态
 * - get_wifi: 获取 WiFi 信息
 * - restart: 重启设备
 * 
 * @param req HTTP 请求句柄
 * @param msg JSON 消息字符串
 */
static void ws_handle_message(httpd_req_t *req, const char *msg)
{
    ESP_LOGI(TAG, "收到消息: %s", msg);

    // 查找命令字段
    const char *cmd_ptr = strstr(msg, "\"cmd\"");
    if (!cmd_ptr) {
        ESP_LOGW(TAG, "未找到 cmd 字段");
        return;
    }

    // ── set 命令：设置摄像头参数 ──────────────────────────────────────
    if (strstr(cmd_ptr, "\"set\"")) {
        const char *var_ptr = strstr(msg, "\"var\"");
        const char *val_ptr = strstr(msg, "\"val\"");
        if (!var_ptr || !val_ptr) {
            ESP_LOGW(TAG, "未找到 var 或 val 字段");
            return;
        }

        // 提取参数名
        char var_name[32] = {0};
        int val = 0;

        // 解析 var: 找到 "var": 后面的字符串值
        const char *colon = strchr(var_ptr, ':');
        if (colon) {
            colon++;  // 跳过冒号
            while (*colon == ' ') colon++;  // 跳过空格
            if (*colon == '"') {
                colon++;  // 跳过开头引号
                const char *end_quote = strchr(colon, '"');
                if (end_quote && (end_quote - colon) < (int)sizeof(var_name)) {
                    strncpy(var_name, colon, end_quote - colon);
                    var_name[end_quote - colon] = '\0';
                }
            }
        }

        // 解析 val: 找到 "val": 后面的数字
        const char *n_start = strchr(val_ptr, ':');
        if (n_start) {
            n_start++;  // 跳过冒号
            while (*n_start == ' ') n_start++;  // 跳过空格
            val = atoi(n_start);
        }

        if (var_name[0]) {
            ESP_LOGI(TAG, "解析结果: var=%s, val=%d", var_name, val);
            
            // 获取传感器当前值（调试用）
            sensor_t *s = esp_camera_sensor_get();
            if (s) {
                ESP_LOGI(TAG, "设置前: brightness=%d, contrast=%d, saturation=%d",
                         s->status.brightness, s->status.contrast, s->status.saturation);
            }
            
            // 应用设置
            int res = apply_sensor_setting(var_name, val);
            ESP_LOGI(TAG, "设置结果: %s = %d -> res=%d", var_name, val, res);

            // 仅在设置成功时保存到 NVS（避免失败设置被持久化）
            if (res >= 0) {
                preferences.begin("camera", false);
                preferences.putInt(var_name, val);
                preferences.end();
                ESP_LOGI(TAG, "设置已保存到 NVS: %s = %d", var_name, val);
            } else {
                ESP_LOGW(TAG, "设置失败，不保存到 NVS: %s = %d", var_name, val);
            }

            // 获取传感器设置后的值（调试用）
            if (s) {
                ESP_LOGI(TAG, "设置后: brightness=%d, contrast=%d, saturation=%d",
                         s->status.brightness, s->status.contrast, s->status.saturation);
            }

            // 发送确认消息
            char ack[96];
            snprintf(ack, sizeof(ack), "{\"type\":\"ack\",\"var\":\"%s\",\"val\":%d,\"ok\":%s}",
                     var_name, val, res >= 0 ? "true" : "false");
            ws_send_text(req, ack);
            ESP_LOGI(TAG, "已发送 ack: %s", ack);
        } else {
            ESP_LOGW(TAG, "无法解析参数名");
        }
    }
    // ── get_status 命令：获取摄像头状态 ───────────────────────────────
    else if (strstr(cmd_ptr, "\"get_status\"")) {
        char *buf = (char *)malloc(1536);
        if (!buf) return;
        int len = build_status_json(buf, 1536);
        if (len > 0) {
            // 构造状态响应
            char *ws_msg = (char *)malloc(1600);
            if (ws_msg) {
                snprintf(ws_msg, 1600, "{\"type\":\"status\",\"data\":{%.*s}}", len - 2, buf + 1);
                ws_send_text(req, ws_msg);
                free(ws_msg);
            }
        }
        free(buf);
    }
    // ── get_wifi 命令：获取 WiFi 信息 ─────────────────────────────────
    else if (strstr(cmd_ptr, "\"get_wifi\"")) {
        char *wifi_buf = (char *)malloc(256);
        if (!wifi_buf) return;
        int wlen = build_wifi_json(wifi_buf, 256);
        if (wlen > 0) {
            // 构造 WiFi 响应
            char *ws_msg = (char *)malloc(512);
            if (ws_msg) {
                snprintf(ws_msg, 512, "{\"type\":\"wifi\",%.*s}", wlen, wifi_buf);
                ws_send_text(req, ws_msg);
                free(ws_msg);
            }
        }
        free(wifi_buf);
    }
    // ── get_system 命令：获取系统信息 ──────────────────────────────────
    else if (strstr(cmd_ptr, "\"get_system\"")) {
        char *sys_buf = (char *)malloc(512);
        if (!sys_buf) return;

        // 获取系统信息
        uint32_t free_heap = ESP.getFreeHeap();
        uint32_t total_heap = ESP.getHeapSize();
        uint32_t free_psram = ESP.getFreePsram();
        uint32_t total_psram = ESP.getPsramSize();
        uint32_t sketch_used = ESP.getSketchSize();
        uint32_t sketch_free = ESP.getFreeSketchSpace();
        uint32_t uptime_sec = millis() / 1000;

        // 格式化运行时间
        int hours = uptime_sec / 3600;
        int minutes = (uptime_sec % 3600) / 60;
        int seconds = uptime_sec % 60;

        int sys_len = snprintf(sys_buf, 512,
            "{\"type\":\"system\","
            "\"freeHeap\":%u,\"totalHeap\":%u,\"heapUsage\":%d,"
            "\"freePsram\":%u,\"totalPsram\":%u,"
            "\"sketchUsed\":%u,\"sketchFree\":%u,"
            "\"uptime\":\"%02d:%02d:%02d\",\"uptimeSec\":%u}",
            free_heap, total_heap, (total_heap > 0) ? (100 - (free_heap * 100 / total_heap)) : 0,
            free_psram, total_psram,
            sketch_used, sketch_free,
            hours, minutes, seconds, uptime_sec);

        if (sys_len > 0 && sys_len < 512) {
            ws_send_text(req, sys_buf);
        }
        free(sys_buf);
    }
    // ── restart 命令：重启设备 ────────────────────────────────────────
    else if (strstr(cmd_ptr, "\"restart\"")) {
        ESP_LOGW(TAG, "收到 WebSocket 重启请求");
        ws_send_text(req, "{\"type\":\"restarting\"}");
        delay(500);  // 等待响应发送完成
        ESP.restart();
    }
}

// ── WebSocket 处理器 ─────────────────────────────────────────────────────────

/**
 * @brief WebSocket 连接处理器
 * 
 * 处理 WebSocket 握手和消息接收。
 * 在握手阶段记录客户端连接，在消息阶段解析并处理命令。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t ws_handler(httpd_req_t *req)
{
    // 握手阶段：记录新客户端
    if (req->method == HTTP_GET) {
        ws_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // 接收帧头
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "接收帧头失败: %d", ret);
        ws_remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.len == 0) return ESP_OK;

    // 读取帧内容
    char *buf = (char *)malloc(ws_pkt.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    ws_pkt.payload = (uint8_t *)buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "接收帧内容失败: %d", ret);
        free(buf);
        ws_remove_client(httpd_req_to_sockfd(req));
        return ret;
    }
    buf[ws_pkt.len] = '\0';

    ESP_LOGD(TAG, "收到消息: %s", buf);
    // 处理消息
    ws_handle_message(req, buf);

    free(buf);
    return ESP_OK;
}

// ── 状态广播 ─────────────────────────────────────────────────────────────────

/**
 * @brief 广播摄像头状态到所有客户端
 * 
 * 构建包含摄像头状态和 WiFi 信息的 JSON，并广播到所有连接的客户端。
 * 用于定时推送和主动更新。
 * 
 * @param hd HTTP 服务器句柄
 */
void ws_broadcast_status(httpd_handle_t hd)
{
    // 检查是否有客户端连接
    portENTER_CRITICAL(&ws_mux);
    int count = ws_count;
    portEXIT_CRITICAL(&ws_mux);
    if (count == 0) return;

    // 使用动态分配避免栈溢出（堆上分配）
    char *status_buf = (char *)malloc(512);
    if (!status_buf) return;

    int slen = build_status_json(status_buf, 512);
    if (slen == 0) {
        free(status_buf);
        return;
    }

    // 构建 WiFi 信息
    char *wifi_buf = (char *)malloc(256);
    if (!wifi_buf) {
        free(status_buf);
        return;
    }
    int wlen = build_wifi_json(wifi_buf, 256);

    // 组合状态和 WiFi 信息
    char *msg = (char *)malloc(1024);
    if (!msg) {
        free(status_buf);
        free(wifi_buf);
        return;
    }
    int mlen = snprintf(msg, 1024,
                        "{\"type\":\"update\",\"status\":{%.*s},\"wifi\":{%.*s}}",
                        slen - 2, status_buf + 1, wlen, wifi_buf);

    if (mlen > 0 && mlen < 1024) {
        ws_broadcast_text(hd, msg);
    }

    // 释放所有动态分配的内存
    free(status_buf);
    free(wifi_buf);
    free(msg);
}

// ── 设置加载 ─────────────────────────────────────────────────────────────────

/**
 * @brief 加载保存的摄像头设置
 * 
 * 从 NVS 中读取之前保存的设置，并应用到摄像头传感器。
 * 必须在 start_stream_server() 之后调用，因为 framesize/pixformat 
 * 变更会触发 reinit_camera() 重建流媒体服务器。
 */
void ws_load_saved_settings()
{
    preferences.begin("camera", true);  // 只读模式

    // 需要加载的设置项列表
    const char *settings[] = {
        "framesize", "pixformat",
        "brightness", "contrast", "saturation", "quality",
        "awb", "agc", "aec", "hmirror", "vflip",
        "wb_mode", "special_effect", "ae_level"
    };

    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        preferences.end();
        return;
    }

    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++) {
        if (preferences.isKey(settings[i])) {
            int val = preferences.getInt(settings[i], 0);
            apply_sensor_setting(settings[i], val);
            ESP_LOGI(TAG, "加载设置: %s = %d", settings[i], val);
        }
    }

    preferences.end();
}

// ── 定时状态推送 ─────────────────────────────────────────────────────────────

static esp_timer_handle_t status_timer = NULL;  // 定时器句柄
static httpd_handle_t timer_hd = NULL;          // 服务器句柄

/**
 * @brief 定时器回调函数
 * 
 * 每 10 秒调用一次，向所有连接的客户端广播最新状态。
 */
static void status_timer_callback(void *arg)
{
    (void)arg;
    if (timer_hd) {
        ws_broadcast_status(timer_hd);
    }
}

/**
 * @brief 启动定时状态推送
 * 
 * 创建定时器，每 10 秒向所有 WebSocket 客户端推送最新状态。
 * 同时初始化客户端连接表。
 * 
 * @param hd HTTP 服务器句柄
 */
void ws_start_status_push(httpd_handle_t hd)
{
    timer_hd = hd;

    // 初始化客户端表（所有槽位设为 -1 表示空闲）
    for (int i = 0; i < WS_MAX_CLIENTS; i++) ws_fds[i] = -1;

    // 加载保存的设置已移至 startCameraServer() 中 start_stream_server() 之后调用

    // 创建定时器
    esp_timer_create_args_t timer_args = {
        .callback = status_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws_status_push"
    };

    esp_err_t ret = esp_timer_create(&timer_args, &status_timer);
    if (ret == ESP_OK) {
        // 启动定时器（10 秒间隔，单位：微秒）
        esp_timer_start_periodic(status_timer, 10000000);
        ESP_LOGI(TAG, "状态推送定时器已启动（10秒间隔）");
    } else {
        ESP_LOGE(TAG, "定时器创建失败: %d", ret);
    }
}
