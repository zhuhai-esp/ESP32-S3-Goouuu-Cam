/**
 * @file httpd_server.cpp
 * @brief HTTP 服务器启动与路由注册模块
 * 
 * 本文件负责：
 * 1. 提供首页（index.html）服务
 * 2. 提供设备重启接口
 * 3. 提供 WiFi 信息查询接口
 * 4. 初始化帧率滤波器
 * 5. 在端口 80 启动控制服务器并注册所有路由
 * 6. 在端口 81 启动流媒体服务器
 * 7. 注册 WebSocket 端点并启动定时状态推送
 */

#include "camera_httpd.h"
#include "esp_log.h"
#include <WiFi.h>

static const char *TAG = "httpd_server";

// ── 首页处理器 ───────────────────────────────────────────────────────────────

/**
 * @brief 首页 HTML 服务处理器
 * 
 * 返回嵌入在固件中的 index.html 文件。
 * HTML 文件通过 PlatformIO 的 board_build.embed_files 功能嵌入。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功
 */
esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t html_len = index_html_end - index_html_start;
    ESP_LOGI(TAG, "提供首页服务 (%u 字节)", html_len);
    return httpd_resp_send(req, (const char *)index_html_start, html_len);
}

// ── 设备重启处理器 ───────────────────────────────────────────────────────────

/**
 * @brief 设备重启处理器
 * 
 * 接收到请求后延迟 500ms 然后重启设备。
 * 用于远程重启功能。
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK（实际上不会返回，因为设备会重启）
 */
static esp_err_t restart_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "收到重启请求");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, NULL, 0);
    delay(500);  // 等待响应发送完成
    ESP.restart();
    return ESP_OK;  // 不会执行到这里
}

// ── WiFi 信息处理器 ───────────────────────────────────────────────────────────

/**
 * @brief WiFi 信息查询处理器
 * 
 * 返回当前 WiFi 连接信息的 JSON 响应：
 * - ssid: WiFi 名称
 * - ip: 设备 IP 地址
 * - rssi: 信号强度（dBm）
 * - quality: 信号质量（0-100）
 * - mac: 设备 MAC 地址
 * 
 * @param req HTTP 请求句柄
 * @return ESP_OK 成功
 */
static esp_err_t wifi_handler(httpd_req_t *req)
{
    static char json[256];
    int rssi = WiFi.RSSI();
    // 计算信号质量百分比
    int quality = (rssi <= -100) ? 0 : (rssi >= -50) ? 100 : 2 * (rssi + 100);
    snprintf(json, sizeof(json),
             "{\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"quality\":%d,\"mac\":\"%s\"}",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
             rssi, quality, WiFi.macAddress().c_str());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

// ── 服务器启动函数 ───────────────────────────────────────────────────────────

/**
 * @brief 启动摄像头 Web 服务器
 * 
 * 初始化并启动两个 HTTP 服务器：
 * 1. 控制服务器（端口 80）：提供网页、API 接口、WebSocket
 * 2. 流媒体服务器（端口 81）：提供 MJPEG 视频流
 * 
 * 注册的路由包括：
 * - / : 首页
 * - /control : 摄像头参数设置
 * - /status : 摄像头状态查询
 * - /capture : JPEG 图像捕获
 * - /bmp : BMP 图像捕获
 * - /xclk : 外部时钟频率设置
 * - /reg : 寄存器写入
 * - /greg : 寄存器读取
 * - /pll : PLL 配置
 * - /resolution : 窗口配置
 * - /restart : 设备重启
 * - /wifi : WiFi 信息查询
 * - /ws : WebSocket 通信
 * - /stream : MJPEG 视频流（端口 81）
 */
void startCameraServer()
{
    // 初始化帧率滤波器（20 个采样点）
    ra_filter_init(&ra_filter, 20);

    // ── 控制服务器配置（端口 80）────────────────────────────────────────────
    // 控制服务器只处理轻量级请求（网页、API、WebSocket），不处理摄像头帧

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;       // 最大处理器数量
    config.stack_size = 8192;           // 栈大小 8KB
    config.max_open_sockets = 7;        // 最大并发连接数
    config.backlog_conn = 8;            // 连接队列深度
    config.lru_purge_enable = true;     // 满连接时自动清理最旧的连接

    // 路由表定义（控制服务器：轻量级请求）
    static const httpd_uri_t uris[] = {
        { .uri = "/",          .method = HTTP_GET, .handler = index_handler,   .user_ctx = NULL },
        { .uri = "/control",   .method = HTTP_GET, .handler = cmd_handler,     .user_ctx = NULL },
        { .uri = "/status",    .method = HTTP_GET, .handler = status_handler,  .user_ctx = NULL },
        { .uri = "/xclk",      .method = HTTP_GET, .handler = xclk_handler,    .user_ctx = NULL },
        { .uri = "/reg",       .method = HTTP_GET, .handler = reg_handler,     .user_ctx = NULL },
        { .uri = "/greg",      .method = HTTP_GET, .handler = greg_handler,    .user_ctx = NULL },
        { .uri = "/pll",       .method = HTTP_GET, .handler = pll_handler,     .user_ctx = NULL },
        { .uri = "/resolution",.method = HTTP_GET, .handler = win_handler,     .user_ctx = NULL },
        { .uri = "/restart",   .method = HTTP_GET, .handler = restart_handler, .user_ctx = NULL },
        { .uri = "/wifi",      .method = HTTP_GET, .handler = wifi_handler,    .user_ctx = NULL },
    };

    // WebSocket 端点定义
    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true  // 标记为 WebSocket 端点
    };

    ESP_LOGI(TAG, "启动 Web 服务器，端口: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        // 注册所有 HTTP 路由
        for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
            httpd_register_uri_handler(camera_httpd, &uris[i]);
        }
        // 注册 WebSocket 处理器
        httpd_register_uri_handler(camera_httpd, &ws_uri);
        ESP_LOGI(TAG, "WebSocket 端点已注册: /ws");

        // 启动定时状态推送到 WebSocket 客户端
        ws_start_status_push(camera_httpd);
    }

    // ── 流媒体服务器配置（端口 81）──────────────────────────────────────────
    // 流媒体服务器处理所有涉及摄像头帧的请求（可能阻塞）
    // 独立服务器确保 capture/bmp/stream 不会阻塞控制面板

    config.server_port += 1;            // 端口 81
    config.ctrl_port   += 1;
    config.max_uri_handlers = 4;        // 只需少量处理器
    config.max_open_sockets = 4;        // 流媒体连接数限制
    config.backlog_conn = 4;
    config.lru_purge_enable = true;
    ESP_LOGI(TAG, "启动流媒体服务器，端口: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        static const httpd_uri_t stream_uris[] = {
            { .uri = "/stream",  .method = HTTP_GET, .handler = stream_handler,  .user_ctx = NULL },
            { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL },
        };
        for (size_t i = 0; i < sizeof(stream_uris) / sizeof(stream_uris[0]); i++) {
            httpd_register_uri_handler(stream_httpd, &stream_uris[i]);
        }
    }
}
