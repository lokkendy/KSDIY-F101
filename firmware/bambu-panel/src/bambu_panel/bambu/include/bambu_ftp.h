#pragma once

#include "bambu_service.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_BAMBU_FTP_LIST_MAX
#define CONFIG_BAMBU_FTP_LIST_MAX 64
#endif

/* 拓竹 Studio 多零件项目名 = 各对象名 " + " 连接，UTF-8 可破百字节；曾 96
 * 截断致 ".3mf" 后缀丢失 → 归档误走 gcode_file → 打印机 fail */
#define BAMBU_FTP_NAME_MAX 192

/** FTPS + mbedTLS 帧较深；8192 在 fetch_cover/日志路径上易栈保护 fault */
#define BAMBU_FTP_TASK_STACK 16384

typedef struct {
    /** 相对 FTP 根路径，如 `foo.gcode.3mf`、`cache/foo.gcode.3mf` */
    char name[BAMBU_FTP_NAME_MAX];
    uint32_t size_bytes;
    bool directory;
} bambu_ftp_entry_t;

/** 打印机 FTPS 目录（OpenBambuAPI implicit TLS :990） */
typedef enum {
    /** 扫描 `/` + `/cache` + `/model`（拓竹 SD 上文件多在 cache） */
    BAMBU_FTP_SCOPE_SDCARD = 0,
    BAMBU_FTP_SCOPE_CACHE, /**< 仅 /cache */
} bambu_ftp_scope_t;

/**
 * 列出可打印文件（*.gcode / *.3mf / *.gcode.3mf）。
 * @param out_max out 数组容量
 * @param out_count 实际条数
 */
/* Check the relative path and candidate suffix. The printer validates file contents. */
bool bambu_ftp_is_printable_path(const char *path);

/* Read-only listing of one relative printer directory; empty path means root. */
esp_err_t bambu_ftp_list_directory(const bambu_lan_config_t *cfg, const char *path,
                                  bambu_ftp_entry_t *out, size_t out_max, size_t *out_count);

esp_err_t bambu_ftp_list_files(const bambu_lan_config_t *cfg, bambu_ftp_scope_t scope,
                               bambu_ftp_entry_t *out, size_t out_max, size_t *out_count);

/**
 * 删除打印机上的文件（DELE）。path 为 list_files 返回的相对路径
 * （如 `foo.gcode.3mf`、`cache/foo.gcode.3mf`）。
 */
esp_err_t bambu_ftp_delete_file(const bambu_lan_config_t *cfg, const char *path);

/**
 * 拉取 3mf 内嵌模型封面（Metadata/plate_<N>[_small].png）。
 * Bambu Studio 写 3mf 时 PNG 条目均为 STORED 且位于文件头部（plate_1.png
 * 固定在第二个条目、偏移 ~286），故只需流式读文件头顺序解析 ZIP local
 * header，命中即断开——单次传输 ≈ 头部 + 封面字节（small ≈ 4KB）。
 * @param rel_path 相对 FTP 根（如 `cache/foo.gcode.3mf`）
 * @param plate_idx 0 = 取任意 plate 封面；>0 = 精确匹配 plate_<idx>
 * @param out/out_cap 调用方缓冲（建议 ≥ 192KB，容纳大 plate 封面）
 * @param out_len PNG 实际字节数
 * 返回 ESP_OK / ESP_ERR_NOT_FOUND（非 3mf / 无封面 / 条目超预算）/ 其他
 */
esp_err_t bambu_ftp_fetch_cover(const bambu_lan_config_t *cfg, const char *rel_path,
                                int plate_idx, uint8_t *out, size_t out_cap, size_t *out_len);

/** Return false to cancel. Called on the upload worker, never the LVGL task. */
typedef bool (*bambu_ftp_progress_cb_t)(uint32_t sent,uint32_t total,void *context);
/** Upload to a temporary remote file then rename; never overwrite an existing target. */
esp_err_t bambu_ftp_upload_file(const bambu_lan_config_t *cfg,const char *local_path,
                               const char *remote_path,bambu_ftp_progress_cb_t progress,void *context);

#ifdef __cplusplus
}
#endif
