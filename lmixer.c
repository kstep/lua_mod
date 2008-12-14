#include <fcntl.h>
#include <sys/soundcard.h>
#include <stdlib.h>

#include <lua.h>
#include <lauxlib.h>

#ifdef DEBUG
#include <err.h>
#else
#define warn(fmt, ...)
#endif

static char* names[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;

typedef struct {
	/*char* name;*/
	int num;
	int fh;
	int refcnt;
} mixer_t;

typedef struct {
	mixer_t *mixer;
	int devno;
	int muted;
} mixer_device_t;

static int get_mixer_dev_num(const char* name) {
	int devno = -1;
	int i;

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		if (strcmp(names[i], name) == 0) {
			devno = i;
			break;
		}
	}

	return devno;
}

static int open_mixer_dev(int mixerno) {
	char buf[14];
	snprintf(buf, sizeof(buf), "/dev/mixer%d", mixerno);

	int mixer = open(buf, O_RDWR);
	return mixer;
}
static int read_mixer(int fh, int devno) {
	int value;
	if (ioctl(fh, MIXER_READ(devno), &value) < 0) return -1;
	return value;
}
static int write_mixer(int fh, int devno, int value) {
	if (ioctl(fh, MIXER_WRITE(devno), &value) < 0) return -1;
	return 0;
}


static inline int luaA_usemetatable(lua_State *L, int objidx, int methodidx) {
	lua_getmetatable(L, objidx);
	lua_pushvalue(L, methodidx);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1)) {
		lua_remove(L, -2);
		return 1;
	}
	lua_pop(L, 2);
	return 0;
}

static int luaA_mixer_device(lua_State *L) {
	mixer_t **mixer = luaL_checkudata(L, 1, "mixer");
	lua_pushfstring(L, "/dev/mixer%d", (*mixer)->num);
	return 1;
}
static int luaA_device_device(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");
	lua_pushstring(L, names[channel->devno]);
	return 1;
}

static int luaA_mixer_get(lua_State *L) {
	mixer_t **mixer = luaL_checkudata(L, 1, "mixer");

	const char* devname = luaL_checkstring(L, 2);

	if (luaA_usemetatable(L, 1, 2)) return 1;

	int devno = get_mixer_dev_num(devname);

	if ((*mixer)->fh < 0 || devno < 0 || read_mixer((*mixer)->fh, devno) < 0) return 0;

	mixer_device_t *channel = lua_newuserdata(L, sizeof(mixer_device_t));

	channel->mixer = *mixer;
	channel->devno = devno;
	channel->muted = 0;
	(*mixer)->refcnt++;
	warn("Mixer %p refcount increased to %d", *mixer, (*mixer)->refcnt);

	luaL_getmetatable(L, "mixer_device");
	lua_setmetatable(L, -2);

	return 1;
}


static int luaA_mixer_set(lua_State *L) {
	mixer_t **mixer = luaL_checkudata(L, 1, "mixer");
	const char* devname = luaL_checkstring(L, 2);
	int type = lua_type(L, 3);
	int value = -1, leftchan, rightchan;

	if (type == LUA_TTABLE) {
		lua_pushnumber(L, 1); lua_gettable(L, 3);
		lua_pushnumber(L, 2); lua_gettable(L, 3);
		leftchan = luaL_checknumber(L, -2);
		rightchan = luaL_checknumber(L, -1);
		lua_pop(L, 2);
	} else if (type == LUA_TNUMBER) {
		leftchan = luaL_checknumber(L, 3);
		rightchan = lua_gettop(L) > 3? luaL_checknumber(L, 4): leftchan;
	} else {
		mixer_device_t *channel = luaL_checkudata(L, 3, "mixer_device");
		value = read_mixer(channel->mixer->fh, channel->devno);
		if (value < 0) return 0;
	}

	if (value == -1) {
		if (leftchan < 0) leftchan = 0;
		if (rightchan < 0) rightchan = 0;
		value = (leftchan & 0x7f) | ((rightchan & 0x7f) << 8);
	}

	int devno = get_mixer_dev_num(devname);

	if ((*mixer)->fh >= 0 && devno >= 0) write_mixer((*mixer)->fh, devno, value);
}

static int luaA_mixer_name(lua_State *L) {
	mixer_t **mixer = luaL_checkudata(L, 1, "mixer");
	lua_pushfstring(L, "udata mixer /dev/mixer%d [fh:%d]", (*mixer)->num, (*mixer)->fh);
	return 1;
}

static int luaA_mixer_open(lua_State *L) {
	int mixernum = luaL_checknumber(L, 1);
	int mixerdev = open_mixer_dev(mixernum);

	if (mixerdev < 0) return 0;

	mixer_t *newmixer = (mixer_t *)malloc(sizeof(mixer_t));
	if (newmixer == NULL) return 0;

	mixer_t **mixptr = lua_newuserdata(L, sizeof(mixer_t *));
	*mixptr = newmixer;

	newmixer->num = mixernum;
	newmixer->fh = mixerdev;
	newmixer->refcnt = 1;
	warn("Mixer %p refcount initialized to %d", newmixer, newmixer->refcnt);

	luaL_getmetatable(L, "mixer");
	lua_setmetatable(L, -2);

	return 1;
}

static int luaA_device_table(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");
	int value = read_mixer(channel->mixer->fh, channel->devno);
	if (value < 0) return 0;

	lua_createtable(L, 2, 0);
	lua_pushnumber(L, 1); lua_pushnumber(L, value & 0x7f);
	lua_settable(L, -3);
	lua_pushnumber(L, 2); lua_pushnumber(L, (value >> 8) & 0x7f);
	lua_settable(L, -3);

	return 1;
}

static int luaA_device_string(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");
	int value = read_mixer(channel->mixer->fh, channel->devno);
	if (value < 0) return 0;

	lua_pushfstring(L, "%d:%d", value & 0x7f, (value >> 8) & 0x7f);

	return 1;
}

static int luaA_device_mixer(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");

	mixer_t **mixer = lua_newuserdata(L, sizeof(mixer_t *));
	*mixer = channel->mixer;
	(*mixer)->refcnt++;
	warn("Mixer %p refcount increased to %d", *mixer, (*mixer)->refcnt);

	luaL_getmetatable(L, "mixer");
	lua_setmetatable(L, -2);
	return 1;
}

static int luaA_device_get(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");

	if (luaA_usemetatable(L, 1, 2)) return 1;

	if (lua_isstring(L, 2)) {
		const char* tmp = luaL_checkstring(L, 2);
		if (strcmp(tmp, "muted") == 0) {
			lua_pushboolean(L, channel->muted != 0);
			return 1;
		}
	}

	int num = luaL_checknumber(L, 2);

	if (num < 1 || num > 2) return 0;

	int value = read_mixer(channel->mixer->fh, channel->devno);
	if (value < 0) return 0;

	if (num == 2) value >>= 8;
	lua_pushnumber(L, value & 0x7f);

	return 1;
}

static int luaA_device_set(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");

	if (lua_isstring(L, 2)) {
		const char* tmp = luaL_checkstring(L, 2);
		if (strcmp(tmp, "muted") == 0) {
			int state = lua_toboolean(L, 3);
			if (state && (channel->muted == 0)) {
				channel->muted = read_mixer(channel->mixer->fh, channel->devno);
				write_mixer(channel->mixer->fh, channel->devno, 0);
			} else {
				write_mixer(channel->mixer->fh, channel->devno, channel->muted);
				channel->muted = 0;
			}
			return 0;
		}
	}

	int num = luaL_checknumber(L, 2);
	int newvalue = luaL_checknumber(L, 3);

	if (num < 1 || num > 2) return 0;
	if (newvalue < 0) newvalue = 0;

	int value = read_mixer(channel->mixer->fh, channel->devno);
	if (value < 0) return 0;

	newvalue &= 0x7f;
	newvalue = num == 1? (value & (0x7f << 8) | newvalue): (value & 0x7f | (newvalue << 8));

	write_mixer(channel->mixer->fh, channel->devno, newvalue);

	return 0;
}

static int luaA_device_both(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");
	int newvalue = luaL_checknumber(L, 2);
	if (newvalue < 0) newvalue = 0;

	newvalue &= 0x7f;

	write_mixer(channel->mixer->fh, channel->devno, newvalue | (newvalue << 8));

}
static int luaA_device_name(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");
	int value = read_mixer(channel->mixer->fh, channel->devno);
	char buf[40];
	if (value < 0) {
		lua_pushfstring(L, "udata mixer #%d device %s [error]", channel->mixer->num, names[channel->devno]);
	} else {
		lua_pushfstring(L, "udata mixer #%d device %s [%d:%d]", channel->mixer->num, names[channel->devno], value & 0x7f, (value >> 8) & 0x7f);
	}
	return 1;
}

static int luaA_device_equal(lua_State *L) {
	mixer_device_t *chan1 = luaL_checkudata(L, 1, "mixer_device");
	mixer_device_t *chan2 = luaL_checkudata(L, 2, "mixer_device");
	int value1 = read_mixer(chan1->mixer->fh, chan1->devno);
	int value2 = read_mixer(chan2->mixer->fh, chan2->devno);
	lua_pushboolean(L, value1 == value2);
	return 1;
}

static int luaA_device_less(lua_State *L) {
	mixer_device_t *chan1 = luaL_checkudata(L, 1, "mixer_device");
	mixer_device_t *chan2 = luaL_checkudata(L, 2, "mixer_device");
	
	int value1 = read_mixer(chan1->mixer->fh, chan1->devno);
	int value2 = read_mixer(chan2->mixer->fh, chan2->devno);
	int lchan, rchan;
	int min1, min2;

	lchan = value1 & 0x7f; rchan = (value1 >> 8) & 0x7f;
	if (lchan > rchan) {
		value1 = lchan; min1 = rchan;
	} else {
		value1 = rchan; min1 = lchan;
	}

	lchan = value2 & 0x7f; rchan = (value2 >> 8) & 0x7f;
	if (lchan > rchan) {
		value2 = lchan; min2 = rchan;
	} else {
		value2 = rchan; min2 = lchan;
	}

	lua_pushboolean(L, value1 == value2? min1 < min2: value1 < value2);
	return 1;
}

static int luaA_mixer_equal(lua_State *L) {
	mixer_t **mixer1 = luaL_checkudata(L, 1, "mixer");
	mixer_t **mixer2 = luaL_checkudata(L, 2, "mixer");
	lua_pushboolean(L, (*mixer1)->num == (*mixer2)->num);
	return 1;
}

static int luaA_mixer_close(lua_State *L) {
	mixer_t **mixer = luaL_checkudata(L, 1, "mixer");
	(*mixer)->refcnt--;
	warn("Mixer %p refcount decreased to %d", *mixer, (*mixer)->refcnt);
	if ((*mixer)->refcnt < 1) {
		warn("Mixer %d (fh=%d) collected by GC!\n", (*mixer)->num, (*mixer)->fh);
		close((*mixer)->fh);
		free(*mixer);
	}
	return 0;
}

static int luaA_device_gc(lua_State *L) {
	mixer_device_t *channel = luaL_checkudata(L, 1, "mixer_device");
	channel->mixer->refcnt--;
	warn("Mixer %p refcount decreased to %d", channel->mixer, channel->mixer->refcnt);
}

static const luaL_reg mixer_methods[] = {
	{"open", luaA_mixer_open},
	{"close", luaA_mixer_close},
	{NULL, NULL}
};

static const luaL_reg mixer_meta[] = {
	{"__index", luaA_mixer_get},
	{"__newindex", luaA_mixer_set},
	{"__gc", luaA_mixer_close},
	{"__tostring", luaA_mixer_name},
	{"__eq", luaA_mixer_equal},
	{"device", luaA_mixer_device},
	{NULL, NULL}
};

static const luaL_reg mixer_device_meta[] = {
	{"__index", luaA_device_get},
	{"__newindex", luaA_device_set},
	{"__tostring", luaA_device_name},
	{"__eq", luaA_device_equal},
	{"__lt", luaA_device_less},
	{"__gc", luaA_device_gc},
	{"asstring", luaA_device_string},
	{"astable", luaA_device_table},
	{"both", luaA_device_both},
	{"mixer", luaA_device_mixer},
	{"device", luaA_device_device},
	{NULL, NULL}
};

inline void luaA_openlib (lua_State *L, const char* name, const luaL_reg meta[], const luaL_reg methods[]) {
	luaL_newmetatable(L, name);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	luaL_register(L, NULL, meta);
	luaL_register(L, name, methods);
	lua_pushvalue(L, -1);
	lua_setmetatable(L, -2);
	lua_pop(L, 2);
}

LUALIB_API int luaopen_mixer (lua_State *L) {
	luaL_newmetatable(L, "mixer_device");
	luaL_register(L, NULL, mixer_device_meta);
	lua_pop(L, 1);

	luaA_openlib(L, "mixer", mixer_meta, mixer_methods);
/*
	lua_pushliteral(L, "version");
	lua_pushliteral(L, "mixer library for lua");
	lua_settable(L, -3);
*/
	return 0;
}
