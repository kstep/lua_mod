#ifndef __LUA_HELPER__

#define __LUA_HELPER__

#include <lua.h>
#include <lauxlib.h>

#define luaA_settable(L, tableidx, index, luatype, value) \
	lua_push##luatype (L, value); \
	lua_setfield(L, tableidx, index)

#define luaA_isettable(L, tableidx, index, luatype, value) \
	lua_push##luatype (L, value); \
	lua_rawseti(L, tableidx, index)

#define LUAA_FUNC(name) static int luaA_##name (lua_State *L)

#define luaA_settype(L, idx, type) \
	luaL_getmetatable(L, type); \
	lua_setmetatable(L, idx)

#define luaA_return_unless(L, cond, idx) \
	do { \
	if (cond) { \
		lua_pop(L, idx); \
		return 0; \
	} \
	while (0)

#define LUAA_SREG(name) static const luaL_reg name [] = {
#define LUAA_REG(objname, name) { #name, luaA_##objname##_##name },
#define LUAA_MREG(objname, name) { "__" #name, luaA_##objname##_##name },
#define LUAA_EREG { NULL, NULL } };

#define LUAA_OPEN(libname, type, version) \
	LUALIB_API int luaopen_##libname (lua_State *L) { \
		luaL_newmetatable(L, type); \
		luaL_register(L, NULL, libname##_meta); \
		lua_pop(L, 1); \
		luaL_register(L, #libname, libname##_methods); \
		lua_pushliteral(L, "version"); \
		lua_pushliteral(L, #libname " library for lua " version); \
		lua_rawset(L, -3); \
		return 1; }

#endif
