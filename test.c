#include <stdlib.h>
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>

static int luaA_test_obj(lua_State *L) {
	int* testobj = lua_newuserdata(L, 5 * sizeof(int));
	printf("creating object 0x%x\n", testobj);
	int i;
	for (i = 0; i < 5; i++)
		testobj[i] = i;
	luaL_getmetatable(L, "test_udata");
	lua_setmetatable(L, -2);
	return 1;
}

static int luaA_test_tostring(lua_State *L) {
	int *testobj = luaL_checkudata(L, 1, "test_udata");
	printf("udata testobj [0x%x]: %d,%d,%d,%d,%d\n", testobj, testobj[0], testobj[1], testobj[2], testobj[3], testobj[4]);
	lua_pushfstring(L, "udata testobj [0x%x]: %d,%d,%d,%d,%d", testobj, testobj[0], testobj[1], testobj[2], testobj[3], testobj[4]);
	return 1;
}

static int luaA_test_destroy(lua_State *L) {
	int *testobj = luaL_checkudata(L, 1, "test_udata");
	printf("destroying object 0x%x\n", testobj);
}

static const luaL_reg test_meta[] = {
	{"__gc", luaA_test_destroy},
	{"__tostring", luaA_test_tostring},
	{NULL, NULL}
};


static const luaL_reg test_methods[] = {
	{"testobj", luaA_test_obj},
	{NULL, NULL}
};

LUALIB_API int luaopen_test(lua_State *L) {

	luaL_newmetatable(L, "test_udata");
	luaL_register(L, NULL, test_meta);
	lua_pop(L, 1);

	luaL_register(L, "testobj", test_methods);

	//lua_pushliteral(L, "version");
	//lua_pushliteral(L, "sysctl library for lua");
	//lua_settable(L, -3);
	return 0;
}
