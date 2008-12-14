#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "luahelper.h"

#define USE_UDATA

#ifdef USE_UDATA 
static inline int luaA_pushbit(lua_State *L, unsigned int num) {
	unsigned int *value = lua_newuserdata(L, sizeof(unsigned int));
	*value = (num);
	luaL_getmetatable(L, "bit_object");
	lua_setmetatable(L, -2);
	return 1;
}

static inline unsigned int luaA_checkbit(lua_State *L, int idx) {
	if (lua_type(L, idx) == LUA_TNUMBER)
		return lua_tonumber(L, idx);
	else
		return *(unsigned int *)luaL_checkudata(L, idx, "bit_object");
}

#define LUAA_BBIT_FUNC(name, op) \
	LUAA_FUNC(bit_##name) { \
		unsigned int a = luaA_checkbit(L, 1); \
		unsigned int b = luaA_checkbit(L, 2); \
		return luaA_pushbit(L, a op b); }

#define LUAA_UBIT_FUNC(name, op) \
	LUAA_FUNC(bit_##name) { \
		unsigned int a = luaA_checkbit(L, 1); \
		return luaA_pushbit(L, op a); }

#else

static inline int luaA_pushbit(lua_State *L, unsigned int num) {
	lua_pushnumber(L, (num));
	luaL_getmetatable(L, "bit_object");
	lua_setmetatable(L, -2);
	return 1;
}

#define LUAA_BBIT_FUNC(name, op) \
	LUAA_FUNC(bit_##name) { \
		unsigned int a = luaL_checknumber(L, 1); \
		unsigned int b = luaL_checknumber(L, 2); \
		return luaA_pushbit(L, a op b); }

#define LUAA_UBIT_FUNC(name, op) \
	LUAA_FUNC(bit_##name) { \
		unsigned int a = luaL_checknumber(L, 1); \
		return luaA_pushbit(L, op a); }
#endif


LUAA_BBIT_FUNC(and, &)
LUAA_BBIT_FUNC(or, |)
LUAA_UBIT_FUNC(not, ~)
LUAA_BBIT_FUNC(shr, >>)
LUAA_BBIT_FUNC(shl, <<)
LUAA_BBIT_FUNC(xor, ^)
LUAA_BBIT_FUNC(nand, & ~)

#ifdef USE_UDATA
LUAA_BBIT_FUNC(eq, ==)
LUAA_BBIT_FUNC(lt, <)

LUAA_FUNC(bit_tostring) {
	unsigned int b = luaA_checkbit(L, 1);
/*
	size_t sz = sizeof(unsigned int) * 8;
	int i;
	char *bits = malloc(sz);
	if (bits == NULL) return 0;

	memset(bits, '0', sz);
	for (i = 0; i < sz; i++) {
		if (b & (1 << i)) {
			bits[sz - i - 1] = '1';
		}
	}

	lua_pushlstring(L, bits, sz);
	free(bits);
*/
	lua_pushnumber(L, b);
	return 1;
}
#endif

LUAA_FUNC(bit_len) {
#ifdef USE_UDATA
	unsigned int a = luaA_checkbit(L, 1);
#else
	unsigned int a = luaL_checknumber(L, 1);
#endif
	int num = 0;
	for (;a != 0;a >>= 1)
		if (a & 1) num++;
	lua_pushnumber(L, num);
	return 1;
}

LUAA_FUNC(bit_index) {
#ifdef USE_UDATA
	unsigned int a = luaA_checkbit(L, 1);
#else
	unsigned int a = luaL_checknumber(L, 1);
#endif
	int i = lua_tonumber(L, 2) - 1;
	return luaA_pushbit(L, (a >> i) & 1);
}

LUAA_FUNC(bit_newindex) {
#ifdef USE_UDATA
	unsigned int a = luaA_checkbit(L, 1);
#else
	unsigned int a = luaL_checknumber(L, 1);
#endif
	unsigned int i = 1 << ((unsigned int)lua_tonumber(L, 2) - 1);
	int v = lua_tonumber(L, 3);
	return luaA_pushbit(L, v? (a | i): (a & ~i));
}

LUAA_FUNC(bit_call) {
	unsigned int a = lua_tonumber(L, 2);
	return luaA_pushbit(L, a);
}

static const luaL_reg bit_meta[] = {
	{"__mul", luaA_bit_and},
	{"__add", luaA_bit_or},
	{"__unm", luaA_bit_not},

	{"__pow", luaA_bit_xor},
	{"__sub", luaA_bit_nand},

	{"__div", luaA_bit_shr},
	{"__mod", luaA_bit_shl},

	{"__len", luaA_bit_len},

	{"__index", luaA_bit_index},
	{"__newindex", luaA_bit_newindex},

	{"__call", luaA_bit_call},

#ifdef USE_UDATA
	{"__lt", luaA_bit_lt},
	{"__eq", luaA_bit_eq},
	{"__tostring", luaA_bit_tostring},
#endif
	{NULL, NULL}
};

LUALIB_API int luaopen_bit (lua_State *L) {
	luaL_register(L, "bit", bit_meta);

	luaL_newmetatable(L, "bit_object");
	luaL_register(L, NULL, bit_meta);

	lua_setmetatable(L, -2);
/*
	lua_pushliteral(L, "version");
	lua_pushliteral(L, "bit library for lua");
	lua_settable(L, -3);
	return 1;
*/
	return 0;
}
