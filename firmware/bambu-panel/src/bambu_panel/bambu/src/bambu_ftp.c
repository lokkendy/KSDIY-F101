/*
 * Bambu LAN FTPS — implicit TLS port 990, user bblp + access code.
 */
#include "bambu_ftp.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_tls_errors.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include "esp_timer.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool upload_path_valid(const char *path);

static const char *TAG = "bambu_ftp";

#ifndef CONFIG_BAMBU_FTP_LIST_MAX
#define CONFIG_BAMBU_FTP_LIST_MAX 64
#endif
#ifndef CONFIG_BAMBU_FTP_PORT
#define CONFIG_BAMBU_FTP_PORT 990
#endif
#ifndef CONFIG_BAMBU_FTP_TLS_TIMEOUT_MS
#define CONFIG_BAMBU_FTP_TLS_TIMEOUT_MS 45000
#endif

static esp_tls_cfg_t tls_cfg(void)
{
    esp_tls_cfg_t c = { 0 };
#if CONFIG_BAMBU_LAN_TLS_INSECURE
    c.skip_common_name = true;
#endif
    c.timeout_ms = CONFIG_BAMBU_FTP_TLS_TIMEOUT_MS;
    return c;
}

/** 控制通道 FTPS（990）；MQTT 已连时打印机 TLS 握手偶发 >20s，带重试 */
static esp_err_t ftp_ctrl_connect(const bambu_lan_config_t *cfg, esp_tls_t **out)
{
    if (!cfg || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    esp_tls_cfg_t tcfg = tls_cfg();
    if (cfg->verify_printer) {
        tcfg.printer_serial = cfg->serial;
        tcfg.timeout_ms = 5000;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        esp_tls_t *ctrl = esp_tls_init();
        if (!ctrl) {
            return ESP_ERR_NO_MEM;
        }
        int ret = esp_tls_conn_new_sync(cfg->printer_ip, strlen(cfg->printer_ip),
                                        CONFIG_BAMBU_FTP_PORT, &tcfg, ctrl);
        if (ret == 1) {
            *out = ctrl;
            if (attempt > 0) {
                ESP_LOGW(TAG, "FTPS ctrl connected on retry %d", attempt);
            }
            return ESP_OK;
        }
        esp_tls_conn_destroy(ctrl);
        if (attempt < 2) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    ESP_LOGE(TAG, "FTPS connect %s:%d failed", cfg->printer_ip, CONFIG_BAMBU_FTP_PORT);
    return ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST;
}

static esp_err_t tls_write_line(esp_tls_t *tls, const char *line)
{
    char buf[512];
    size_t n = strlen(line);
    if (n + 2 >= sizeof(buf)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(buf, line, n);
    buf[n++] = '\r';
    buf[n++] = '\n';
    ssize_t w = esp_tls_conn_write(tls, buf, n);
    return (w == (ssize_t)n) ? ESP_OK : ESP_FAIL;
}

static esp_err_t tls_read_line(esp_tls_t *tls, char *buf, size_t cap)
{
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = esp_tls_conn_read(tls, &c, 1);
        if (n <= 0) {
            return ESP_FAIL;
        }
        buf[i++] = c;
        if (c == '\n') {
            break;
        }
    }
    buf[i] = 0;
    while (i > 0 && (buf[i - 1] == '\r' || buf[i - 1] == '\n')) {
        buf[--i] = 0;
    }
    return ESP_OK;
}

static int reply_code(const char *line)
{
    if (!line || strlen(line) < 3 || !isdigit((unsigned char)line[0])) {
        return -1;
    }
    return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
}

static esp_err_t ftp_read_reply(esp_tls_t *tls, char *last_line, size_t last_cap, int *code_out)
{
    char line[512];
    int code = -1;
    for (;;) {
        if (tls_read_line(tls, line, sizeof(line)) != ESP_OK) {
            return ESP_FAIL;
        }
        int c = reply_code(line);
        if (c >= 0) {
            if (code < 0) {
                code = c;
            }
            if (last_line && last_cap) {
                strncpy(last_line, line, last_cap - 1);
                last_line[last_cap - 1] = 0;
            }
            if (line[3] == ' ') {
                break;
            }
        }
    }
    if (code_out) {
        *code_out = code;
    }
    return ESP_OK;
}

static esp_err_t ftp_cmd(esp_tls_t *tls, const char *cmd, int expect_min, int expect_max,
                         char *last_line, size_t last_cap)
{
    if (tls_write_line(tls, cmd) != ESP_OK) {
        return ESP_FAIL;
    }
    int code = 0;
    if (ftp_read_reply(tls, last_line, last_cap, &code) != ESP_OK) {
        return ESP_FAIL;
    }
    if (code < expect_min || code > expect_max) {
        ESP_LOGW(TAG,"FTP '%s' -> %d",strncmp(cmd,"PASS ",5)==0?"PASS <redacted>":cmd,code);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool parse_pasv(const char *line, char *host, size_t host_cap, uint16_t *port_out)
{
    const char *p = strchr(line, '(');
    if (!p) {
        p = strrchr(line, ' ');
        if (!p) {
            return false;
        }
        p++;
    } else {
        p++;
    }
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return false;
    }
    if(h1<0||h1>255||h2<0||h2>255||h3<0||h3>255||h4<0||h4>255||p1<0||p1>255||p2<0||p2>255||!(p1*256+p2))return false;
    snprintf(host, host_cap, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port_out = (uint16_t)(p1 * 256 + p2);
    return true;
}

static bool name_is_printable(const char *name)
{
    if (!name || !name[0] || name[0] == '.') {
        return false;
    }
    size_t n = strlen(name);
    if (n > 6 && strcasecmp(name + n - 6, ".gcode") == 0) {
        return true;
    }
    if (n > 4 && strcasecmp(name + n - 4, ".3mf") == 0) {
        return true;
    }
    if (n > 11 && strcasecmp(name + n - 11, ".gcode.3mf") == 0) {
        return true;
    }
    return false;
}

static void trim_name(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) {
        s[--n] = 0;
    }
    char *start = s;
    while (*start == ' ') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
}

static esp_err_t ftp_session_setup(esp_tls_t *tls, const char *access_code)
{
    int code = 0;
    if (ftp_read_reply(tls, NULL, 0, &code) != ESP_OK || code != 220) {
        return ESP_FAIL;
    }
    if (ftp_cmd(tls, "USER bblp", 200, 331, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    char pass[128];
    snprintf(pass, sizeof(pass), "PASS %s", access_code);
    if (ftp_cmd(tls, pass, 200, 230, NULL, 0) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (ftp_cmd(tls, "PBSZ 0", 200, 200, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (ftp_cmd(tls, "PROT P", 200, 200, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    if (ftp_cmd(tls, "TYPE I", 200, 200, NULL, 0) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t ftp_cwd_path(esp_tls_t *tls, const char *path)
{
    if (!path || !path[0] || strcmp(path, "/") == 0 || strcmp(path, ".") == 0) {
        if (ftp_cmd(tls, "CWD /", 250, 250, NULL, 0) == ESP_OK) {
            return ESP_OK;
        }
        return ftp_cmd(tls, "CWD .", 250, 250, NULL, 0);
    }
    char cmd[BAMBU_FTP_NAME_MAX + 8];
    int n = snprintf(cmd, sizeof(cmd), "CWD %s", path);
    if (n < 0 || (size_t)n >= sizeof(cmd)) return ESP_ERR_INVALID_ARG;
    return ftp_cmd(tls, cmd, 250, 250, NULL, 0);
}

static const char *file_basename(const char *path)
{
    if (!path) {
        return "";
    }
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static bool entry_has_name(const bambu_ftp_entry_t *out, size_t count, const char *full_name)
{
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(out[i].name, full_name) == 0) {
            return true;
        }
    }
    return false;
}

/** unix ls -l 行解析：`-rw-rw-rw- 1 ftp ftp 4096 Feb 01 2024 name.3mf` */
static bool parse_list_line(const char *line, const char **name_out, uint32_t *size_out)
{
    if (line[0] != '-' && line[0] != 'd') {
        return false;
    }
    const char *p = line;
    int fields = 0;
    uint32_t size = 0;
    while (fields < 8) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            return false;
        }
        const char *fs = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (fields == 4) {
            size = (uint32_t)strtoul(fs, NULL, 10);
        }
        fields++;
    }
    while (*p == ' ') {
        p++;
    }
    if (!*p) {
        return false;
    }
    *name_out = p;
    *size_out = (line[0] == '-') ? size : 0; /* 目录不报尺寸 */
    return true;
}

static bool relative_directory(const char *path)
{
    if (!path || strlen(path) >= BAMBU_FTP_NAME_MAX || path[0] == '/') return false;
    while (*path) {
        size_t len = strcspn(path, "/");
        if (!len || (len == 1 && path[0] == '.') ||
            (len == 2 && path[0] == '.' && path[1] == '.')) return false;
        for (size_t i = 0; i < len; ++i)
            if ((unsigned char)path[i] < 32 || path[i] == 127 || path[i] == '\\') return false;
        path += len;
        if (*path && !*++path) return false;
    }
    return true;
}

bool bambu_ftp_is_printable_path(const char *path)
{
    return relative_directory(path) && name_is_printable(file_basename(path));
}

static size_t append_nlsts_blob(char *blob, const char *dir_prefix, bool browse, bambu_ftp_entry_t *out,
                                size_t out_max, size_t count)
{
    char *save = NULL;
    for (char *tok = strtok_r(blob, "\r\n", &save); tok; tok = strtok_r(NULL, "\r\n", &save)) {
        trim_name(tok);
        if (!tok[0]) {
            continue;
        }
        const char *base = tok;
        uint32_t size = 0;
        const char *list_name = NULL;
        bool parsed = parse_list_line(tok, &list_name, &size);
        bool directory = parsed && tok[0] == 'd';
        if (parsed) base = list_name;
        else if (browse) continue;
        else base = file_basename(tok);
        if (browse) {
            if (!base[0] || base[0] == '.' || strchr(base, '/') || !relative_directory(base)) continue;
        } else if (directory || !name_is_printable(base)) {
            continue;
        }
        char full[BAMBU_FTP_NAME_MAX];
        int length = dir_prefix && dir_prefix[0] ?
            snprintf(full, sizeof(full), "%s/%s", dir_prefix, base) :
            snprintf(full, sizeof(full), "%s", base);
        if (length < 0 || (size_t)length >= sizeof(full)) continue;
        if (entry_has_name(out, count, full)) {
            continue;
        }
        if (count >= out_max || count >= (size_t)CONFIG_BAMBU_FTP_LIST_MAX) {
            break;
        }
        strncpy(out[count].name, full, BAMBU_FTP_NAME_MAX - 1);
        out[count].name[BAMBU_FTP_NAME_MAX - 1] = 0;
        out[count].size_bytes = size;
        out[count].directory = directory;
        count++;
    }
    return count;
}

static esp_err_t ftp_open_data(esp_tls_t *ctrl, esp_tls_t **data_out);

static esp_err_t ftp_transfer_collect(esp_tls_t *tls, const char *cmd_name,
                                      const char *dir_prefix, bool browse, bambu_ftp_entry_t *out,
                                      size_t out_max, size_t *inout_count)
{
    esp_tls_t *data = NULL;
    esp_err_t err = ftp_open_data(tls, &data);
    if (err != ESP_OK) return err;

    if (tls_write_line(tls, cmd_name) != ESP_OK) {
        esp_tls_conn_destroy(data);
        return ESP_FAIL;
    }

    enum { k_nlst_cap = 65536 };
    char *blob = (char *)malloc(k_nlst_cap);
    if (!blob) {
        esp_tls_conn_destroy(data);
        return ESP_ERR_NO_MEM;
    }
    size_t total = 0;
    for (;;) {
        if (total + 512 >= k_nlst_cap) {
            break;
        }
        ssize_t n = esp_tls_conn_read(data, blob + total, k_nlst_cap - total - 1);
        if (n <= 0) {
            break;
        }
        total += (size_t)n;
    }
    blob[total] = 0;
    esp_tls_conn_destroy(data);

    /* 拓竹常见：150 开数据连接 → 226 结束；须读尽 2xx，否则 QUIT 会误读到 226 */
    int code = 0;
    esp_err_t xfer_ok = ESP_FAIL;
    for (int i = 0; i < 12; i++) {
        if (ftp_read_reply(tls, NULL, 0, &code) != ESP_OK) {
            break;
        }
        if (code == 226 || code == 250) {
            xfer_ok = ESP_OK;
            break;
        }
        if (code >= 200 && code < 300 && code != 150 && code != 125) {
            xfer_ok = ESP_OK;
            break;
        }
    }
    if (xfer_ok != ESP_OK) {
        ESP_LOGW(TAG, "%s xfer reply %d", cmd_name, code);
        free(blob);
        return ESP_FAIL;
    }

    size_t count = inout_count && *inout_count ? *inout_count : 0;
    size_t before = count;
    count = append_nlsts_blob(blob, dir_prefix, browse, out, out_max, count);
    ESP_LOGI(TAG, "%s %s: +%u names (raw %u bytes)", cmd_name,
             dir_prefix && dir_prefix[0] ? dir_prefix : "/",
             (unsigned)(count - before), (unsigned)total);
    free(blob);
    if (inout_count) {
        *inout_count = count;
    }
    return ESP_OK;
}

static esp_err_t ftp_nlst_collect(esp_tls_t *tls, const bambu_lan_config_t *cfg,
                                  const char *dir_prefix, bambu_ftp_entry_t *out, size_t out_max,
                                  size_t *out_count)
{
    size_t count = out_count && *out_count ? *out_count : 0;
    /* LIST 带 size；个别固件只认 NLST → 同参降级重试 */
    esp_err_t err = ftp_transfer_collect(tls, "LIST", dir_prefix, false, out, out_max, &count);
    if (err != ESP_OK) {
        count = out_count && *out_count ? *out_count : 0;
        err = ftp_transfer_collect(tls, "NLST", dir_prefix, false, out, out_max, &count);
    }
    if (out_count) {
        *out_count = count;
    }
    (void)cfg;
    return err;
}

static esp_err_t ftp_list_one_dir(esp_tls_t *tls, const bambu_lan_config_t *cfg,
                                  const char *cwd_path, const char *dir_prefix,
                                  bambu_ftp_entry_t *out, size_t out_max, size_t *inout_count)
{
    if (ftp_cwd_path(tls, cwd_path) != ESP_OK) {
        ESP_LOGD(TAG, "skip dir %s (CWD fail)", cwd_path ? cwd_path : "/");
        return ESP_OK;
    }
    return ftp_nlst_collect(tls, cfg, dir_prefix, out, out_max, inout_count);
}

esp_err_t bambu_ftp_list_files(const bambu_lan_config_t *cfg, bambu_ftp_scope_t scope,
                               bambu_ftp_entry_t *out, size_t out_max, size_t *out_count)
{
    if (out_count) {
        *out_count = 0;
    }
    if (!cfg || !cfg->printer_ip[0] || !cfg->access_code[0] || !out || out_max == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_tls_t *ctrl = NULL;
    esp_err_t err = ftp_ctrl_connect(cfg, &ctrl);
    if (err != ESP_OK) {
        return err;
    }

    err = ftp_session_setup(ctrl, cfg->access_code);
    if (err != ESP_OK) {
        esp_tls_conn_destroy(ctrl);
        return err;
    }
    size_t total = 0;
    if (scope == BAMBU_FTP_SCOPE_CACHE) {
        (void)ftp_list_one_dir(ctrl, cfg, "cache", "cache", out, out_max, &total);
    } else {
        (void)ftp_list_one_dir(ctrl, cfg, "cache", "cache", out, out_max, &total);
        /* P1/A1 系型号目录为 models（X1C 无此目录，550 自动跳过） */
        (void)ftp_list_one_dir(ctrl, cfg, "models", "models", out, out_max, &total);
        (void)ftp_list_one_dir(ctrl, cfg, "model", "model", out, out_max, &total);
        (void)ftp_list_one_dir(ctrl, cfg, "/", NULL, out, out_max, &total);
    }
    if (out_count) {
        *out_count = total;
    }
    err = (total > 0) ? ESP_OK : ESP_FAIL;
    ESP_LOGI(TAG, "FTPS list total %u files", (unsigned)total);
    if (tls_write_line(ctrl, "QUIT") == ESP_OK) {
        (void)ftp_read_reply(ctrl, NULL, 0, NULL);
    }
    esp_tls_conn_destroy(ctrl);
    return err;
}

esp_err_t bambu_ftp_list_directory(const bambu_lan_config_t *cfg, const char *path,
                                  bambu_ftp_entry_t *out, size_t out_max, size_t *out_count)
{
    if (out_count) *out_count = 0;
    if (!cfg || !cfg->printer_ip[0] || !cfg->access_code[0] || !relative_directory(path) ||
        !out || !out_max || !out_count) return ESP_ERR_INVALID_ARG;

    esp_tls_t *ctrl = NULL;
    esp_err_t err = ftp_ctrl_connect(cfg, &ctrl);
    if (err != ESP_OK) return err;
    err = ftp_session_setup(ctrl, cfg->access_code);
    if (err == ESP_OK) err = ftp_cwd_path(ctrl, path);
    if (err == ESP_OK) err = ftp_transfer_collect(ctrl, "LIST", path, true, out, out_max, out_count);
    if (err == ESP_OK && tls_write_line(ctrl, "QUIT") == ESP_OK)
        (void)ftp_read_reply(ctrl, NULL, 0, NULL);
    esp_tls_conn_destroy(ctrl);
    return err;
}

esp_err_t bambu_ftp_delete_file(const bambu_lan_config_t *cfg, const char *path)
{
    if (!cfg || !upload_path_valid(path)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cfg->printer_ip[0] || !cfg->access_code[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 相对路径拆目录 + 文件名：cache/foo.3mf → CWD cache + DELE foo.3mf */
    const char *base = path;
    char dir[BAMBU_FTP_NAME_MAX];
    dir[0] = 0;
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - path);
        if (dlen == 0 || dlen >= sizeof(dir)) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(dir, path, dlen);
        dir[dlen] = 0;
        base = slash + 1;
    }
    if (!base[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_tls_t *ctrl = NULL;
    esp_err_t err = ftp_ctrl_connect(cfg, &ctrl);
    if (err != ESP_OK) {
        return err;
    }
    err = ftp_session_setup(ctrl, cfg->access_code);
    if (err != ESP_OK) {
        esp_tls_conn_destroy(ctrl);
        return err;
    }
    if (ftp_cwd_path(ctrl, dir[0] ? dir : "/") != ESP_OK) {
        esp_tls_conn_destroy(ctrl);
        return ESP_ERR_INVALID_ARG;
    }
    char cmd[BAMBU_FTP_NAME_MAX + 8];
    snprintf(cmd, sizeof(cmd), "DELE %s", base);
    err = ftp_cmd(ctrl, cmd, 200, 299, NULL, 0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "FTPS deleted %s", path);
    } else {
        ESP_LOGW(TAG, "FTPS delete %s failed", path);
    }
    if (tls_write_line(ctrl, "QUIT") == ESP_OK) {
        (void)ftp_read_reply(ctrl, NULL, 0, NULL);
    }
    esp_tls_conn_destroy(ctrl);
    return err;
}

/* ── 3mf 内嵌封面：流式解析 ZIP local file header（PNG 条目 STORED） ── */

/* n<=0 不再笼统当 EOF：esp-tls 直通 mbedtls 错误码（负数=SSL 错误，0=对端干净关闭），
 * 经 err 带出供 miss 日志定案（服务器掐流 426 vs 读错误 vs 正常尽头） */
static esp_err_t data_read_full(esp_tls_t *tls, uint8_t *buf, size_t need, int *err)
{
    size_t got = 0;
    while (got < need) {
        ssize_t n = esp_tls_conn_read(tls, buf + got, need - got);
        if (n <= 0) {
            if (err) {
                *err = (int)n;
            }
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    return ESP_OK;
}

static esp_err_t data_skip(esp_tls_t *tls, uint32_t bytes, int *err)
{
    uint8_t sink[4096];   /* 4KB 汇：plate PNG 常在多 MB gcode 之后，跳读吞吐要够快 */
    while (bytes) {
        size_t chunk = bytes > sizeof(sink) ? sizeof(sink) : bytes;
        if (data_read_full(tls, sink, chunk, err) != ESP_OK) {
            return ESP_FAIL;
        }
        bytes -= (uint32_t)chunk;
    }
    return ESP_OK;
}

/** 条目名是否为封面目标：Metadata/plate_<N>[_small].png（plate_idx=0 放宽为任意 plate） */
static bool cover_name_match(const char *name, int plate_idx)
{
    const char *pfx = "Metadata/plate_";
    if (strncmp(name, pfx, strlen(pfx)) != 0) {
        return false;
    }
    const char *p = name + strlen(pfx);
    if (plate_idx > 0) {
        char want[24];
        int n = snprintf(want, sizeof(want), "%d", plate_idx);
        if (strncmp(p, want, (size_t)n) != 0) {
            return false;
        }
        p += n;
    } else {
        while (*p >= '0' && *p <= '9') {
            p++;
        }
    }
    if (strncmp(p, "_small", 6) == 0) {
        p += 6;
    }
    return strcmp(p, ".png") == 0;
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* ── 流式扫描器的滚动窗口 ──
 * 拓竹 3MF 全条目带 data descriptor（local header csize 恒 0），P1 FTPS 又
 * 不支持 REST（502），无法按偏移取 central directory——只能整文件流式扫，
 * descriptor 条目用“找下一个合法 local header”特征重同步，PNG 长度按
 * PNG 自身 chunk 结构读到 IEND 为止 */
typedef struct {
    esp_tls_t *tls;
    uint8_t buf[8192];
    size_t len;        /* buf 内有效字节 */
    size_t pos;        /* 已消费位置 */
    uint32_t streamed; /* 累计流过字节（计账/预算） */
    int err;           /* 最近一次读返回（负数=mbedtls 错误，0=干净 EOF） */
    uint8_t rej_logged; /* 诊断：已记录的被拒候选数 */
    uint8_t cd_logged;  /* 诊断：已记录的 central directory 条目数 */
} zstream_t;

static size_t z_fill(zstream_t *s, size_t need)
{
    if (s->pos == s->len) {
        s->pos = s->len = 0;
    } else if (s->pos > 0) {
        memmove(s->buf, s->buf + s->pos, s->len - s->pos);
        s->len -= s->pos;
        s->pos = 0;
    }
    while (s->len < need && s->len < sizeof(s->buf)) {
        ssize_t n = esp_tls_conn_read(s->tls, s->buf + s->len, sizeof(s->buf) - s->len);
        if (n <= 0) {
            s->err = (int)n;
            break;
        }
        s->len += (size_t)n;
        s->streamed += (uint32_t)n;
    }
    return s->len;
}

static bool z_take(zstream_t *s, uint8_t *dst, size_t need)
{
    if (need > sizeof(s->buf) / 2) {
        /* 大块（PNG 载荷）：取尽窗口后直读，避免反复搬缓冲 */
        size_t avail = s->len - s->pos;
        if (avail) {
            memcpy(dst, s->buf + s->pos, avail);
            s->pos = s->len;
            dst += avail;
            need -= avail;
        }
        if (data_read_full(s->tls, dst, need, &s->err) != ESP_OK) {
            return false;
        }
        s->streamed += (uint32_t)need;
        return true;
    }
    if (z_fill(s, need) < need) {
        return false;
    }
    memcpy(dst, s->buf + s->pos, need);
    s->pos += need;
    return true;
}

/* 丢弃 want 字节（跳过 gcode 载荷），只推进游标不拷贝 */
static bool z_discard(zstream_t *s, size_t want)
{
    while (want) {
        if (s->pos >= s->len && z_fill(s, 1) == 0) {
            return false;
        }
        size_t avail = s->len - s->pos;
        size_t chunk = want < avail ? want : avail;
        s->pos += chunk;
        want -= chunk;
    }
    return true;
}

/* 长度未知（descriptor）条目后重同步：找下一个字段合理的 local header 并停
 * 在其起点。候选校验 fnlen/exlen/method 边界——压缩数据里 4 字节偶合再过
 * 三组字段校验的概率可忽略；校验不过则继续推进一个字节 */
static bool z_resync(zstream_t *s)
{
    for (;;) {
        if (s->len - s->pos < 30 && z_fill(s, 30) < 30) {
            return false;   /* 流尽（err==0=干净 EOF）或读错误 */
        }
        const uint8_t *p = s->buf + s->pos;
        if (p[0] == 'P' && p[1] == 'K') {
            if (p[2] == 3 && p[3] == 4) {
                /* local header：method@8、fnlen@26、exlen@28（CD 记录 method 才在
                 * +10——曾按 CD 布局读成修改时间，真头全被校验拒绝） */
                uint16_t fnlen = rd_le16(p + 26);
                uint16_t exlen = rd_le16(p + 28);
                uint16_t method = rd_le16(p + 8);
                if (fnlen >= 1 && fnlen <= 128 && exlen <= 256 && method <= 8) {
                    return true;
                }
                if (s->rej_logged < 8) {
                    s->rej_logged++;
                    ESP_LOGW(TAG, "z_resync reject %u: fnlen=%u exlen=%u method=%u",
                             s->rej_logged, fnlen, exlen, method);
                }
                s->pos++;
                continue;
            }
            if (p[2] == 1 && p[3] == 2 && s->cd_logged < 12) {
                /* central directory 记录（name@46/method@10/csize@20/lho@42）：
                 * 扫描器走到归档尾必然路过 CD——顺手记下真实条目表，
                 * miss 时无需二次连接即可看到文件里到底有什么 */
                uint16_t cdfn = rd_le16(p + 28);
                uint16_t cdex = rd_le16(p + 30);
                if (cdfn >= 1 && cdfn <= 200 && cdex <= 256) {
                    size_t want = 46 + (cdfn < 60 ? cdfn : 59);
                    if (s->len - s->pos < want) {
                        z_fill(s, want);   /* 压实后 pos 归 0：重取 p */
                        p = s->buf + s->pos;
                    }
                    if (s->len - s->pos >= want) {
                        char nm[60];
                        memcpy(nm, p + 46, want - 46);
                        nm[want - 46] = 0;
                        s->cd_logged++;
                        ESP_LOGW(TAG, "cd-tail #%u: %s method=%u csize=%u lho=%u",
                                 s->cd_logged, nm, rd_le16(p + 10), rd_le32(p + 20),
                                 rd_le32(p + 42));
                    }
                }
                s->pos++;
                continue;
            }
        }
        const uint8_t *end = s->buf + s->len;
        const uint8_t *q = memchr(p + 1, 'P', (size_t)(end - (p + 1)));
        s->pos = q ? (size_t)(q - s->buf) : s->len;
    }
}

/* PNG 结构直读：签名 + chunk(4B 大端长度+4B 类型+数据+4B crc) 走到 IEND。
 * 用于 csize 未知的 STORED 封面条目——长度由 PNG 自己声明 */
static bool z_copy_png(zstream_t *s, uint8_t *out, size_t cap, size_t *out_len)
{
    uint8_t sig[8];
    if (!z_take(s, sig, sizeof(sig))) {
        return false;
    }
    if (sig[0] != 0x89 || sig[1] != 'P' || sig[2] != 'N' || sig[3] != 'G' ||
        sig[4] != '\r' || sig[5] != '\n' || sig[6] != 0x1A || sig[7] != '\n') {
        return false;
    }
    memcpy(out, sig, sizeof(sig));
    size_t len = sizeof(sig);
    for (;;) {
        uint8_t ch[8];
        if (!z_take(s, ch, sizeof(ch))) {
            return false;
        }
        uint32_t clen = ((uint32_t)ch[0] << 24) | ((uint32_t)ch[1] << 16) |
                        ((uint32_t)ch[2] << 8) | (uint32_t)ch[3];
        if (len + 8 + (size_t)clen + 4 > cap) {
            return false;   /* 超缓冲（调用方预算装不下，如全尺寸 plate PNG） */
        }
        memcpy(out + len, ch, sizeof(ch));
        len += sizeof(ch);
        if (!z_take(s, out + len, (size_t)clen + 4)) {
            return false;
        }
        len += (size_t)clen + 4;
        if (memcmp(ch + 4, "IEND", 4) == 0) {
            *out_len = len;
            return true;
        }
    }
}

/* PASV + 数据通道 TLS 连接（复用 ctrl 会话） */
static esp_err_t ftp_open_data(esp_tls_t *ctrl, esp_tls_t **data_out)
{
    char pasv_line[512];
    if (ftp_cmd(ctrl, "PASV", 200, 227, pasv_line, sizeof(pasv_line)) != ESP_OK) {
        return ESP_FAIL;
    }
    char data_host[32];
    uint16_t data_port = 0;
    if (!parse_pasv(pasv_line, data_host, sizeof(data_host), &data_port)) {
        return ESP_FAIL;
    }
    esp_tls_cfg_t dcfg = tls_cfg();
    esp_tls_t *data = esp_tls_init();
    if (!data) {
        return ESP_ERR_NO_MEM;
    }
    const char *host = data_host;
    if (ctrl->lan_serial[0]) {
        host = ctrl->lan_host;
        dcfg.printer_serial = ctrl->lan_serial;
        dcfg.timeout_ms = 5000;
    }
    if (esp_tls_conn_new_sync(host, strlen(host), data_port, &dcfg, data) != 1) {
        esp_tls_conn_destroy(data);
        return ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST;
    }
    *data_out = data;
    return ESP_OK;
}

/* 数据通道收尾：读 ctrl 最终应答（226=完成 / 426=服务器掐流），非 2xx 记日志 */
static void ftp_retr_finish(esp_tls_t *ctrl)
{
    char line[128];
    int code = 0;
    if (ftp_read_reply(ctrl, line, sizeof(line), &code) == ESP_OK && code / 100 != 2) {
        ESP_LOGW(TAG, "ftp final reply %d: %.60s", code, line);
    }
}

/* 开数据通道并对 RETR 设 REST 起点（REST 须在 PASV 之后、RETR 之前） */
static esp_err_t ftp_retr_open(esp_tls_t *ctrl, const char *base, uint32_t offset,
                               esp_tls_t **data_out)
{
    esp_tls_t *data = NULL;
    if (ftp_open_data(ctrl, &data) != ESP_OK) {
        return ESP_FAIL;
    }
    char cmd[BAMBU_FTP_NAME_MAX + 16];
    esp_err_t err = ESP_FAIL;
    if (offset) {
        snprintf(cmd, sizeof(cmd), "REST %lu", (unsigned long)offset);
        if (ftp_cmd(ctrl, cmd, 300, 399, NULL, 0) != ESP_OK) {
            goto out;
        }
    }
    snprintf(cmd, sizeof(cmd), "RETR %s", base);
    if (tls_write_line(ctrl, cmd) != ESP_OK) {
        goto out;
    }
    int rcode = 0;
    if (ftp_read_reply(ctrl, NULL, 0, &rcode) != ESP_OK || rcode / 100 != 1) {
        ESP_LOGW(TAG, "RETR %s @%lu -> %d", base, (unsigned long)offset, rcode);
        goto out;
    }
    err = ESP_OK;
out:
    if (err != ESP_OK) {
        esp_tls_conn_destroy(data);
    } else {
        *data_out = data;
    }
    return err;
}

/* ── 3MF 封面：central directory 直取（拓竹流式 zip 的唯一可行路径） ──
 * 真机定案：拓竹 3MF 条目带 data descriptor（local header csize=0），
 * 逐条目跳读立即失步（sync lost）。改为：
 *   SIZE 取文件长 → REST 读尾部窗口 → 解 EOCD/central directory →
 *   取 plate PNG 条目的 local 头偏移与真实 csize → 二次 REST 精确读 PNG。
 * 返回：ESP_OK 命中 / ESP_ERR_NOT_FOUND=CD 已解但无目标（权威 miss）/
 * ESP_FAIL=SIZE·REST·结构不支持（调用方回退旧扫描）。out 借作尾窗暂存 */
static esp_err_t fetch_cover_via_central_dir(esp_tls_t *ctrl, const char *base,
                                             int plate_idx, uint8_t *out, size_t out_cap,
                                             size_t *out_len)
{
    char cmd[BAMBU_FTP_NAME_MAX + 16];
    char line[128];

    snprintf(cmd, sizeof(cmd), "SIZE %s", base);
    if (ftp_cmd(ctrl, cmd, 213, 213, line, sizeof(line)) != ESP_OK) {
        ESP_LOGW(TAG, "SIZE %s unsupported -> legacy scan", base);
        return ESP_FAIL;
    }
    unsigned long fsize_l = 0;
    if (sscanf(line + 3, "%lu", &fsize_l) != 1 || fsize_l < 68 || fsize_l > 0xFFFFFFFFul) {
        return ESP_FAIL;
    }
    uint32_t fsize = (uint32_t)fsize_l;
    if (ftp_cmd(ctrl, "REST 0", 300, 399, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "REST unsupported -> legacy scan");
        return ESP_FAIL;
    }

    /* 尾窗 = EOCD(22B) + 注释(≤64KB) + CD（Bambu 几十条目约 10KB），
     * 128KB 覆盖最坏情况且小于调用方 192KB 缓冲 */
    size_t tail = (size_t)fsize < out_cap ? (size_t)fsize : out_cap;
    if (tail > 128u * 1024u) {
        tail = 128u * 1024u;
    }
    uint32_t tail_off = fsize - (uint32_t)tail;

    esp_tls_t *data = NULL;
    int rerr = 0;
    esp_err_t err = ESP_FAIL;
    if (ftp_retr_open(ctrl, base, tail_off, &data) == ESP_OK) {
        err = data_read_full(data, out, tail, &rerr);
        esp_tls_conn_destroy(data);
        ftp_retr_finish(ctrl);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "tail read %lu..%lu fail rc=%d", (unsigned long)tail_off,
                     (unsigned long)fsize, rerr);
        }
    } else {
        ESP_LOGW(TAG, "tail RETR @%lu rejected", (unsigned long)tail_off);
    }
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    /* EOCD（PK\x05\x06）从尾部向前找 */
    const uint8_t *eocd = NULL;
    if (tail >= 22) {
        const uint8_t *p = out + tail - 22;
        for (;;) {
            if (p[0] == 'P' && p[1] == 'K' && p[2] == 5 && p[3] == 6) {
                eocd = p;
                break;
            }
            if (p == out) {
                break;
            }
            p--;
        }
    }
    if (!eocd) {
        ESP_LOGW(TAG, "no EOCD in tail (%u bytes)", (unsigned)tail);
        return ESP_FAIL;
    }
    uint32_t cd_size = rd_le32(eocd + 12);
    uint32_t cd_off = rd_le32(eocd + 16);
    uint16_t cd_cnt = rd_le16(eocd + 10);
    if (cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu || cd_size == 0 || cd_cnt == 0xFFFFu) {
        ESP_LOGW(TAG, "zip64 central directory -> legacy scan");
        return ESP_FAIL;
    }
    if (cd_off < tail_off || (size_t)(cd_off - tail_off) + cd_size > tail) {
        ESP_LOGW(TAG, "cd window mismatch off=%lu size=%lu tail@%lu+%u",
                 (unsigned long)cd_off, (unsigned long)cd_size, (unsigned long)tail_off,
                 (unsigned)tail);
        return ESP_FAIL;
    }

    /* 遍历 central directory 找目标（先见者先得：plate_N.png 排在 _small 前）。
     * CD 的 csize 是真实值；条目即使带 data descriptor 也能精确读取 */
    const uint8_t *cd = out + (cd_off - tail_off);
    const uint8_t *cd_end = cd + cd_size;
    uint32_t png_off = 0;
    uint32_t png_csize = 0;
    char png_name[80];
    png_name[0] = 0;
    while (cd + 46 <= cd_end) {
        if (!(cd[0] == 'P' && cd[1] == 'K' && cd[2] == 1 && cd[3] == 2)) {
            break;
        }
        uint16_t fnlen = rd_le16(cd + 28);
        uint16_t exlen = rd_le16(cd + 30);
        uint16_t cmtlen = rd_le16(cd + 32);
        size_t rec = 46u + (size_t)fnlen + exlen + cmtlen;
        if ((size_t)(cd_end - cd) < rec) {
            break;   /* CD 截断/损坏 */
        }
        uint16_t method = rd_le16(cd + 10);
        uint32_t csize = rd_le32(cd + 20);
        uint32_t lho = rd_le32(cd + 42);
        char nm[80];
        size_t cp = fnlen < sizeof(nm) - 1 ? fnlen : sizeof(nm) - 1;
        memcpy(nm, cd + 46, cp);
        nm[cp] = 0;
        if (!png_csize && lho != 0xFFFFFFFFu && method == 0 && csize >= 8 && csize <= out_cap &&
            cover_name_match(nm, plate_idx)) {
            png_off = lho;
            png_csize = csize;
            snprintf(png_name, sizeof(png_name), "%s", nm);
        }
        cd += rec;
    }
    if (!png_csize) {
        ESP_LOGW(TAG, "cd: no stored plate png in %d entries", (int)cd_cnt);
        return ESP_ERR_NOT_FOUND;
    }

    /* 二次 REST：跳过 local 头(30B)+名+extra，按 CD 尺寸精确读 PNG */
    if (ftp_retr_open(ctrl, base, png_off, &data) != ESP_OK) {
        return ESP_FAIL;
    }
    uint8_t lh[30];
    rerr = 0;
    err = data_read_full(data, lh, sizeof(lh), &rerr);
    if (err == ESP_OK && !(lh[0] == 'P' && lh[1] == 'K' && lh[2] == 3 && lh[3] == 4)) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        uint32_t head_extra = (uint32_t)rd_le16(lh + 26) + rd_le16(lh + 28);
        if (data_skip(data, head_extra, &rerr) == ESP_OK &&
            data_read_full(data, out, png_csize, &rerr) == ESP_OK &&
            out[0] == 0x89 && out[1] == 'P' && out[2] == 'N' && out[3] == 'G') {
            if (out_len) {
                *out_len = png_csize;
            }
            esp_tls_conn_destroy(data);
            ftp_retr_finish(ctrl);
            ESP_LOGI(TAG, "cover via cd: %s %s %u bytes @%lu/%lu", base, png_name,
                     (unsigned)png_csize, (unsigned long)png_off, (unsigned long)fsize);
            return ESP_OK;
        }
        err = ESP_FAIL;
    }
    esp_tls_conn_destroy(data);
    ftp_retr_finish(ctrl);
    ESP_LOGW(TAG, "png read @%lu fail rc=%d", (unsigned long)png_off, rerr);
    return ESP_FAIL;
}


esp_err_t bambu_ftp_fetch_cover(const bambu_lan_config_t *cfg, const char *rel_path,
                                int plate_idx, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (out_len) {
        *out_len = 0;
    }
    if (!cfg || !upload_path_valid(rel_path) || !out || out_cap < 4096) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cfg->printer_ip[0] || !cfg->access_code[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 拆目录/文件名（同 delete） */
    const char *base = rel_path;
    char dir[BAMBU_FTP_NAME_MAX];
    dir[0] = 0;
    const char *slash = strrchr(rel_path, '/');
    if (slash) {
        size_t dlen = (size_t)(slash - rel_path);
        if (dlen == 0 || dlen >= sizeof(dir)) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(dir, rel_path, dlen);
        dir[dlen] = 0;
        base = slash + 1;
    }
    if (!base[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_tls_t *ctrl = NULL;
    esp_err_t err = ftp_ctrl_connect(cfg, &ctrl);
    if (err != ESP_OK) {
        return err;
    }
    err = ftp_session_setup(ctrl, cfg->access_code);
    if (err != ESP_OK) {
        esp_tls_conn_destroy(ctrl);
        return err;
    }
    if (ftp_cwd_path(ctrl, dir[0] ? dir : "/") != ESP_OK) {
        esp_tls_conn_destroy(ctrl);
        return ESP_ERR_INVALID_ARG;
    }

    /* 首选 central directory 直取（拓竹流式 zip 的唯一可行路径）；
     * SIZE/REST/结构不支持（ESP_FAIL）才回退旧的逐条目扫描（适用常规 zip） */
    esp_err_t result = fetch_cover_via_central_dir(ctrl, base, plate_idx, out, out_cap, out_len);
    if (result == ESP_FAIL) {
        result = ESP_ERR_NOT_FOUND;
        esp_tls_t *data = NULL;
        if (ftp_retr_open(ctrl, base, 0, &data) == ESP_OK) {
            /* 整文件流式扫（ftp_retr_open 已读 1xx 过渡应答）：
             * descriptor 条目特征重同步，PNG 按 chunk 结构读到 IEND。
             * 扫描窗口 8KB 必须堆分配——压任务栈曾致栈保护 fault
             * （worker 仅 8K 栈，fetch_cover 帧里 8KB buf 直接越界） */
            enum { k_scan_budget = 32u * 1024u * 1024u };
            zstream_t *zs = calloc(1, sizeof(*zs));
            if (!zs) {
                ESP_LOGW(TAG, "cover scan window oom");
            } else {
                zs->tls = data;
                bool saw_zip = false;
                char last_name[80];
                uint32_t last_csize = 0;
                const char *why = "eof";
                last_name[0] = 0;
                while (zs->streamed < k_scan_budget) {
                    uint8_t hdr[30];
                    if (!z_take(zs, hdr, sizeof(hdr))) {
                        why = zs->err ? "hdr read err" : "eof";
                        break;   /* 数据流尽头（= 文件扫完）：无封面 */
                    }
                    if (hdr[0] != 'P' || hdr[1] != 'K' || hdr[2] != 3 || hdr[3] != 4) {
                        if (!saw_zip) {
                            why = "not a zip";
                            break;   /* 非 3mf：别把整个非归档文件流一遍 */
                        }
                        /* 跳读落空（如 csize 两种口径不一/描述符未立 flag）：落点
                         * 常是条目间残余，下一个真头可能恰好横跨在这 30 字节里。
                         * 小块 z_take 走窗口路径字节仍在 buf——回退游标到命中处；
                         * 否则特征重同步兜底，不再一票判死 */
                        bool backtracked = false;
                        for (size_t k = 1; k + 4 <= sizeof(hdr); k++) {
                            if (hdr[k] == 'P' && hdr[k + 1] == 'K' && hdr[k + 2] == 3 &&
                                hdr[k + 3] == 4) {
                                zs->pos -= sizeof(hdr) - k;
                                backtracked = true;
                                break;
                            }
                        }
                        if (backtracked) {
                            continue;
                        }
                        if (!z_resync(zs)) {
                            why = zs->err ? "resync err" : "sync lost";
                            break;
                        }
                        continue;
                    }
                    saw_zip = true;
                    uint16_t flags = rd_le16(hdr + 6);
                    uint16_t method = rd_le16(hdr + 8);
                    uint32_t csize = rd_le32(hdr + 18);
                    uint16_t fnlen = rd_le16(hdr + 26);
                    uint16_t exlen = rd_le16(hdr + 28);
                    char name[80];
                    size_t want_name = fnlen < sizeof(name) - 1 ? fnlen : sizeof(name) - 1;
                    if (want_name && !z_take(zs, (uint8_t *)name, want_name)) {
                        why = zs->err ? "name read err" : "eof";
                        break;
                    }
                    name[want_name] = 0;
                    snprintf(last_name, sizeof(last_name), "%s", name);
                    last_csize = csize;
                    /* 超长名剩余部分 + extra 统称前缀 */
                    uint32_t pre = (uint32_t)exlen + (uint32_t)(fnlen - want_name);

                    bool matched = cover_name_match(name, plate_idx) && method == 0;
                    bool consumed = false;
                    if (matched && ((flags & 0x08) || csize == 0)) {
                        /* 流式条目：zip 侧长度未知，按 PNG chunk 结构读到 IEND */
                        size_t got = 0;
                        if (z_discard(zs, pre) && z_copy_png(zs, out, out_cap, &got)) {
                            if (out_len) {
                                *out_len = got;
                            }
                            result = ESP_OK;
                            ESP_LOGI(TAG, "cover %s: %s %u bytes png-walk (%u KB streamed)",
                                     rel_path, name, (unsigned)got,
                                     (unsigned)(zs->streamed >> 10));
                            break;
                        }
                        if (zs->err) {
                            why = "png walk err";
                            break;
                        }
                        /* 缓冲装不下（如全尺寸 plate PNG 超 192KB）或坏流：
                         * 重同步继续找 _small 等后续候选 */
                        if (!z_resync(zs)) {
                            why = zs->err ? "png resync err" : "eof";
                            break;
                        }
                        consumed = true;
                    } else if (matched && csize >= 8 && csize <= out_cap) {
                        if (z_discard(zs, pre) && z_take(zs, out, csize) &&
                            out[0] == 0x89 && out[1] == 'P' && out[2] == 'N' &&
                            out[3] == 'G') {
                            if (out_len) {
                                *out_len = csize;
                            }
                            result = ESP_OK;
                            ESP_LOGI(TAG, "cover %s: %s %u bytes (%u KB streamed)", rel_path,
                                     name, (unsigned)csize, (unsigned)(zs->streamed >> 10));
                            break;
                        }
                        why = zs->err ? "png read err" : "eof";
                        break;   /* 命中但读断流：不再继续 */
                    }
                    if (!consumed) {
                        if (flags & 0x08) {
                            /* data descriptor 条目：csize 不可信（恒 0，个别写库
                             * 两值并存），跳过已知部分后特征重同步 */
                            if (!z_discard(zs, pre + csize) || !z_resync(zs)) {
                                why = zs->err ? "desc resync err" : "eof";
                                break;
                            }
                        } else if (!z_discard(zs, pre + csize)) {
                            why = zs->err ? "skip err" : "eof";
                            break;
                        }
                    }
                }
                if (result != ESP_OK) {
                    if (strcmp(why, "eof") == 0 && zs->streamed >= k_scan_budget) {
                        why = "budget";
                    }
                    ESP_LOGW(TAG, "cover miss %s (%s, %u KB streamed, last '%s' csize=%u rc=%d)",
                             rel_path, why, (unsigned)(zs->streamed >> 10), last_name,
                             (unsigned)last_csize, zs->err);
                }
                free(zs);
            }
        }
        if (data) {
            esp_tls_conn_destroy(data);
        }

        /* 命中即断流：服务器侧 426/226 皆可能，读尽即可 QUIT；
         * 非 2xx 的最终应答是传输异常的直接证据（426=服务器主动掐流） */
        for (int i = 0; i < 8; i++) {
            char line[128];
            int rcode = 0;
            if (ftp_read_reply(ctrl, line, sizeof(line), &rcode) != ESP_OK) {
                break;
            }
            if (rcode / 100 != 2) {
                ESP_LOGW(TAG, "ftp final reply %d: %.60s", rcode, line);
            }
        }
    }
    if (tls_write_line(ctrl, "QUIT") == ESP_OK) {
        (void)ftp_read_reply(ctrl, NULL, 0, NULL);
    }
    esp_tls_conn_destroy(ctrl);
    return result;
}

static bool upload_path_valid(const char *path)
{
    if(!path || !*path || *path=='/' || strlen(path)>=BAMBU_FTP_NAME_MAX)return false;
    const char *part=path;
    for(const unsigned char *p=(const unsigned char*)path;;++p){
        if(*p && (*p<32 || *p==127))return false;
        if(!*p || *p=='/'){
            size_t n=(const char*)p-part;
            if((n==1 && part[0]=='.') || (n==2 && part[0]=='.' && part[1]=='.'))return false;
            part=(const char*)p+1;
        }
        if(!*p)break;
    }
    return true;
}

esp_err_t bambu_ftp_upload_file(const bambu_lan_config_t *cfg,const char *local_path,
                               const char *remote_path,bambu_ftp_progress_cb_t progress,void *context)
{
    if(!cfg || !local_path || !upload_path_valid(remote_path))return ESP_ERR_INVALID_ARG;
    FILE *file=fopen(local_path,"rb");if(!file)return ESP_ERR_NOT_FOUND;
    if(fseek(file,0,SEEK_END) || ftell(file)<=0 || ftell(file)>0x7fffffffL){fclose(file);return ESP_ERR_INVALID_ARG;}
    uint32_t total=(uint32_t)ftell(file);rewind(file);
    esp_tls_t *ctrl=NULL,*data=NULL;esp_err_t err=ftp_ctrl_connect(cfg,&ctrl);
    if(err!=ESP_OK){fclose(file);return err;}
    char command[384],temp[256],reply[512];int code=0;bool created=false;
    if(ftp_session_setup(ctrl,cfg->access_code)!=ESP_OK){err=ESP_ERR_INVALID_RESPONSE;goto done;}
    snprintf(command,sizeof(command),"SIZE %s",remote_path);
    if(tls_write_line(ctrl,command)!=ESP_OK || ftp_read_reply(ctrl,reply,sizeof(reply),&code)!=ESP_OK){err=ESP_FAIL;goto done;}
    if(code==213){err=ESP_ERR_INVALID_STATE;goto done;}
    if(code!=550){err=ESP_ERR_NOT_SUPPORTED;goto done;}
    snprintf(temp,sizeof(temp),"%s.f101-%08lx.tmp",remote_path,(unsigned long)(uint32_t)esp_timer_get_time());
    if(progress && !progress(0,total,context)){err=ESP_ERR_INVALID_STATE;goto done;}
    err=ftp_open_data(ctrl,&data);if(err!=ESP_OK)goto done;
    snprintf(command,sizeof(command),"STOR %s",temp);
    if(tls_write_line(ctrl,command)!=ESP_OK || ftp_read_reply(ctrl,NULL,0,&code)!=ESP_OK || code/100!=1){err=ESP_FAIL;goto done;}
    created=true;uint8_t buffer[2048];uint32_t sent=0;
    while(sent<total){
        size_t want=total-sent;if(want>sizeof(buffer))want=sizeof(buffer);
        size_t count=fread(buffer,1,want,file);
        if(count!=want || esp_tls_conn_write(data,buffer,count)!=(ssize_t)count){err=ESP_FAIL;break;}
        sent+=(uint32_t)count;
        if(progress && !progress(sent,total,context)){err=ESP_ERR_INVALID_STATE;break;}
    }
    if(sent==total && err==ESP_OK && f101_tls_close_notify(data)!=0)err=ESP_FAIL;
    esp_tls_conn_destroy(data);data=NULL;
    if(err==ESP_OK && (ftp_read_reply(ctrl,NULL,0,&code)!=ESP_OK || (code!=226 && code!=250)))err=ESP_FAIL;
    if(err!=ESP_OK)goto done;
    /* Recheck before committing; a file created since the first SIZE is kept. */
    snprintf(command,sizeof(command),"SIZE %s",remote_path);
    if(tls_write_line(ctrl,command)!=ESP_OK || ftp_read_reply(ctrl,NULL,0,&code)!=ESP_OK){err=ESP_FAIL;goto done;}
    if(code!=550){err=ESP_ERR_INVALID_STATE;goto done;}
    snprintf(command,sizeof(command),"RNFR %s",temp);
    if(ftp_cmd(ctrl,command,350,350,NULL,0)!=ESP_OK){err=ESP_FAIL;goto done;}
    snprintf(command,sizeof(command),"RNTO %s",remote_path);
    if(ftp_cmd(ctrl,command,250,250,NULL,0)!=ESP_OK){err=ESP_FAIL;goto done;}
    created=false;err=ESP_OK;
done:
    if(data)esp_tls_conn_destroy(data);
    if(created){snprintf(command,sizeof(command),"DELE %s",temp);tls_write_line(ctrl,command);}
    if(ctrl)esp_tls_conn_destroy(ctrl);
    fclose(file);return err;
}
