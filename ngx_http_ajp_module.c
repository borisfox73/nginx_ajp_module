
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_ajp.h>
#include <ngx_http_ajp_module.h>
#include <ngx_http_ajp_handler.h>


static char *ngx_http_ajp_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_ajp_store(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

#if (NGX_HTTP_CACHE)
static char *ngx_http_ajp_cache(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_ajp_cache_key(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
#endif

static char *ngx_http_ajp_lowat_check(ngx_conf_t *cf, void *post,
    void *data);

static char *ngx_http_ajp_upstream_max_fails_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char *ngx_http_ajp_upstream_fail_timeout_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

#if (nginx_version >= 1007009)
static void *ngx_http_ajp_create_main_conf(ngx_conf_t *cf);
#endif
static void *ngx_http_ajp_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ajp_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_ajp_proxy_init_headers(ngx_conf_t *cf,
    ngx_http_ajp_loc_conf_t *conf,
    ngx_ajp_proxy_headers_t *headers, ngx_keyval_t *default_headers);

static ngx_int_t ngx_http_ajp_module_init_process(ngx_cycle_t *cycle);

static ngx_conf_post_t  ngx_http_ajp_lowat_post = { ngx_http_ajp_lowat_check };


static ngx_conf_bitmask_t  ngx_http_ajp_next_upstream_masks[] = {
    { ngx_string("error"),          NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"),        NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_header"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("http_500"),       NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { ngx_string("http_503"),       NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { ngx_string("http_404"),       NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("updating"),       NGX_HTTP_UPSTREAM_FT_UPDATING },
    { ngx_string("off"),            NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


static ngx_path_init_t  ngx_http_ajp_temp_path = {
    ngx_string(NGX_HTTP_AJP_TEMP_PATH), { 1, 2, 0 }
};


static ngx_str_t  ngx_http_ajp_hide_headers[] = {
    ngx_string("Status"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_null_string
};


// Default headers to always pass to upstream.
// Currently none.

static ngx_keyval_t  ngx_ajp_proxy_headers[] = {
/*
    { ngx_string("Host"), ngx_string("$host") },
    { ngx_string("Connection"), ngx_string("close") },
    { ngx_string("Content-Length"), ngx_string("$proxy_internal_body_length") },
    { ngx_string("Transfer-Encoding"), ngx_string("$proxy_internal_chunked") },
    { ngx_string("TE"), ngx_string("") },
    { ngx_string("Keep-Alive"), ngx_string("") },
    { ngx_string("Expect"), ngx_string("") },
    { ngx_string("Upgrade"), ngx_string("") },
*/
    { ngx_null_string, ngx_null_string }
};

#if (NGX_HTTP_CACHE)

static ngx_keyval_t  ngx_ajp_proxy_cache_headers[] = {
/*
    { ngx_string("Host"), ngx_string("$host") },
    { ngx_string("Connection"), ngx_string("close") },
    { ngx_string("Content-Length"), ngx_string("$proxy_internal_body_length") },
    { ngx_string("Transfer-Encoding"), ngx_string("$proxy_internal_chunked") },
    { ngx_string("TE"), ngx_string("") },
    { ngx_string("Keep-Alive"), ngx_string("") },
    { ngx_string("Expect"), ngx_string("") },
    { ngx_string("Upgrade"), ngx_string("") },
    // cache-specific
    { ngx_string("If-Modified-Since"),
      ngx_string("$upstream_cache_last_modified") },
    { ngx_string("If-Unmodified-Since"), ngx_string("") },
    { ngx_string("If-None-Match"), ngx_string("$upstream_cache_etag") },
    { ngx_string("If-Match"), ngx_string("") },
    { ngx_string("Range"), ngx_string("") },
    { ngx_string("If-Range"), ngx_string("") },
*/
    { ngx_null_string, ngx_null_string }
};


static ngx_str_t  ngx_http_ajp_hide_cache_headers[] = {
    ngx_string("Status"),
    ngx_string("X-Accel-Expires"),
    ngx_string("X-Accel-Redirect"),
    ngx_string("X-Accel-Limit-Rate"),
    ngx_string("X-Accel-Buffering"),
    ngx_string("X-Accel-Charset"),
    ngx_string("Set-Cookie"),
    ngx_string("P3P"),
    ngx_null_string
};

#endif


static ngx_command_t  ngx_http_ajp_commands[] = {

    { ngx_string("ajp_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE12,
      ngx_http_ajp_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ajp_secret"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, secret),
      NULL },

    { ngx_string("ajp_header_packet_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, ajp_header_packet_buffer_size_conf),
      NULL },

    { ngx_string("ajp_max_data_packet_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, max_ajp_data_packet_size_conf),
      NULL },

    { ngx_string("ajp_store"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_ajp_store,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ajp_store_access"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_conf_set_access_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.store_access),
      NULL },

    { ngx_string("ajp_ignore_client_abort"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.ignore_client_abort),
      NULL },

    { ngx_string("ajp_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("ajp_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("ajp_send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.send_lowat),
      &ngx_http_ajp_lowat_post },

    { ngx_string("ajp_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("ajp_pass_request_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.pass_request_headers),
      NULL },

    { ngx_string("ajp_pass_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.pass_request_body),
      NULL },

    { ngx_string("ajp_intercept_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.intercept_errors),
      NULL },

    { ngx_string("ajp_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("ajp_buffers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_bufs_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.bufs),
      NULL },

    { ngx_string("ajp_busy_buffers_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.busy_buffers_size_conf),
      NULL },

#if (NGX_HTTP_CACHE)

    { ngx_string("ajp_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_ajp_cache,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ajp_cache_key"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_ajp_cache_key,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ajp_cache_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_2MORE,
      ngx_http_file_cache_set_slot,
#if (nginx_version >= 1007009)
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_ajp_main_conf_t, caches),
#else
      0,
      0,
#endif
      &ngx_http_ajp_module },

    { ngx_string("ajp_cache_valid"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_file_cache_valid_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.cache_valid),
      NULL },

    { ngx_string("ajp_cache_min_uses"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.cache_min_uses),
      NULL },

    { ngx_string("ajp_script_url"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, script_url),
      NULL },

    { ngx_string("ajp_cache_use_stale"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.cache_use_stale),
      &ngx_http_ajp_next_upstream_masks },

    { ngx_string("ajp_cache_methods"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.cache_methods),
      &ngx_http_upstream_cache_method_mask },

    { ngx_string("ajp_cache_lock"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.cache_lock),
      NULL },

     { ngx_string("ajp_cache_lock_timeout"),
       NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
       ngx_conf_set_msec_slot,
       NGX_HTTP_LOC_CONF_OFFSET,
       offsetof(ngx_http_ajp_loc_conf_t, upstream.cache_lock_timeout),
       NULL },

#endif

    { ngx_string("ajp_temp_path"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
      ngx_conf_set_path_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.temp_path),
      NULL },

    { ngx_string("ajp_max_temp_file_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.max_temp_file_size_conf),
      NULL },

    { ngx_string("ajp_temp_file_write_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.temp_file_write_size_conf),
      NULL },

    { ngx_string("ajp_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.next_upstream),
      &ngx_http_ajp_next_upstream_masks },

    { ngx_string("ajp_upstream_max_fails"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_ajp_upstream_max_fails_unsupported,
      0,
      0,
      NULL },

    { ngx_string("ajp_upstream_fail_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_ajp_upstream_fail_timeout_unsupported,
      0,
      0,
      NULL },

    { ngx_string("ajp_pass_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.pass_headers),
      NULL },

    { ngx_string("ajp_hide_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_str_array_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.hide_headers),
      NULL },
/*
    { ngx_string("ajp_param"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_upstream_param_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, params_source),
      NULL },
*/
    { ngx_string("ajp_ignore_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, upstream.ignore_headers),
      &ngx_http_upstream_ignore_headers_masks },

    { ngx_string("ajp_keep_conn"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, keep_conn),
      NULL },

    { ngx_string("ajp_set_header"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
      ngx_conf_set_keyval_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, headers_source),
      NULL },

    { ngx_string("ajp_headers_hash_max_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, headers_hash_max_size),
      NULL },

    { ngx_string("ajp_headers_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ajp_loc_conf_t, headers_hash_bucket_size),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_ajp_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

#if (nginx_version >= 1007009)
    ngx_http_ajp_create_main_conf,         /* create main configuration */
#else
    NULL,                                  /* create main configuration */
#endif
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_ajp_create_loc_conf,          /* create location configuration */
    ngx_http_ajp_merge_loc_conf            /* merge location configuration */
};


ngx_module_t  ngx_http_ajp_module = {
    NGX_MODULE_V1,
    &ngx_http_ajp_module_ctx,              /* module context */
    ngx_http_ajp_commands,                 /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_ajp_module_init_process,      /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_http_ajp_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ajp_loc_conf_t    *alcf = conf;

    size_t                      add;
    u_short                     port;
    ngx_url_t                   u;
    ngx_str_t                  *value, *url;
    ngx_uint_t                  n;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_script_compile_t   sc;

    if (alcf->upstream.upstream || alcf->ajp_lengths) {
        return "is duplicate";
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_ajp_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    value = cf->args->elts;

    url = &value[1];

    n = ngx_http_script_variables_count(url);

    if (n) {

        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = url;
        sc.lengths = &alcf->ajp_lengths;
        sc.values = &alcf->ajp_values;
        sc.variables = n;
        sc.complete_lengths = 1;
        sc.complete_values = 1;

        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
        if( cf->args->nelts>2 ) {
            alcf->secret = value[2];
        }

        return NGX_CONF_OK;
    }

    add = port = 0;
    if (ngx_strncasecmp(url->data, (u_char *) "ajp://", 6) == 0) {
        add = 6;
        port = 8009;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url.len = url->len - add;
    u.url.data = url->data + add;
    u.default_port = port;
    u.uri_part = 1;
    u.no_resolve = 1;

    alcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (alcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }
    if( cf->args->nelts>2 ) {
         alcf->secret = value[2];
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_ajp_store(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t                  *value;
    ngx_http_ajp_loc_conf_t    *alcf = conf;
    ngx_http_script_compile_t   sc;

    if ((alcf->upstream.store != NGX_CONF_UNSET) ||
        alcf->upstream.store_lengths)
    {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {
        alcf->upstream.store = 0;
        return NGX_CONF_OK;
    }

#if (NGX_HTTP_CACHE)

#if (nginx_version >= 1007009)
    if (alcf->upstream.cache > 0)
#else
    if (alcf->upstream.cache != NGX_CONF_UNSET_PTR
        && alcf->upstream.cache != NULL)
#endif
    {
        return "is incompatible with \"ajp_cache\"";
    }

#endif

    if (ngx_strcmp(value[1].data, "on") == 0) {
        alcf->upstream.store = 1;
        return NGX_CONF_OK;
    }

    /* include the terminating '\0' into script */
    value[1].len++;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = &value[1];
    sc.lengths = &alcf->upstream.store_lengths;
    sc.values = &alcf->upstream.store_values;
    sc.variables = ngx_http_script_variables_count(&value[1]);
    sc.complete_lengths = 1;
    sc.complete_values = 1;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_ajp_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;

    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"ajp_send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"ajp_send_lowat\" is not supported, ignored");

    *np = 0;
#endif

    return NGX_CONF_OK;
}


#if (NGX_HTTP_CACHE)

static char *
ngx_http_ajp_cache(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ajp_loc_conf_t *alcf = conf;

    ngx_str_t  *value;
#if (nginx_version >= 1007009)
    ngx_http_complex_value_t           cv;
    ngx_http_compile_complex_value_t   ccv;
#endif

    value = cf->args->elts;

#if (nginx_version >= 1007009)
    if (alcf->upstream.cache != NGX_CONF_UNSET) {
#else
    if (alcf->upstream.cache != NGX_CONF_UNSET_PTR) {
#endif
        return "is duplicate";
    }

    if (ngx_strcmp(value[1].data, "off") == 0) {
#if (nginx_version >= 1007009)
        alcf->upstream.cache = 0;
#else
        alcf->upstream.cache = NULL;
#endif
        return NGX_CONF_OK;
    }

    if (alcf->upstream.store > 0 || alcf->upstream.store_lengths) {
        return "is incompatible with \"ajp_store\"";
    }

#if (nginx_version >= 1007009)
    alcf->upstream.cache = 1;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &cv;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (cv.lengths != NULL) {

        alcf->upstream.cache_value = ngx_palloc(cf->pool,
                                             sizeof(ngx_http_complex_value_t));
        if (alcf->upstream.cache_value == NULL) {
            return NGX_CONF_ERROR;
        }

        *alcf->upstream.cache_value = cv;

        return NGX_CONF_OK;
    }

    alcf->upstream.cache_zone = ngx_shared_memory_add(cf, &value[1], 0,
                                                      &ngx_http_ajp_module);
    if (alcf->upstream.cache_zone == NULL) {
        return NGX_CONF_ERROR;
    }
#else
    alcf->upstream.cache = ngx_shared_memory_add(cf, &value[1], 0,
                                                 &ngx_http_ajp_module);
    if (alcf->upstream.cache == NULL) {
        return NGX_CONF_ERROR;
    }
#endif

    return NGX_CONF_OK;
}


static char *
ngx_http_ajp_cache_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ajp_loc_conf_t *alcf = conf;

    ngx_str_t                         *value;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (alcf->cache_key.value.len) {
        return "is duplicate";
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = &alcf->cache_key;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

#endif


static char *
ngx_http_ajp_upstream_max_fails_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
         "\"ajp_upstream_max_fails\" is not supported, "
         "use the \"max_fails\" parameter of the \"server\" directive ",
         "inside the \"upstream\" block");

    return NGX_CONF_ERROR;
}


static char *
ngx_http_ajp_upstream_fail_timeout_unsupported(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf)
{
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
         "\"ajp_upstream_fail_timeout\" is not supported, "
         "use the \"fail_timeout\" parameter of the \"server\" directive ",
         "inside the \"upstream\" block");

    return NGX_CONF_ERROR;
}


#if (nginx_version >= 1007009)
static void *
ngx_http_ajp_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_ajp_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ajp_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

#if (NGX_HTTP_CACHE)
    if (ngx_array_init(&conf->caches, cf->pool, 4,
                       sizeof(ngx_http_file_cache_t *))
        != NGX_OK)
    {  
        return NULL;
    }
#endif

    return conf;
}
#endif


static void *
ngx_http_ajp_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_ajp_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ajp_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.ignore_headers = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.cache_use_stale = 0;
     *     conf->upstream.cache_methods = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.hide_headers_hash = { NULL, 0 };
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     *     conf->upstream.store_lengths = NULL;
     *     conf->upstream.store_values = NULL;
     *
     */

    conf->ajp_header_packet_buffer_size_conf = NGX_CONF_UNSET_SIZE;
    conf->max_ajp_data_packet_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream.store = NGX_CONF_UNSET;
    conf->upstream.store_access = NGX_CONF_UNSET_UINT;
    conf->upstream.buffering = NGX_CONF_UNSET;
    conf->upstream.ignore_client_abort = NGX_CONF_UNSET;

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.send_lowat = NGX_CONF_UNSET_SIZE;
    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    conf->upstream.busy_buffers_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.max_temp_file_size_conf = NGX_CONF_UNSET_SIZE;
    conf->upstream.temp_file_write_size_conf = NGX_CONF_UNSET_SIZE;

    conf->upstream.pass_request_headers = NGX_CONF_UNSET;
    conf->upstream.pass_request_body = NGX_CONF_UNSET;

#if (NGX_HTTP_CACHE)
#if (nginx_version >= 1007009)
    conf->upstream.cache = NGX_CONF_UNSET;
#else
    conf->upstream.cache = NGX_CONF_UNSET_PTR;
#endif
    conf->upstream.cache_min_uses = NGX_CONF_UNSET_UINT;
    conf->upstream.cache_valid = NGX_CONF_UNSET_PTR;
    conf->upstream.cache_lock = NGX_CONF_UNSET;
    conf->upstream.cache_lock_timeout = NGX_CONF_UNSET_MSEC;
#endif

    conf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
    conf->upstream.pass_headers = NGX_CONF_UNSET_PTR;

    conf->upstream.intercept_errors = NGX_CONF_UNSET;

    /* "ajp_cyclic_temp_file" is disabled */
    conf->upstream.cyclic_temp_file = 0;

    conf->keep_conn = NGX_CONF_UNSET;

    conf->headers_source = NGX_CONF_UNSET_PTR;
    conf->headers_hash_max_size = NGX_CONF_UNSET_UINT;
    conf->headers_hash_bucket_size = NGX_CONF_UNSET_UINT;

    ngx_str_set(&conf->upstream.module, "ajp");

    return conf;
}


static char *
ngx_http_ajp_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ajp_loc_conf_t *prev = parent;
    ngx_http_ajp_loc_conf_t *conf = child;

//<<<<<<< HEAD
    size_t                        size;
    ngx_str_t                    *h;
    ngx_hash_init_t               hash;
    ngx_http_script_compile_t     sc;
    ngx_uint_t n;
//=======
//    size_t            size;
//    ngx_str_t        *h;
//    ngx_hash_init_t   hash;

    if (conf->secret.data == NULL){
      conf->secret = prev->secret;
    }

#if (NGX_HTTP_CACHE) && (nginx_version >= 1007009)

    if (conf->upstream.store > 0) {
        conf->upstream.cache = 0;
    }

    if (conf->upstream.cache > 0) {
        conf->upstream.store = 0;
    }

#endif
//>>>>>>> upstream/master

    if (conf->upstream.store != 0) {
        ngx_conf_merge_value(conf->upstream.store,
                             prev->upstream.store, 0);

        if (conf->upstream.store_lengths == NULL) {
            conf->upstream.store_lengths = prev->upstream.store_lengths;
            conf->upstream.store_values = prev->upstream.store_values;
        }
    }

    ngx_conf_merge_size_value(conf->ajp_header_packet_buffer_size_conf,
                              prev->ajp_header_packet_buffer_size_conf,
                              (size_t) AJP_MSG_BUFFER_SZ);

    ngx_conf_merge_size_value(conf->max_ajp_data_packet_size_conf,
                              prev->max_ajp_data_packet_size_conf,
                              (size_t) AJP_MSG_BUFFER_SZ);

    ngx_conf_merge_uint_value(conf->upstream.store_access,
                              prev->upstream.store_access, 0600);

    ngx_conf_merge_value(conf->upstream.buffering,
                         prev->upstream.buffering, 1);

    ngx_conf_merge_value(conf->upstream.ignore_client_abort,
                         prev->upstream.ignore_client_abort, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.send_lowat,
                              prev->upstream.send_lowat, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bufs_value(conf->upstream.bufs, prev->upstream.bufs,
                              8, ngx_pagesize);

    if (conf->upstream.bufs.num < 2) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "there must be at least 2 \"ajp_buffers\"");
        return NGX_CONF_ERROR;
    }

    if (conf->ajp_header_packet_buffer_size_conf > AJP_MAX_BUFFER_SZ) {
        conf->ajp_header_packet_buffer_size_conf = AJP_MAX_BUFFER_SZ;
    }

    if(conf->max_ajp_data_packet_size_conf < AJP_MSG_BUFFER_SZ) {
        conf->max_ajp_data_packet_size_conf = AJP_MSG_BUFFER_SZ;
    } else if(conf->max_ajp_data_packet_size_conf > AJP_MAX_BUFFER_SZ ) {
        conf->max_ajp_data_packet_size_conf = AJP_MAX_BUFFER_SZ;
    }

    size = conf->upstream.buffer_size;
    if (size < conf->upstream.bufs.size) {
        size = conf->upstream.bufs.size;
    }

    ngx_conf_merge_size_value(conf->upstream.busy_buffers_size_conf,
                              prev->upstream.busy_buffers_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.busy_buffers_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.busy_buffers_size = 2 * size;
    } else {
        conf->upstream.busy_buffers_size =
                                         conf->upstream.busy_buffers_size_conf;
    }

    if (conf->upstream.busy_buffers_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"ajp_busy_buffers_size\" must be equal or bigger than "
             "maximum of the value of \"ajp_buffer_size\" and "
             "one of the \"ajp_buffers\"");

        return NGX_CONF_ERROR;
    }

    if (conf->upstream.busy_buffers_size
        > (conf->upstream.bufs.num - 1) * conf->upstream.bufs.size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"ajp_busy_buffers_size\" must be less than "
             "the size of all \"ajp_buffers\" minus one buffer");

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_size_value(conf->upstream.temp_file_write_size_conf,
                              prev->upstream.temp_file_write_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.temp_file_write_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.temp_file_write_size = 2 * size;
    } else {
        conf->upstream.temp_file_write_size =
                                      conf->upstream.temp_file_write_size_conf;
    }

    if (conf->upstream.temp_file_write_size < size) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"ajp_temp_file_write_size\" must be equal or bigger than "
             "maximum of the value of \"ajp_buffer_size\" and "
             "one of the \"ajp_buffers\"");

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_size_value(conf->upstream.max_temp_file_size_conf,
                              prev->upstream.max_temp_file_size_conf,
                              NGX_CONF_UNSET_SIZE);

    if (conf->upstream.max_temp_file_size_conf == NGX_CONF_UNSET_SIZE) {
        conf->upstream.max_temp_file_size = 1024 * 1024 * 1024;
    } else {
        conf->upstream.max_temp_file_size =
                                        conf->upstream.max_temp_file_size_conf;
    }

    if (conf->upstream.max_temp_file_size != 0
        && conf->upstream.max_temp_file_size < size)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
             "\"ajp_max_temp_file_size\" must be equal to zero to disable "
             "the temporary files usage or must be equal or bigger than "
             "maximum of the value of \"ajp_buffer_size\" and "
             "one of the \"ajp_buffers\"");

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_bitmask_value(conf->upstream.ignore_headers,
                              prev->upstream.ignore_headers,
                              NGX_CONF_BITMASK_SET);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (ngx_conf_merge_path_value(cf, &conf->upstream.temp_path,
                              prev->upstream.temp_path,
                              &ngx_http_ajp_temp_path)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

#if (NGX_HTTP_CACHE)

#if (nginx_version >= 1007009)
    if (conf->upstream.cache == NGX_CONF_UNSET) {
        ngx_conf_merge_value(conf->upstream.cache,
                              prev->upstream.cache, 0);

        conf->upstream.cache_zone = prev->upstream.cache_zone;
        conf->upstream.cache_value = prev->upstream.cache_value;
    }

    if (conf->upstream.cache_zone && conf->upstream.cache_zone->data == NULL) {
        ngx_shm_zone_t  *shm_zone;

        shm_zone = conf->upstream.cache_zone;
#else
    ngx_conf_merge_ptr_value(conf->upstream.cache,
                             prev->upstream.cache, NULL);

    if (conf->upstream.cache && conf->upstream.cache->data == NULL) {
        ngx_shm_zone_t  *shm_zone;

        shm_zone = conf->upstream.cache;
#endif

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"ajp_cache\" zone \"%V\" is unknown, "
                           "Maybe you haven't set the ajp_cache_path",
                           &shm_zone->shm.name);

        return NGX_CONF_ERROR;
    }

    ngx_conf_merge_uint_value(conf->upstream.cache_min_uses,
                              prev->upstream.cache_min_uses, 1);

    ngx_conf_merge_bitmask_value(conf->upstream.cache_use_stale,
                              prev->upstream.cache_use_stale,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_OFF));

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.cache_use_stale = NGX_CONF_BITMASK_SET
                                         |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.cache_use_stale & NGX_HTTP_UPSTREAM_FT_ERROR) {
        conf->upstream.cache_use_stale |= NGX_HTTP_UPSTREAM_FT_NOLIVE;
    }

    if (conf->upstream.cache_methods == 0) {
        conf->upstream.cache_methods = prev->upstream.cache_methods;
    }

    conf->upstream.cache_methods |= NGX_HTTP_GET|NGX_HTTP_HEAD;

    ngx_conf_merge_ptr_value(conf->upstream.cache_valid,
                             prev->upstream.cache_valid, NULL);

    if (conf->cache_key.value.data == NULL) {
        conf->cache_key = prev->cache_key;
    }

    if (conf->upstream.cache && conf->cache_key.value.data == NULL) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "no \"fastcgi_cache_key\" for \"fastcgi_cache\"");
    }

    ngx_conf_merge_value(conf->upstream.cache_lock,
                         prev->upstream.cache_lock, 0);
    
    ngx_conf_merge_str_value(conf->script_url,
                         prev->script_url, "");

    ngx_conf_merge_msec_value(conf->upstream.cache_lock_timeout,
                              prev->upstream.cache_lock_timeout, 5000);

#endif

    ngx_conf_merge_value(conf->upstream.pass_request_headers,
                         prev->upstream.pass_request_headers, 1);
    ngx_conf_merge_value(conf->upstream.pass_request_body,
                         prev->upstream.pass_request_body, 1);

    ngx_conf_merge_value(conf->upstream.intercept_errors,
                         prev->upstream.intercept_errors, 0);

    ngx_conf_merge_value(conf->keep_conn, prev->keep_conn, 0);


    /*if (ngx_http_ajp_merge_params(cf, parent, child) != NGX_OK) {
        return NGX_CONF_ERROR;
    }*/


    ngx_conf_merge_uint_value(conf->headers_hash_max_size,
                              prev->headers_hash_max_size, 512);

    ngx_conf_merge_uint_value(conf->headers_hash_bucket_size,
                              prev->headers_hash_bucket_size, 64);

    conf->headers_hash_bucket_size = ngx_align(conf->headers_hash_bucket_size,
                                               ngx_cacheline_size);

    ngx_conf_merge_ptr_value(conf->headers_source, prev->headers_source, NULL);

    if (conf->headers_source == prev->headers_source) {
        conf->headers = prev->headers;
#if (NGX_HTTP_CACHE)
        conf->headers_cache = prev->headers_cache;
#endif
    }

    if (ngx_ajp_proxy_init_headers(cf, conf, &conf->headers,
                                   ngx_ajp_proxy_headers) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

#if (NGX_HTTP_CACHE)
    if (conf->upstream.cache
        && ngx_ajp_proxy_init_headers(cf, conf, &conf->headers_cache,
                                      ngx_ajp_proxy_cache_headers) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
#endif

    /*
     * special handling to preserve conf->headers in the "http" section
     * to inherit it to all servers
     */

    if (prev->headers.hash.buckets == NULL
        && conf->headers_source == prev->headers_source) {
        prev->headers = conf->headers;
#if (NGX_HTTP_CACHE)
        prev->headers_cache = conf->headers_cache;
#endif
    }


    hash.max_size = conf->headers_hash_max_size;
    hash.bucket_size = ngx_align(conf->headers_hash_bucket_size, ngx_cacheline_size);
    hash.name = "ajp_hide_headers_hash";

#if (NGX_HTTP_CACHE)

    h = conf->upstream.cache ? ngx_http_ajp_hide_cache_headers:
                               ngx_http_ajp_hide_headers;
#else

    h = ngx_http_ajp_hide_headers;

#endif

    if (ngx_http_upstream_hide_headers_hash(cf, &conf->upstream,
                                            &prev->upstream, h, &hash)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    if (conf->ajp_lengths == NULL) {
        conf->ajp_lengths = prev->ajp_lengths;
        conf->ajp_values = prev->ajp_values;
    }

//<<<<<<< HEAD
    if (conf->param_lengths == NULL) {
        conf->param_lengths = prev->param_lengths;
        conf->param_values = prev->param_values;
    }

    if (conf->script_url.len > 0) {
        /*ngx_http_script_copy_code_t  *copy;
        copy = ngx_array_push_n(conf->ajp_lengths,
                                sizeof(ngx_http_script_copy_code_t));*/
        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
        n = ngx_http_script_variables_count(&conf->script_url);
        sc.cf = cf;
        sc.source = &conf->script_url;
	    sc.lengths = &conf->param_lengths;
        sc.values = &conf->param_values;
        sc.variables = n;
        sc.complete_lengths = 1;
        sc.complete_values = 1;
        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

//=======
//>>>>>>> upstream/master
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_ajp_proxy_init_headers(ngx_conf_t *cf, ngx_http_ajp_loc_conf_t *conf,
    ngx_ajp_proxy_headers_t *headers, ngx_keyval_t *default_headers)
{
    u_char                       *p;
    size_t                        size;
    uintptr_t                    *code;
    ngx_uint_t                    i;
    ngx_array_t                   headers_names, headers_merged;
    ngx_keyval_t                 *src, *s, *h;
    ngx_hash_key_t               *hk;
    ngx_hash_init_t               hash;
    ngx_http_script_compile_t     sc;
    ngx_http_script_copy_code_t  *copy;

    if (headers->hash.buckets) {
        return NGX_OK;
    }

    if (ngx_array_init(&headers_names, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK) {
        return NGX_ERROR;
    }

    if (ngx_array_init(&headers_merged, cf->temp_pool, 4, sizeof(ngx_keyval_t))
        != NGX_OK) {
        return NGX_ERROR;
    }

    headers->lengths = ngx_array_create(cf->pool, 64, 1);
    if (headers->lengths == NULL) {
        return NGX_ERROR;
    }

    headers->values = ngx_array_create(cf->pool, 512, 1);
    if (headers->values == NULL) {
        return NGX_ERROR;
    }

    if (conf->headers_source) {

        src = conf->headers_source->elts;
        for (i = 0; i < conf->headers_source->nelts; i++) {

            s = ngx_array_push(&headers_merged);
            if (s == NULL) {
                return NGX_ERROR;
            }

            *s = src[i];
        }
    }

    h = default_headers;

    while (h->key.len) {

        src = headers_merged.elts;
        for (i = 0; i < headers_merged.nelts; i++) {
            if (ngx_strcasecmp(h->key.data, src[i].key.data) == 0) {
                goto next;
            }
        }

        s = ngx_array_push(&headers_merged);
        if (s == NULL) {
            return NGX_ERROR;
        }

        *s = *h;

    next:

        h++;
    }

    src = headers_merged.elts;
    for (i = 0; i < headers_merged.nelts; i++) {

        hk = ngx_array_push(&headers_names);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = src[i].key;
        hk->key_hash = ngx_hash_key_lc(src[i].key.data, src[i].key.len);
        hk->value = (void *) 1;

        if (src[i].value.len == 0) {
            continue;
        }

        copy = ngx_array_push_n(headers->lengths,
                                sizeof(ngx_http_script_copy_code_t));
        if (copy == NULL) {
            return NGX_ERROR;
        }

        copy->code = (ngx_http_script_code_pt) (void *)
                                                 ngx_http_script_copy_len_code;
        copy->len = src[i].key.len;

        size = (sizeof(ngx_http_script_copy_code_t)
                + src[i].key.len + sizeof(uintptr_t) - 1)
               & ~(sizeof(uintptr_t) - 1);

        copy = ngx_array_push_n(headers->values, size);
        if (copy == NULL) {
            return NGX_ERROR;
        }

        copy->code = ngx_http_script_copy_code;
        copy->len = src[i].key.len;

        p = (u_char *) copy + sizeof(ngx_http_script_copy_code_t);
        ngx_memcpy(p, src[i].key.data, src[i].key.len);

        ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

        sc.cf = cf;
        sc.source = &src[i].value;
        sc.flushes = &headers->flushes;
        sc.lengths = &headers->lengths;
        sc.values = &headers->values;

        if (ngx_http_script_compile(&sc) != NGX_OK) {
            return NGX_ERROR;
        }

        code = ngx_array_push_n(headers->lengths, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;

        code = ngx_array_push_n(headers->values, sizeof(uintptr_t));
        if (code == NULL) {
            return NGX_ERROR;
        }

        *code = (uintptr_t) NULL;
    }

    code = ngx_array_push_n(headers->lengths, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_ERROR;
    }

    *code = (uintptr_t) NULL;

    hash.hash = &headers->hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = conf->headers_hash_max_size;
    hash.bucket_size = conf->headers_hash_bucket_size;
    hash.name = "ajp_proxy_headers_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    return ngx_hash_init(&hash, headers_names.elts, headers_names.nelts);
}


// STUB. TODO: implement correct parameters merging. See ngx_http_fastcgi_merge_params() ($NGINX_SOURCE_DIR/src/http/modules/ngx_http_fastcgi_module.c:2428)
/*
static ngx_int_t
ngx_http_ajp_merge_params(ngx_conf_t *cf, void *parent, void *child) {
    
    if (child->params_source == NULL) {
        child->params_source = parent->params_source;
	child->params = parent->params;
	child->params_len parent->params_len;
	return NGX_OK;
    }

    

    return NGX_OK;
}
*/

static ngx_int_t ngx_http_ajp_module_init_process(ngx_cycle_t *cycle)
{
    ajp_header_init();

    return NGX_OK;
//        8c:17:5e:7b
}
