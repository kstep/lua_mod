#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>

#include <lua.h>
#include <lauxlib.h>

#define luaA_settable(L, tableidx, index, luatype, value) \
	lua_push##luatype (L, value); \
	lua_setfield(L, tableidx, index)

#define luaA_isettable(L, tableidx, index, luatype, value) \
	lua_push##luatype (L, value); \
	lua_rawseti(L, tableidx, index)

static int luaA_mntinfo_statfs(lua_State *L, struct statfs *stfs) {
	lua_createtable(L, 0, 20);

	luaA_settable(L, -2, "version", number, stfs->f_version);
	luaA_settable(L, -2, "type", number, stfs->f_type);
	luaA_settable(L, -2, "flags", number, stfs->f_flags);
	luaA_settable(L, -2, "bsize", number, stfs->f_bsize);
	luaA_settable(L, -2, "iosize", number, stfs->f_iosize);
	luaA_settable(L, -2, "blocks", number, stfs->f_blocks);
	luaA_settable(L, -2, "bfree", number, stfs->f_bfree);
	luaA_settable(L, -2, "bavail", number, stfs->f_bavail);
	luaA_settable(L, -2, "files", number, stfs->f_files);
	luaA_settable(L, -2, "ffree", number, stfs->f_ffree);
	luaA_settable(L, -2, "syncwrites", number, stfs->f_syncwrites);
	luaA_settable(L, -2, "asyncwrites", number, stfs->f_asyncwrites);
	luaA_settable(L, -2, "syncreads", number, stfs->f_syncreads);
	luaA_settable(L, -2, "asyncreads", number, stfs->f_asyncreads);
	//luaA_settable(L, -2, "spare", number, stfs->f_spare);
	luaA_settable(L, -2, "namemax", number, stfs->f_namemax);
	luaA_settable(L, -2, "owner", number, stfs->f_owner);

	lua_createtable(L, 2, 0);
	luaA_isettable(L, -2, 1, number, stfs->f_fsid.val[0]);
	luaA_isettable(L, -2, 2, number, stfs->f_fsid.val[1]);
	lua_setfield(L, -2, "fsid");

	//luaA_settable(L, -2, "charspare", string, stfs->f_charspare);
	luaA_settable(L, -2, "fstypename", string, stfs->f_fstypename);
	luaA_settable(L, -2, "mntfromname", string, stfs->f_mntfromname);
	luaA_settable(L, -2, "mntonname", string, stfs->f_mntonname);

	return 1;
}

static int luaA_mntinfo_getstatfs(lua_State *L) {
	const char* mntpname = luaL_checkstring(L, 1);
	struct statfs stfs;
	if (statfs(mntpname, &stfs))
		return 0;

	return luaA_mntinfo_statfs(L, &stfs);
}

static int luaA_mntinfo_next(lua_State *L) {
	struct statfs *stfs = (struct statfs *)lua_touserdata(L, 1);
	int index = luaL_checknumber(L, 2) - 1;
	if (index < 1) return 0;
	lua_pushnumber(L, index);
	luaA_mntinfo_statfs(L, &stfs[index - 1]);
	return 2;
}

static int luaA_mntinfo_each(lua_State *L) {
	struct statfs *stfs = NULL;
	int items;
	items = getmntinfo(&stfs, MNT_NOWAIT);
	if (items < 1) return 0;

	lua_pushcfunction(L, luaA_mntinfo_next);
	lua_pushlightuserdata(L, stfs);
	lua_pushnumber(L, items);
	luaA_mntinfo_statfs(L, &stfs[items - 1]);
	return 4;
}

static const luaL_reg mntinfo_methods[] = {
	{"getstat", luaA_mntinfo_getstatfs},
	{"each", luaA_mntinfo_each},
	{NULL, NULL}
};

LUALIB_API int luaopen_mntinfo(lua_State *L) {

	luaL_register(L, "mntinfo", mntinfo_methods);
	lua_pushliteral(L, "version");
	lua_pushliteral(L, "mntinfo library for lua");
	lua_settable(L, -3);
	return 1;
}

