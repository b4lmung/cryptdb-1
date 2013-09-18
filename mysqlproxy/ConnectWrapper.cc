#include <sstream>
#include <fstream>
#include <assert.h>
#include <lua5.1/lua.hpp>

#include <util/ctr.hh>
#include <util/cryptdb_log.hh>
#include <util/scoped_lock.hh>

#include <main/rewrite_main.hh>
#include <main/rewrite_util.hh>
#include <parser/sql_utils.hh>

// FIXME: Ownership semantics.
class WrapperState {
    WrapperState(const WrapperState &other);
    WrapperState &operator=(const WrapperState &rhs);

public:
    WrapperState() {;}
    std::string last_query;
    std::ofstream * PLAIN_LOG;
    ReturnMeta rmeta;
    std::string cur_db;
    QueryRewrite * qr;
};

static Timer t;

//static EDBProxy * cl = NULL;
static ProxyState * ps = NULL;
static pthread_mutex_t big_lock;

static bool EXECUTE_QUERIES = true;

static std::string TRAIN_QUERY ="";

static bool LOG_PLAIN_QUERIES = false;
static std::string PLAIN_BASELOG = "";


static int counter = 0;

static std::map<std::string, WrapperState*> clients;

static int
returnResultSet(lua_State *L, const ResType &res);

static Item *
make_item_by_type(std::string value, enum_field_types type)
{
    Item * i;

    switch(type) {
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_TINY:
        i = new Item_int((long long) valFromStr(value));
        break;

    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_TIMESTAMP:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_NEWDATE:
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATETIME:
        i = new Item_string(make_thd_string(value), value.length(), &my_charset_bin);
        break;

    default:
        thrower() << "unknown data type " << type;
    }
    return i;
}

static Item_null *
make_null(const std::string &name = "")
{
    char *const n = current_thd->strdup(name.c_str());
    return new Item_null(n);
}

static std::string
xlua_tolstring(lua_State *l, int index)
{
    size_t len;
    const char *s = lua_tolstring(l, index, &len);
    return std::string(s, len);
}

static void
xlua_pushlstring(lua_State *l, const std::string &s)
{
    lua_pushlstring(l, s.data(), s.length());
}

static int
connect(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    const std::string client = xlua_tolstring(L, 1);
    const std::string server = xlua_tolstring(L, 2);
    const uint port = luaL_checkint(L, 3);
    const std::string user = xlua_tolstring(L, 4);
    const std::string psswd = xlua_tolstring(L, 5);
    const std::string embed_dir = xlua_tolstring(L, 6);

    ConnectionInfo ci = ConnectionInfo(server, user, psswd, port);

    if (clients.find(client) != clients.end()) {
           LOG(warn) << "duplicate client entry";
    }

    clients[client] = new WrapperState();

    // Is it the first connection?
    if (!ps) {
        std::cerr << "starting proxy\n";
        //cryptdb_logger::setConf(string(getenv("CRYPTDB_LOG")?:""));

        LOG(wrapper) << "connect " << client << "; "
                     << "server = " << server << ":" << port << "; "
                     << "user = " << user << "; "
                     << "password = " << psswd;

        const std::string dbname = "cryptdbtest";
        const std::string mkey = "113341234";  // XXX do not change as
                                               // it's used for tpcc exps
        char * ev = getenv("ENC_BY_DEFAULT");
        if (ev && "FALSE" == toUpperCase(ev)) {
            std::cerr << "\n\n enc by default false " << "\n\n";
            ps = new ProxyState(ci, embed_dir, dbname, mkey,
                                SECURITY_RATING::PLAIN);
        } else {
            std::cerr << "\n\nenc by default true" << "\n\n";
            ps = new ProxyState(ci, embed_dir, dbname, mkey,
                                SECURITY_RATING::BEST_EFFORT);
        }

        //may need to do training
        ev = getenv("TRAIN_QUERY");
        if (ev) {
            std::cerr << "Deprecated query training!" << std::endl;
        }

        ev = getenv("EXECUTE_QUERIES");
        if (ev && "FALSE" == toUpperCase(ev)) {
            LOG(wrapper) << "do not execute queries";
            EXECUTE_QUERIES = false;
        } else {
            LOG(wrapper) << "execute queries";
            EXECUTE_QUERIES = true;
        }

        ev = getenv("LOAD_ENC_TABLES");
        if (ev) {
            std::cerr << "No current functionality for loading tables\n";
            //cerr << "loading enc tables\n";
            //cl->loadEncTables(string(ev));
        }

        ev = getenv("LOG_PLAIN_QUERIES");
        if (ev) {
            std::string logPlainQueries = std::string(ev);
            if (logPlainQueries != "") {
                LOG_PLAIN_QUERIES = true;
                PLAIN_BASELOG = logPlainQueries;
                logPlainQueries += StringFromVal(++counter);

                assert_s(system(("rm -f" + logPlainQueries + "; touch " + logPlainQueries).c_str()) >= 0, "failed to rm -f and touch " + logPlainQueries);

                std::ofstream * PLAIN_LOG =
                    new std::ofstream(logPlainQueries, std::ios_base::app);
                LOG(wrapper) << "proxy logs plain queries at " << logPlainQueries;
                assert_s(PLAIN_LOG != NULL, "could not create file " + logPlainQueries);
                clients[client]->PLAIN_LOG = PLAIN_LOG;
            } else {
                LOG_PLAIN_QUERIES = false;
            }
        }
    } else {
        if (LOG_PLAIN_QUERIES) {
            std::string logPlainQueries =
                PLAIN_BASELOG+StringFromVal(++counter);
            assert_s(system((" touch " + logPlainQueries).c_str()) >= 0, "failed to remove or touch plain log");
            LOG(wrapper) << "proxy logs plain queries at " << logPlainQueries;

            std::ofstream * PLAIN_LOG =
                new std::ofstream(logPlainQueries, std::ios_base::app);
            assert_s(PLAIN_LOG != NULL, "could not create file " + logPlainQueries);
            clients[client]->PLAIN_LOG = PLAIN_LOG;
        }
    }



    return 0;
}

static int
disconnect(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end())
        return 0;

    LOG(wrapper) << "disconnect " << client;
    if (clients[client]->qr) {
        delete clients[client]->qr;
    }
    delete clients[client];
    clients.erase(client);

    return 0;
}

// FIXME: Use TELL policy.
static int
rewrite(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    const std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end()) {
        return 0;
    }

    const std::string query = xlua_tolstring(L, 2);
    const std::string query_data = xlua_tolstring(L, 3);

    std::list<std::string> new_queries;
    AssignOnce<PREAMBLE_STATUS> preamble_status;

    clients[client]->last_query = query;
    t.lap_ms();
    if (EXECUTE_QUERIES) {
        try {
            assert(ps);

            QueryRewrite *qr = NULL;
            preamble_status = queryPreamble(*ps, query, &qr, &new_queries);
            assert(qr);
            assert(preamble_status.get() != PREAMBLE_STATUS::FAILURE);

            // FIXME: Redundant.
            clients[client]->qr = qr;
            clients[client]->rmeta = qr->rmeta;
        } catch (CryptDBError &e) {
            LOG(wrapper) << "cannot rewrite " << query << ": " << e.msg;
            lua_pushboolean(L, false);
            lua_pushnil(L);
            return 2;
        }
    }

    if (LOG_PLAIN_QUERIES) {
        *(clients[client]->PLAIN_LOG) << query << "\n";
    }

    assert(PREAMBLE_STATUS::SUCCESS == preamble_status.get() ||
           (PREAMBLE_STATUS::ROLLBACK == preamble_status.get() &&
            new_queries.size() == 1));
    lua_pushboolean(L, PREAMBLE_STATUS::ROLLBACK == preamble_status.get());

    // NOTE: Potentially out of int range.
    assert(new_queries.size() < INT_MAX);
    lua_createtable(L, static_cast<int>(new_queries.size()), 0);
    const int top = lua_gettop(L);
    int index = 1;
    for (auto it : new_queries) {
        xlua_pushlstring(L, it);
        lua_rawseti(L, top, index);
        index++;
    }

    return 2;
}

static int
queryFailure(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end()) {
        throw CryptDBError("Failed to properly handle failed query!");
    }

    assert(EXECUTE_QUERIES);

    assert(ps);
    assert(clients[client]->qr);

    QueryRewrite *const qr = clients[client]->qr;
    assert(qr->output->handleQueryFailure(ps->getEConn()));

    return 0;
}

static int
epilogue(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end())
        return 0;

    assert(EXECUTE_QUERIES);

    assert(ps);
    assert(clients[client]->qr);

    QueryRewrite const *const qr = clients[client]->qr;
    assert(qr->output->afterQuery(ps->getEConn()));
    if (qr->output->queryAgain()) {
        ResType *const res_type =
            executeQuery(*ps, clients[client]->last_query);
        assert(res_type);

        // Tell lua not to decrypt the results because executeQuery
        // handles decryption.
        lua_pushboolean(L, false);
        lua_pushinteger(L, reinterpret_cast<lua_Integer>(res_type));
        return 2;
    }

    lua_pushboolean(L, qr->output->doDecryption());
    lua_pushnil(L);
    return 2;
}

static int
rollbackOnionAdjust(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    const std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end())
        return 0;

    assert(queryHandleRollback(*ps, clients[client]->last_query));

    return 0;
}

static int
passDecryptedPtr(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    const std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end())
        return 0;

    const ResType *const res_type =
        reinterpret_cast<ResType *>(lua_tointeger(L, 2));

    return returnResultSet(L, *res_type);
}

inline std::vector<Item *>
itemNullVector(unsigned int count)
{
    std::vector<Item *> out;
    for (unsigned int i = 0; i < count; ++i) {
        out.push_back(make_null());
    }

    return out;
}

static void
getResTypeFromLuaTable(lua_State *L, int fields_index, int rows_index,
                       ResType *const out_res)
{
    /* iterate over the fields argument */
    lua_pushnil(L);
    while (lua_next(L, fields_index)) {
        if (!lua_istable(L, -1))
            LOG(warn) << "mismatch";

        lua_pushnil(L);
        while (lua_next(L, -2)) {
            const std::string k = xlua_tolstring(L, -2);
            if (k == "name")
                out_res->names.push_back(xlua_tolstring(L, -1));
            else if (k == "type")
                out_res->types.push_back(static_cast<enum_field_types>(luaL_checkint(L, -1)));
            else
                LOG(warn) << "unknown key " << k;
            lua_pop(L, 1);
        }

        lua_pop(L, 1);
    }

    assert(out_res->names.size() == out_res->types.size());

    /* iterate over the rows argument */
    lua_pushnil(L);
    while (lua_next(L, rows_index)) {
        if (!lua_istable(L, -1))
            LOG(warn) << "mismatch";

        /* initialize all items to NULL, since Lua skips
           nil array entries */
        std::vector<Item *> row = itemNullVector(out_res->types.size());

        lua_pushnil(L);
        while (lua_next(L, -2)) {
            const int key = luaL_checkint(L, -2) - 1;

            assert(key >= 0
                   && static_cast<uint>(key) < out_res->types.size());
            const std::string data = xlua_tolstring(L, -1);
            Item *const value =
                make_item_by_type(data, out_res->types[key]);
            row[key] = value;

            lua_pop(L, 1);
        }
        // We can not use this assert because rows that contain many
        // NULLs don't return their columns in a strictly increasing
        // order.
        // assert((unsigned int)key == out_res->names.size() - 1);

        out_res->rows.push_back(row);
        lua_pop(L, 1);
    }
}

static int
decrypt(lua_State *L)
{
    ANON_REGION(__func__, &perf_cg);
    scoped_lock l(&big_lock);
    assert(0 == mysql_thread_init());

    THD *const thd = static_cast<THD *>(create_embedded_thd(0));
    auto thd_cleanup = cleanup([&thd]
        {
            thd->clear_data_list();
            thd->store_globals();
            thd->unlink();
            delete thd;
        });

    const std::string client = xlua_tolstring(L, 1);
    if (clients.find(client) == clients.end())
        return 0;
    const bool decryptp = lua_toboolean(L, 2);
    const ResType *const hack_res_type =
        reinterpret_cast<ResType *>(lua_tointeger(L, 3));

    if (false == decryptp && hack_res_type) {
        assert(hack_res_type);
        return returnResultSet(L, *hack_res_type);
    }

    ResType res;
    getResTypeFromLuaTable(L, 4, 5, &res);

    ResType rd;
    try {
        if (true == decryptp) {
            ResType *const rt =
                Rewriter::decryptResults(res, clients[client]->rmeta);
            assert(rt);
            rd = *rt;
        } else {
            rd = res;
        }
    }
    catch(CryptDBError e) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    return returnResultSet(L, rd);
}

static int
returnResultSet(lua_State *L, const ResType &rd)
{
    /* return decrypted result set */
    lua_createtable(L, (int)rd.names.size(), 0);
    int t_fields = lua_gettop(L);
    for (uint i = 0; i < rd.names.size(); i++) {
        lua_createtable(L, 0, 1);
        int t_field = lua_gettop(L);

        /* set name for field */
        xlua_pushlstring(L, rd.names[i]);
        lua_setfield(L, t_field, "name");

/*
        // FIXME.
        // set type for field
        lua_pushinteger(L, rd.types[i]);
        lua_setfield(L, t_field, "type");
*/

        /* insert field element into fields table at i+1 */
        lua_rawseti(L, t_fields, i+1);
    }

    lua_createtable(L, (int) rd.rows.size(), 0);
    int t_rows = lua_gettop(L);
    for (uint i = 0; i < rd.rows.size(); i++) {
        lua_createtable(L, (int) rd.rows[i].size(), 0);
        int t_row = lua_gettop(L);

        for (uint j = 0; j < rd.rows[i].size(); j++) {
            if (rd.rows[i][j] == NULL) {
                lua_pushnil(L);
            } else {
                xlua_pushlstring(L, ItemToString(rd.rows[i][j]));
            }
            lua_rawseti(L, t_row, j+1);
        }

        lua_rawseti(L, t_rows, i+1);
    }

    //cerr << clients[client]->last_query << " took (too long) " << t.lap_ms() << endl;;
    return 2;
}

static const struct luaL_reg
cryptdb_lib[] = {
#define F(n) { #n, n }
    F(connect),
    F(disconnect),
    F(rewrite),
    F(decrypt),
    F(rollbackOnionAdjust),
    F(passDecryptedPtr),
    F(epilogue),
    F(queryFailure),
    { 0, 0 },
};

extern "C" int lua_cryptdb_init(lua_State *L);

int
lua_cryptdb_init(lua_State *L)
{
    luaL_openlib(L, "CryptDB", cryptdb_lib, 0);
    return 1;
}
