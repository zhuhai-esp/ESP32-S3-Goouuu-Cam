/**
 * @file httpd_common.cpp
 * @brief HTTP 服务器公共工具模块
 * 
 * 本文件提供所有 HTTP 处理器共享的底层功能：
 * 1. 全局 HTTP 服务器句柄管理
 * 2. 滑动平均滤波器（用于计算平均帧率）
 * 3. GET 请求查询字符串解析
 * 4. JPEG 分块编码回调函数
 */

#include "camera_httpd.h"
#include "esp_log.h"

static const char *TAG = "httpd_common";

// ── 全局服务器句柄 ───────────────────────────────────────────────────────────
// 控制服务器（端口80）和流媒体服务器（端口81）

httpd_handle_t stream_httpd = NULL;   // MJPEG 流媒体服务器句柄
httpd_handle_t camera_httpd = NULL;   // 摄像头控制服务器句柄

// ── 滑动平均滤波器（用于帧率计算）────────────────────────────────────────────

ra_filter_t ra_filter;  // 全局帧率滤波器实例

/**
 * @brief 初始化滑动平均滤波器
 * @param filter 滤波器指针
 * @param sample_size 采样窗口大小
 * @return 成功返回滤波器指针，失败返回 NULL
 */
ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values) return NULL;
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

/**
 * @brief 运行滑动平均滤波器
 * @param filter 滤波器指针
 * @param value 新的采样值
 * @return 滤波后的平均值
 */
int ra_filter_run(ra_filter_t *filter, int value)
{
    if (!filter->values) return value;
    // 移除最旧的值，添加新值
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    // 更新索引（循环缓冲区）
    filter->index = (filter->index + 1) % filter->size;
    // 更新已收集样本数
    if (filter->count < filter->size) filter->count++;
    return filter->sum / filter->count;
}

// ── GET 请求解析工具函数 ─────────────────────────────────────────────────────

/**
 * @brief 解析 HTTP GET 请求的查询字符串
 * @param req HTTP 请求句柄
 * @param obuf 输出缓冲区指针（需要调用者释放）
 * @return 成功返回 ESP_OK，失败返回 ESP_FAIL
 */
esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
    // 获取查询字符串长度
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len <= 1) {
        httpd_resp_send_404(req);  // 无查询参数，返回 404
        return ESP_FAIL;
    }

    // 分配内存并获取查询字符串
    char *buf = (char *)malloc(buf_len);
    if (!buf) {
        httpd_resp_send_500(req);  // 内存分配失败，返回 500
        return ESP_FAIL;
    }

    if (httpd_req_get_url_query_str(req, buf, buf_len) != ESP_OK) {
        free(buf);
        httpd_resp_send_404(req);  // 获取失败，返回 404
        return ESP_FAIL;
    }

    *obuf = buf;
    return ESP_OK;
}

/**
 * @brief 从查询字符串中获取指定键的整数值
 * @param buf 查询字符串
 * @param key 键名
 * @param def 默认值（键不存在时返回）
 * @return 解析后的整数值，失败返回默认值
 */
int parse_get_var(char *buf, const char *key, int def)
{
    char val[16];
    if (httpd_query_key_value(buf, key, val, sizeof(val)) != ESP_OK) return def;
    return atoi(val);
}

// ── 流媒体服务器启动（端口 81）─────────────────────────────────────────────
// 被 startCameraServer 和 reinit_camera 共用，避免配置/路由重复

/**
 * @brief 启动流媒体服务器
 *
 * 在端口 81 上启动独立的 HTTP 服务器，注册 /stream 和 /capture 路由。
 * 被 startCameraServer（首次启动）和 reinit_camera（重新初始化后重启）共用。
 *
 * @return true 成功，false 失败
 */
bool start_stream_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port   = 32769;
    config.max_uri_handlers = 4;
    config.max_open_sockets = 4;
    config.backlog_conn = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    if (httpd_start(&stream_httpd, &config) != ESP_OK) {
        ESP_LOGE(TAG, "流媒体服务器启动失败");
        return false;
    }

    static const httpd_uri_t stream_uris[] = {
        { .uri = "/stream",  .method = HTTP_GET, .handler = stream_handler,  .user_ctx = NULL },
        { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL },
    };
    for (size_t i = 0; i < sizeof(stream_uris) / sizeof(stream_uris[0]); i++) {
        httpd_register_uri_handler(stream_httpd, &stream_uris[i]);
    }
    return true;
}

// ── JPEG 分块编码回调函数 ────────────────────────────────────────────────────

/**
 * @brief JPEG 分块编码回调函数
 * 
 * 用于将摄像头帧转换为 JPEG 格式时的流式输出。
 * 每次回调都会发送一个数据块到 HTTP 客户端。
 * 
 * @param arg jpg_chunking_t 结构体指针
 * @param index 数据块索引（0 表示第一个块）
 * @param data 数据块指针
 * @param len 数据块长度
 * @return 成功返回数据长度，失败返回 0
 */
size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if (!index) j->len = 0;  // 第一个块，重置长度计数器
    // 发送数据块到客户端
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) return 0;
    j->len += len;  // 累加已发送长度
    return len;
}
