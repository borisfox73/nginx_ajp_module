
#ifndef _NGX_AJP_MODULE_H_INCLUDED_
#define _NGX_AJP_MODULE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if (nginx_version >= 1007009)
typedef struct {
    ngx_array_t                caches;  /* ngx_http_file_cache_t * */
} ngx_http_ajp_main_conf_t;
#endif

typedef struct {
    ngx_array_t                   *flushes;
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_hash_t                     hash;
} ngx_ajp_proxy_headers_t;

typedef struct {
    ngx_http_upstream_conf_t   upstream;

    size_t                     ajp_header_packet_buffer_size_conf;
    size_t                     max_ajp_data_packet_size_conf;

    ngx_array_t                   *ajp_lengths;
    ngx_array_t                   *ajp_values;
    ngx_array_t                   *param_lengths;
    ngx_array_t                   *param_values;
    ngx_str_t			          script_url;
    ngx_flag_t                     keep_conn;

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t   cache_key;
#endif

    ngx_str_t		       secret;

    ngx_ajp_proxy_headers_t        headers;
#if (NGX_HTTP_CACHE)
    ngx_ajp_proxy_headers_t        headers_cache;
#endif
    ngx_array_t                   *headers_source;
    ngx_uint_t                     headers_hash_max_size;
    ngx_uint_t                     headers_hash_bucket_size;

} ngx_http_ajp_loc_conf_t;


extern ngx_module_t  ngx_http_ajp_module;


#endif /* _NGX_AJP_MODULE_H_INCLUDED_ */
