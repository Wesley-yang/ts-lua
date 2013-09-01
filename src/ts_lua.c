

#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "ts_lua_util.h"

#define TS_LUA_MAX_SCRIPT_FNAME_LENGTH          1024
#define TS_LUA_FUNCTION_REMAP                   "do_remap"
#define TS_LUA_FUNCTION_CACHE_LOOKUP_COMPLETE   "do_cache_lookup_complete"
#define TS_LUA_FUNCTION_SEND_REQUEST            "do_send_request"
#define TS_LUA_FUNCTION_READ_RESPONSE           "do_read_response"
#define TS_LUA_FUNCTION_SEND_RESPONSE           "do_send_response"


typedef struct {
    pthread_key_t lua_state_key;
    char script_file[TS_LUA_MAX_SCRIPT_FNAME_LENGTH];
} ts_lua_conf;


lua_State *
ts_lua_get_thread_vm(ts_lua_conf *lua_conf)
{
    int         ret;
    lua_State   *l = (lua_State*)pthread_getspecific(lua_conf->lua_state_key);

    if (l == NULL) {
        l = ts_lua_new_state();

        ret = luaL_loadfile(l, lua_conf->script_file);
        if (ret) {
            fprintf(stderr, "luaL_loadfile failed: %s\n", lua_tostring(l, -1));
        }

        ret = lua_pcall(l, 0, 0, 0);
        if (ret) {
            fprintf(stderr, "lua_pcall failed: %s\n", lua_tostring(l, -1));
        }

        pthread_setspecific(lua_conf->lua_state_key, l);
    }

    return l;
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
    if (!api_info || api_info->size < sizeof(TSRemapInterface))
        return TS_ERROR;

    return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** ih, char* errbuf, int errbuf_size)
{
    if (argc < 3) {
        TSError("[%s] lua script file required !!", __FUNCTION__);
        return TS_ERROR;
    }

    ts_lua_conf *conf = TSmalloc(sizeof(ts_lua_conf));
    if (!conf) {
        TSError("[%s] TSmalloc failed !!", __FUNCTION__);
        return TS_ERROR;
    }

    int status = pthread_key_create(&conf->lua_state_key, NULL);
    if (status) {
        TSError("[%s] pthread_key_create Error: %s", __FUNCTION__, strerror(errno));
        TSfree(conf);
        return TS_ERROR;
    }

    sprintf(conf->script_file, "%s", argv[2]);

    *ih = conf;

    return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void* ih)
{
    TSfree(ih);
    return;
}

TSRemapStatus
TSRemapDoRemap(void* ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
    int             ret, match;
    lua_State       *lua_thread;
    lua_State       *l;
    ts_lua_conf     *lua_conf;
    ts_lua_ctx      *ctx;

    lua_conf = (ts_lua_conf*)ih;
    lua_thread = ts_lua_get_thread_vm(lua_conf);

    ctx = TSmalloc(sizeof(ts_lua_ctx));
    memset(ctx, 0, sizeof(ts_lua_ctx));

    ctx->lua = lua_newthread(lua_thread);
    ctx->txnp = rh;
    ctx->client_request_bufp = rri->requestBufp;
    ctx->client_request_hdrp = rri->requestHdrp;

    ts_lua_set_ctx(ctx->lua, ctx);

    l = ctx->lua;

    lua_getglobal(l, TS_LUA_FUNCTION_REMAP);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        return TSREMAP_NO_REMAP;
    }

    ret = lua_pcall(l, 0, 1, 0);
    if (ret) {
        fprintf(stderr, "lua_pcall failed: %s\n", lua_tostring(l, -1));
    }

    match = lua_tointeger(l, -1);
    lua_pop(l, 1);

    if (match) {
        TSDebug("ts_lua", "header matches :)");
    }

    return TSREMAP_NO_REMAP;
}

