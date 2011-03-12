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

#define IN_ENUM(ext, ind) ((ext)[(ind) / 8] & (1 << ((ind) % 8)))
#define BOUND_CHECK(value, min, max) if ((value) < (min)) (value) = (min); else if ((value) > (max)) (value) = (max)

typedef struct {
    /*char* name;*/
    int num;
    int fh;
    int refcnt;
} mixer_t;

typedef struct {
    mixer_t *mixer;
    int devno;
    oss_mixext ext;
} mixer_ext_t;

static int open_mixer_dev(int mixerno) {
    char buf[14];
    if (mixerno > 0) {
        snprintf(buf, sizeof(buf), "/dev/mixer%d", mixerno);
    } else {
        snprintf(buf, sizeof(buf), "/dev/mixer");
    }

    int mixer = open(buf, O_RDWR);
    return mixer;
}
static int read_mixer(mixer_ext_t *mext) {
    oss_mixer_value val;
    val.dev = mext->ext.dev;
    val.ctrl = mext->ext.ctrl;
    val.timestamp = mext->ext.timestamp;
    if (ioctl(mext->mixer->fh, SNDCTL_MIX_READ, &val) < 0) return -1;
    return val.value;
}
static int write_mixer(mixer_ext_t *mext, int value) {
    oss_mixer_value val;
    val.dev = mext->ext.dev;
    val.ctrl = mext->ext.ctrl;
    val.timestamp = mext->ext.timestamp;
    val.value = value;
    if (ioctl(mext->mixer->fh, SNDCTL_MIX_WRITE, &val) < 0) return -1;
    return 0;
}

static int format_stereo_value(lua_State *L, mixer_ext_t *mext, int value) {
    int shift = 8;
    int mask = 0xff;
    switch (mext->ext.type) {
    case MIXT_STEREOSLIDER16:
    case MIXT_STEREODB:
    shift = 16; mask = 0xffff; break;
    case MIXT_STEREOSLIDER:
    case MIXT_STEREOPEAK:
    case MIXT_STEREOVU:
    shift = 8; mask = 0xff; break;
    default:
    return 0;
    }

    lua_pushnumber(L, value & mask);
    lua_pushnumber(L, (value >> shift) & mask);
    return 2;
}

static int format_mono_value(lua_State *L, mixer_ext_t *mext, int value) {
    int mask = 0xff;
    switch (mext->ext.type) {
    case MIXT_SLIDER:
    mask = ~0; break;
    case MIXT_MONOSLIDER16:
    case MIXT_MONODB:
    mask = 0xffff; break;
    case MIXT_MONOSLIDER:
    case MIXT_MONOPEAK:
    case MIXT_MONOVU:
    mask = 0xff; break;
    default:
    return 0; return;
    }

    lua_pushnumber(L, value & mask);
    return 1;
}

static int format_switch_value(lua_State *L, mixer_ext_t *mext, int value) {
    switch (mext->ext.type) {
    case MIXT_ONOFF:
    case MIXT_MUTE:
    break;
    default:
    return 0;
    }

    lua_pushboolean(L, value? 1: 0);
    return 1;
}

static int format_enum_value(lua_State *L, mixer_ext_t *mext, int value) {
    if (mext->ext.type != MIXT_ENUM) return 0;
    value &= 0xff;
    oss_mixer_enuminfo einf;

    einf.dev = mext->ext.dev;
    einf.ctrl = mext->ext.ctrl;
    if (ioctl(mext->mixer->fh, SNDCTL_MIX_ENUMINFO, &einf) < 0) return 0;
    if (value >= einf.nvalues) return 0;

    lua_pushstring(L, einf.strings + einf.strindex[value]);
    return 1;
}

static int format_normal_value(lua_State *L, mixer_ext_t *mext, int value) {
    switch (mext->ext.type) {
    case MIXT_VALUE:
    case MIXT_HEXVALUE:
    break;
    default:
    return 0;
    }

    lua_pushnumber(L, value);
    return 1;
}

static int pack_into_table(lua_State *L, int nvalues) {
    int i;
    lua_createtable(L, nvalues, 0);
    for (i = 0; i < nvalues; i++) {
        lua_pushvalue(L, -2);
        lua_remove(L, -3);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
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

static int luaA_mixer_ext(lua_State *L) {
    return 0;
}
static int luaA_mixer_ext_device(lua_State *L) {
    return 0;
}
static int luaA_mixer_set(lua_State *L) {
    return 0;
}

static int luaA_mixer_name(lua_State *L) {
    return 0;
}

static int luaA_mixer_ext_table(lua_State *L) {
    return 0;
}

static int luaA_mixer_ext_string(lua_State *L) {
    return 0;
}

static int luaA_mixer_ext_mixer(lua_State *L) {
    mixer_ext_t *mext = luaL_checkudata(L, 1, "mixer_ext");

    mixer_t **mixer = lua_newuserdata(L, sizeof(mixer_t *));
    *mixer = mext->mixer;
    (*mixer)->refcnt++;
    warn("Mixer %p refcount increased to %d", *mixer, (*mixer)->refcnt);

    luaL_getmetatable(L, "mixer_dev");
    lua_setmetatable(L, -2);
    return 1;
}

static int luaA_mixer_ext_name(lua_State *L) {
    return 0;
}

static int luaA_mixer_ext_equal(lua_State *L) {
    return 0;
}

static int luaA_mixer_ext_less(lua_State *L) {
    return 0;
}

static int luaA_mixer_equal(lua_State *L) {
    mixer_t **mixer1 = luaL_checkudata(L, 1, "mixer_dev");
    mixer_t **mixer2 = luaL_checkudata(L, 2, "mixer_dev");
    lua_pushboolean(L, (*mixer1)->num == (*mixer2)->num);
    return 1;
}

static int luaA_mixer_ext_gc(lua_State *L) {
    mixer_ext_t *mext = luaL_checkudata(L, 1, "mixer_ext");
    mext->mixer->refcnt--;
    warn("Mixer %p refcount decreased to %d", mext->mixer, mext->mixer->refcnt);
}


// -------------------------------------------------------------------------------------------------------
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

    luaL_getmetatable(L, "mixer_dev");
    lua_setmetatable(L, -2);

    return 1;
}

static int luaA_mixer_close(lua_State *L) {
    mixer_t **mixer = luaL_checkudata(L, 1, "mixer_dev");
    (*mixer)->refcnt--;
    warn("Mixer %p refcount decreased to %d", *mixer, (*mixer)->refcnt);
    if ((*mixer)->refcnt < 1) {
        warn("Mixer %d (fh=%d) collected by GC!\n", (*mixer)->num, (*mixer)->fh);
        close((*mixer)->fh);
        free(*mixer);
    }
    return 0;
}

static const luaL_reg mixer_methods[] = {
    {"open", luaA_mixer_open},
    {"close", luaA_mixer_close},
    {NULL, NULL}
};

static int luaA_mixer_get(lua_State *L) {
    mixer_t **mixer = luaL_checkudata(L, 1, "mixer_dev");
    if ((*mixer)->fh < 0) return 0;
    if (luaA_usemetatable(L, 1, 2)) return 1;

    int devno = -1;
    const char* devname;

    if (lua_isnumber(L, 2)) {
        devno = luaL_checknumber(L, 2) - 1;
        if (devno < 0) return 0;
    } else {
        devname = luaL_checkstring(L, 2);
    }

    int i, nrext;

    mixer_ext_t *mext = lua_newuserdata(L, sizeof(mixer_ext_t));
    mext->mixer = *mixer;
    mext->ext.dev = (*mixer)->num;

    if (devno < 0) {
        nrext = (*mixer)->num;
        if (ioctl((*mixer)->fh, SNDCTL_MIX_NREXT, &nrext) < 0)
            return 0;

        for (i = 0; i < nrext; i++) {
            mext->ext.ctrl = i;
            if (ioctl((*mixer)->fh, SNDCTL_MIX_EXTINFO, &(mext->ext)) < 0)
                return 0;

            if ((strcmp(mext->ext.extname, devname) == 0)) { // || (strcmp(mext->ext.id, devname) == 0)) {
                devno = i;
                break;
            }
        }

        if (devno < 0) return 0;

    } else {
        mext->ext.ctrl = devno;
        if (ioctl((*mixer)->fh, SNDCTL_MIX_EXTINFO, &(mext->ext)) < 0)
            return 0;
    }

    mext->devno = devno;
    (*mixer)->refcnt++;
    warn("Mixer %p refcount increased to %d", *mixer, (*mixer)->refcnt);

    luaL_getmetatable(L, "mixer_ext");
    lua_setmetatable(L, -2);

    return 1;
}

static int luaA_mixer_len(lua_State *L) {
    mixer_t **mixer = luaL_checkudata(L, 1, "mixer_dev");
    int nmix;
    nmix = (*mixer)->num;
    if (ioctl((*mixer)->fh, SNDCTL_MIX_NREXT, &nmix) < 0)
        return 0;

    oss_mixext ext;
    int i;

    for (i = 0; i < nmix; i++) {
        ext.dev = (*mixer)->num;
        ext.ctrl = i;
        if (ioctl((*mixer)->fh, SNDCTL_MIX_EXTINFO, &ext) < 0)
            return 0;
        /*printf("%s %s\n", ext.extname, ext.id);*/
    }

    lua_pushnumber(L, nmix);
    return 1;
}

static const luaL_reg mixer_meta[] = {
    {"__index", luaA_mixer_get},
    {"__newindex", luaA_mixer_set},
    {"__gc", luaA_mixer_close},
    /*{"__tostring", luaA_mixer_name},*/
    {"__eq", luaA_mixer_equal},
    {"__len", luaA_mixer_len},
    {"mixer_ext", luaA_mixer_ext},
    {NULL, NULL}
};

static int luaA_mixer_ext_get(lua_State *L) {
    mixer_ext_t *mext = luaL_checkudata(L, 1, "mixer_ext");
    if (luaA_usemetatable(L, 1, 2)) return 1;

    int value, i;
    
    if (lua_isnumber(L, 2)) {
        i = luaL_checknumber(L, 2);
        if (i < 1 || 2 < i) return 0;

        value = read_mixer(mext);
        if (format_stereo_value(L, mext, value) == 0)
            return 0;

        lua_remove(L, -i);
        return 1;
    }

    const char *index = luaL_checkstring(L, 2);
    oss_mixer_enuminfo enuminf;
    int result;

    if (strcmp(index, "type") == 0) {
        switch (mext->ext.type) {
        case MIXT_3D             : lua_pushstring(L, "3d"); break;
        case MIXT_DEVROOT        : lua_pushstring(L, "devroot"); break;
        case MIXT_ENUM           : lua_pushstring(L, "enum"); break;
        case MIXT_GROUP          : lua_pushstring(L, "group"); break;
        case MIXT_HEXVALUE       : lua_pushstring(L, "hexvalue"); break;
        case MIXT_MARKER         : lua_pushstring(L, "marker"); break;
        case MIXT_MESSAGE        : lua_pushstring(L, "message"); break;
        case MIXT_MONODB         : lua_pushstring(L, "mono db"); break;
        case MIXT_MONOPEAK       : lua_pushstring(L, "mono peak"); break;
        case MIXT_MONOSLIDER16   : lua_pushstring(L, "mono slider16"); break;
        case MIXT_MONOSLIDER     : lua_pushstring(L, "mono slider"); break;
        case MIXT_MONOVU         : lua_pushstring(L, "mono vu"); break;
        case MIXT_MUTE           : lua_pushstring(L, "mute"); break;
        case MIXT_ONOFF          : lua_pushstring(L, "switch"); break;
        case MIXT_RADIOGROUP     : lua_pushstring(L, "radio group"); break;
        /*case MIXT_ROOT           : lua_pushstring(L, "root"); break;*/
        case MIXT_SLIDER         : lua_pushstring(L, "slider"); break;
        case MIXT_STEREODB       : lua_pushstring(L, "stereo db"); break;
        case MIXT_STEREOPEAK     : lua_pushstring(L, "stereo peak"); break;
        case MIXT_STEREOSLIDER16 : lua_pushstring(L, "stereo slider16"); break;
        case MIXT_STEREOSLIDER   : lua_pushstring(L, "stereo slider"); break;
        case MIXT_STEREOVU       : lua_pushstring(L, "stereo vu"); break;
        case MIXT_VALUE          : lua_pushstring(L, "value"); break;
        default: return 0;
        }
        return 1;

    } else if (strcmp(index, "name") == 0) {
        lua_pushstring(L, mext->ext.extname);
        return 1;

    } else if (strcmp(index, "id") == 0) {
        lua_pushstring(L, mext->ext.id);
        return 1;

    } else if (strcmp(index, "value") == 0) {
        value = read_mixer(mext);
        if (value < 0) return 0;

        if (result = format_normal_value(L, mext, value));
        else if (result = format_stereo_value(L, mext, value));
        else if (result = format_mono_value(L, mext, value));
        else if (result = format_switch_value(L, mext, value));
        else if (result = format_enum_value(L, mext, value));
        else return 0;

        if (result > 1) pack_into_table(L, result);
        return 1;

    } else if (strcmp(index, "min") == 0) {
        lua_pushnumber(L, mext->ext.minvalue);
        return 1;

    } else if (strcmp(index, "max") == 0) {
        lua_pushnumber(L, mext->ext.maxvalue);
        return 1;

    } else if (strcmp(index, "color") == 0) {
        lua_pushnumber(L, mext->ext.rgbcolor);
        return 1;

    } else if (strcmp(index, "enum") == 0) {
        if (mext->ext.type != MIXT_ENUM) return 0;

        enuminf.dev = mext->ext.dev;
        enuminf.ctrl = mext->ext.ctrl;
        if (ioctl(mext->mixer->fh, SNDCTL_MIX_ENUMINFO, &enuminf) < 0) return 0;

        lua_createtable(L, mext->ext.maxvalue, 0);

        value = 0;
        for (i = 0; i < mext->ext.maxvalue; i++) {
            if (!IN_ENUM(mext->ext.enum_present, i)) continue;
            lua_pushstring(L, enuminf.strings + enuminf.strindex[i]);
            lua_rawseti(L, -2, ++value);
        }

        return 1;

    } else if (strcmp(index, "readonly") == 0) {
        lua_pushboolean(L, (mext->ext.flags & MIXF_WRITEABLE) == 0);
        return 1;
    }

    return 0;
}

static int luaA_mixer_ext_set(lua_State *L) {
    mixer_ext_t *mext = luaL_checkudata(L, 1, "mixer_ext");
    if ((mext->ext.flags & MIXF_WRITEABLE) == 0) return 0;

    const char* strindex = luaL_checkstring(L, 2);
    if (strcmp(strindex, "value") != 0) return 0;

    oss_mixer_enuminfo enuminf;
    int shift = 0, mask = 0, mono = 0, onoff = 0;
    int value, rvalue;

    switch (mext->ext.type) {
        case MIXT_SLIDER:
            mask = ~0; mono = 1; break;

        case MIXT_MONOSLIDER16:
        case MIXT_MONODB:
            mono = 1;
        case MIXT_STEREOSLIDER16:
        case MIXT_STEREODB:
            shift = 16; mask = 0xffff; break;

        case MIXT_MONOSLIDER:
        case MIXT_MONOPEAK:
        case MIXT_MONOVU:
            mono = 1;
        case MIXT_STEREOSLIDER:
        case MIXT_STEREOPEAK:
        case MIXT_STEREOVU:
            shift = 8; mask = 0xff; break;

        case MIXT_ONOFF:
        case MIXT_MUTE:
            onoff = 1;

        default:;
    }

    if (lua_istable(L, 3)) { // two channels
        if (!mask || mono) return 0;

        lua_rawgeti(L, 3, 1); // left channel
        lua_rawgeti(L, 3, 2); // right channel
        value = luaL_checknumber(L, -1); BOUND_CHECK(value, mext->ext.minvalue, mext->ext.maxvalue);
        rvalue = luaL_checknumber(L, -2); BOUND_CHECK(rvalue, mext->ext.minvalue, mext->ext.maxvalue);

        value = ((value & mask) << shift) | (rvalue & mask);
        lua_pop(L, 2);

    } else if (lua_isboolean(L, 3)) { // switch/mute
        if (!onoff) return 0;
        value = lua_toboolean(L, 3);

    } else if (lua_isnumber(L, 3)) { // single channel or set both channels to this value
        if (!mask) return 0;
        value = lua_tonumber(L, 3);
        BOUND_CHECK(value, mext->ext.minvalue, mext->ext.maxvalue);
        value = value & mask;
        if (!mono) value = (value << shift) | value;

    } else if (lua_isstring(L, 3)) { // set enum
        if (mext->ext.type != MIXT_ENUM) return 0;
        enuminf.dev = mext->ext.dev;
        enuminf.ctrl = mext->ext.ctrl;
        if (ioctl(mext->mixer->fh, SNDCTL_MIX_ENUMINFO, &enuminf) < 0) return 0;

        const char* strvalue = lua_tostring(L, 3);

        for (value = 0; value < mext->ext.maxvalue; value++) {
            if (!IN_ENUM(mext->ext.enum_present, value))
                continue;
            if (strcmp(enuminf.strings + enuminf.strindex[value], strvalue) == 0)
                break;
        }
        if (value == mext->ext.maxvalue) return 0;
    }

    write_mixer(mext, value);
    return 0;
}

static int luaA_mixer_ext_len(lua_State *L) {
    mixer_ext_t *mext = luaL_checkudata(L, 1, "mixer_ext");
    switch (mext->ext.type) {
        case MIXT_GROUP:
        case MIXT_DEVROOT:
            lua_pushnumber(L, mext->ext.update_counter);
            return 1;

        default:
            return 0;
    }
}

static const luaL_reg mixer_ext_meta[] = {
    {"__index", luaA_mixer_ext_get},
    {"__newindex", luaA_mixer_ext_set},
    /*{"__tostring", luaA_mixer_ext_name},*/
    {"__eq", luaA_mixer_ext_equal},
    {"__lt", luaA_mixer_ext_less},
    {"__gc", luaA_mixer_ext_gc},
    {"__len", luaA_mixer_ext_len},
    {"asstring", luaA_mixer_ext_string},
    {"astable", luaA_mixer_ext_table},
    {"mixer", luaA_mixer_ext_mixer},
    {"mixer_ext", luaA_mixer_ext_device},
    {NULL, NULL}
};

inline void luaA_newmetatable(lua_State *L, const char* name, const luaL_reg meta[]) {
	luaL_newmetatable(L, name);
	luaL_register(L, NULL, meta);
	lua_pop(L, 1);
}

LUALIB_API int luaopen_mixer (lua_State *L) {
    luaA_newmetatable(L, "mixer_ext", mixer_ext_meta);
    luaA_newmetatable(L, "mixer_dev", mixer_meta);
    luaL_register(L, "mixer", mixer_methods);
    return 1;
}
