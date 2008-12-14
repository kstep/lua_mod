#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

typedef struct {
	struct ifaddrs *ifap;
	struct ifaddrs *ifa;
} lifaddrs_t;

static int luaA_ifaddr_totable(lua_State *L, struct ifaddrs *ifa) {
	lua_createtable(L, 0, 4); //6);

	lua_pushliteral(L, "name");
	lua_pushstring(L, ifa->ifa_name);
	lua_settable(L, -3);

	lua_pushliteral(L, "flags");
	lua_pushnumber(L, ifa->ifa_flags);
	lua_settable(L, -3);

	lua_pushliteral(L, "family");
	lua_pushnumber(L, ifa->ifa_addr->sa_family);
	lua_settable(L, -3);

	lua_pushliteral(L, "ipaddr");
	lua_pushstring(L, inet_ntoa(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr));
	lua_settable(L, -3);

	return 1;
}

static int luaA_ifaddr_next(lua_State *L) {
	lifaddrs_t *lifa = lua_touserdata(L, 1);
	lifa->ifa = lifa->ifa->ifa_next;
	//while ((lifa->ifa = lifa->ifa->ifa_next) && (lifa->ifa->ifa_addr->sa_family != AF_INET));

	if (lifa->ifa == NULL) return 0; 
	
	return luaA_ifaddr_totable(L, lifa->ifa);
}

static int luaA_ifaddr_each(lua_State *L) {
	lifaddrs_t *lifa = luaL_checkudata(L, 1, "ifaddrs");

	lifa->ifa = lifa->ifap;
	lua_pushcfunction(L, luaA_ifaddr_next);
	lua_pushlightuserdata(L, lifa);
	luaA_ifaddr_totable(L, lifa->ifap);

	return 3;
}

static int luaA_ifaddr_index(lua_State *L) {
	lifaddrs_t *lifa = luaL_checkudata(L, 1, "ifaddrs");

	luaL_getmetatable(L, "ifaddrs");
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);

	if (!lua_isnil(L, -1)) {
		lua_remove(L, -2);
		return 1;
	}
	lua_pop(L, 2);

	const char *index = luaL_checkstring(L, 2);
	struct ifaddrs *ifa;

	for (ifa = lifa->ifap; ifa; ifa = ifa->ifa_next)
		if (ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, index) == 0)
			return luaA_ifaddr_totable(L, ifa);

	return 0;
}

static int luaA_ifaddr_init(lua_State *L) {
	lifaddrs_t *ifaddr = lua_newuserdata(L, sizeof(lifaddrs_t));
	if (getifaddrs(&(ifaddr->ifap))) {
		lua_pop(L, 1);
		return 0;
	}
	ifaddr->ifa = ifaddr->ifap;
	luaL_getmetatable(L, "ifaddrs");
	lua_setmetatable(L, -2);
	return 1;
}

static int luaA_ifaddr_rewind(lua_State *L) {
	lifaddrs_t *lifa = luaL_checkudata(L, 1, "ifaddrs");
	lifa->ifa = lifa->ifap;
	return 0;
}

static int luaA_ifaddr_gc(lua_State *L) {
	lifaddrs_t *lifa = luaL_checkudata(L, 1, "ifaddrs");
	freeifaddrs(lifa->ifap);
	return 0;
}

static const luaL_reg ifaddr_methods[] = {
	{"init", luaA_ifaddr_init},
	{NULL, NULL}
};

static const luaL_reg ifaddr_meta[] = {
	{"__index", luaA_ifaddr_index},
	{"__gc", luaA_ifaddr_gc},
	//{"__newindex", luaA_ifaddr_newindex},
	//{"__tostring", luaA_mixer_tostring},
	//{"__eq", luaA_mixer_eq},
	{"each", luaA_ifaddr_each},
	{"next", luaA_ifaddr_next},
	{"rewind", luaA_ifaddr_rewind},
	{NULL, NULL}
};

LUALIB_API int luaopen_ifaddrs (lua_State *L) {

	luaL_newmetatable(L, "ifaddrs");
	luaL_register(L, NULL, ifaddr_meta);

	lua_pop(L, 1);
	luaL_register(L, "ifaddrs", ifaddr_methods);

	lua_pushliteral(L, "version");
	lua_pushliteral(L, "ifaddrs library for lua");
	lua_settable(L, -3);

	return 1;
}
