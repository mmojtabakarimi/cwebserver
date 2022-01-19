#ifndef __H_KORE_ASSETS_H
#define __H_KORE_ASSETS_H
extern const u_int8_t asset_builtin_kore_conf[];
extern const u_int32_t asset_len_builtin_kore_conf;
extern const time_t asset_mtime_builtin_kore_conf;
extern const char *asset_sha256_builtin_kore_conf;
int asset_serve_builtin_kore_conf(struct http_request *);

#endif
