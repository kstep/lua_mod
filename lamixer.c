// Includes {{{
#include <alsa/asoundlib.h>
#include <stdlib.h>

#include <lua.h>
#include <lauxlib.h>

#ifdef DEBUG
#include <err.h>
#else
#define warn(fmt, ...)
#endif

#include "luahelper.h"
// }}}

// Comments {{{
/**
 * We're using simple mixer interface here.
 * Sequence:
 * mixer.open() => snd_mixer_open() => mixerobj
 * mixer.close() => snd_mixer_close()
 *
 * mixerobj[name] => snd_mixer_selem_id_malloc(), snd_mixer_selem_id_set_name(), snd_mixer_find_selem(), snd_mixer_selem_id_free() => mixerdevobj
 * mixerobj:each() => snd_mixer_[first|last]_elem(), snd_mixer_elem_[next|prev]()
 * mixerelemobj.name => snd_mixer_selem_get_name()
 * mixerelemobj.vol => snd_mixer_selem_[gs]et_[playback|capture]_volume_all
 * mixerelemobj.dB => snd_mixer_selem_[gs]et_[playback|capture]_volume_dB_all
 * mixerelemobj.muted => snd_mixer_selem_[gs]et_[playback|capture]_switch_all
 * mixerelemobj.mixer => parent mixerobj
 * mixerelemobj.idx => snd_mixer_selem_get_index()
 * mixerelemobj.volrange => snd_mixer_selem_[gs]et_[playback|capture]_volume_range() => table of int (min, max)
 * mixerelemobj.dBrange => snd_mixer_selem_[gs]et_[playback|capture]_dB_range() => table of int (min, max)
 * mixerelemobj[num] => mixerchanobj
 *
 * mixerchanobj.muted => snd_mixer_selem_[gs]et_[playback|capture]_switch()
 * mixerchanobj.vol => snd_mixer_selem_[gs]et_[playback|capture]_volume()
 * mixerchanobj.dB => snd_mixer_selem_[gs]et_[playback|capture]_volume_dB()
 * mixerchanobj.name => snd_mixer_selem_channel_name()
 * mixerchanobj.idx => number of channel
 * mixerchanobj.elem => parent mixerdevobj
 * mixerchanobj.mixer => parent mixerobj
 *
 * mixerobj[name] = table of int => set channels of this elem
 * mixerobj[name] = int => set both channels of this elem
 * mixerobj[name] = bool => switch mute on/off for this elem
 * mixerobj[name] = mixerdevobj => set this elem to the same state as mixerdevobj elem
 *
 * mixerdevobj[num] = int => set volume for this channel
 * mixerdevobj[num] = bool => switch mute on/off for this channel
 * mixerdevobj[num] = mixerchanobj => set this channel to the same state as mixerchanobj channel
 *
 * to operate with mixer / elem / channel we need:
 * snd_mixer_elem_t for most of time operating with elem,
 * snd_mixer_t for operating with mixer, while spawning elems,
 * snd_mixer_selem_channel_id_t for most of time operating with channel.
 *
 * We can also cache capabilities for mixerdevobj:
 * snd_mixer_selem_has_[playback|capture]_channel(),
 * snd_mixer_selem_has_[common|playback|capture]_volume(),
 * snd_mixer_selem_has_[playback|capture]_volume_joined(),
 * snd_mixer_selem_is_active(),
 * snd_mixer_selem_is_[playback|capture]_mono(),
 * snd_mixer_selem_has_[common|playback|capture]_switch(),
 * snd_mixer_selem_has_[playback|capture]_switch_joined(),
 *
 * mixerobj & mixerdevobj need refcount.
 */
// }}}

// Typedefs & structures {{{

typedef struct {
    snd_mixer_selem_channel_id_t chan;
    char *name;
} lua_mixer_channel_name_t;

typedef struct {
	snd_mixer_t *hdl;
    /* char *devname; */
	int refcnt;
} lua_mixer_t;

typedef struct {
    long min; long dBmin;
    long max; long dBmax;
} lua_mixer_range_t;

typedef struct {
	lua_mixer_t *mixer;
    snd_mixer_elem_t *hdl;

#define LUAA_MIX_CAP_VOLUME   1
#define LUAA_MIX_CAP_SWITCH   2
#define LUAA_MIX_CAP_VJOINED  4
#define LUAA_MIX_CAP_SJOINED  8
#define LUAA_MIX_CAP_MONO     16
#define LUAA_MIX_CAP_ENUM     32

    int pcaps;
    int ccaps;
    lua_mixer_range_t prange;
    lua_mixer_range_t crange;
    int refcnt;
} lua_mixer_elem_t;

typedef struct {
    lua_mixer_elem_t *elem;
    snd_mixer_selem_channel_id_t chan;
} lua_mixer_chan_t;

static lua_mixer_channel_name_t lua_mixer_channel_names[SND_MIXER_SCHN_LAST];

// }}}

// Helper functions {{{
void mixer_init_chanid_cache()
{
    snd_mixer_selem_channel_id_t i;
    for (i = SND_MIXER_SCHN_FRONT_LEFT; i < SND_MIXER_SCHN_LAST; i++)
    {
        lua_mixer_channel_names[i].chan = i;
        lua_mixer_channel_names[i].name = snd_mixer_selem_channel_name(i);
    }
}

snd_mixer_selem_channel_id_t mixer_get_chanid_by_name(const char *channame)
{
    snd_mixer_selem_channel_id_t i;
    for (i = SND_MIXER_SCHN_FRONT_LEFT; i < SND_MIXER_SCHN_LAST; i++)
    {
        if (strcmp(channame, lua_mixer_channel_names[i].name) == 0)
            return i;
    }
}

/**
 * Найти элемент миксера по имени и вернуть его
 */
snd_mixer_elem_t* mixer_find_selem(snd_mixer_t *mixer, const char *elemname)
{
    snd_mixer_elem_t *melem;
    snd_mixer_selem_id_t *id;

    if (snd_mixer_selem_id_malloc(&id) < 0)
        goto mixer_selem_failed;

    snd_mixer_selem_id_set_index(id, 0);
    snd_mixer_selem_id_set_name(id, elemname);

    melem = snd_mixer_find_selem(mixer, id);
    snd_mixer_selem_id_free(id);

    return melem;

mixer_selem_failed:
    return NULL;
}

/**
 * Создать новую структуру-обёртку для элемента миксера по его имени
 */
lua_mixer_elem_t *mixer_new_elem(lua_mixer_t *mixer, const char *elemname)
{
    //int err;
    lua_mixer_elem_t *elem = NULL;
    snd_mixer_elem_t *melem;

    melem = mixer_find_selem(mixer->hdl, elemname);
    if (melem == NULL)
        goto mixer_elem_find_failed;

    elem = (lua_mixer_elem_t *)malloc(sizeof(lua_mixer_elem_t));
    if (elem == NULL)
        goto mixer_elem_malloc_failed;

    elem->hdl = melem;
    elem->mixer = mixer;
    elem->refcnt = 1;

    elem->pcaps = elem->ccaps = 0;
    elem->prange.min = elem->prange.max = 0;
    elem->crange.min = elem->crange.max = 0;
    elem->prange.dBmin = elem->prange.dBmax = 0;
    elem->crange.dBmin = elem->crange.dBmax = 0;

    mixer->refcnt++;

    return elem;

mixer_elem_malloc_failed:
    snd_mixer_elem_free(melem);

mixer_elem_find_failed:
    return NULL;
}

/**
 * Прочитать свойтсва элемента миксера и закешировать их
 */
void mixer_fill_caps(lua_mixer_elem_t *elem)
{
    if (snd_mixer_selem_has_playback_volume(elem->hdl))
    {
        elem->pcaps |= LUAA_MIX_CAP_VOLUME;
        if (snd_mixer_selem_has_playback_volume_joined(elem->hdl))
            elem->pcaps |= LUAA_MIX_CAP_VJOINED;
        snd_mixer_selem_get_playback_volume_range(elem->hdl, &(elem->prange.min), &(elem->prange.max));
    }
    if (snd_mixer_selem_has_playback_switch(elem->hdl))
    {
        elem->pcaps |= LUAA_MIX_CAP_SWITCH;
        if (snd_mixer_selem_has_playback_switch_joined(elem->hdl))
            elem->pcaps |= LUAA_MIX_CAP_SJOINED;
    }
    if (snd_mixer_selem_is_playback_mono(elem->hdl))
        elem->pcaps |= LUAA_MIX_CAP_MONO;
    if (snd_mixer_selem_is_enum_playback(elem->hdl))
        elem->pcaps |= LUAA_MIX_CAP_ENUM;

    if (snd_mixer_selem_has_capture_volume(elem->hdl))
    {
        elem->ccaps |= LUAA_MIX_CAP_VOLUME;
        if (snd_mixer_selem_has_capture_volume_joined(elem->hdl))
            elem->ccaps |= LUAA_MIX_CAP_VJOINED;
        snd_mixer_selem_get_capture_volume_range(elem->hdl, &(elem->crange.min), &(elem->crange.max));
        snd_mixer_selem_get_capture_dB_range(elem->hdl, &(elem->crange.dBmin), &(elem->crange.dBmax));
    }
    if (snd_mixer_selem_has_capture_switch(elem->hdl))
    {
        elem->ccaps |= LUAA_MIX_CAP_SWITCH;
        if (snd_mixer_selem_has_capture_switch_joined(elem->hdl))
            elem->ccaps |= LUAA_MIX_CAP_SJOINED;
    }
    if (snd_mixer_selem_is_capture_mono(elem->hdl))
        elem->ccaps |= LUAA_MIX_CAP_MONO;
    if (snd_mixer_selem_is_enum_capture(elem->hdl))
        elem->ccaps |= LUAA_MIX_CAP_ENUM;
}
// }}}

// Mixer element object {{{
LUAA_FUNC(mixer_elem_next)
{
    lua_mixer_elem_t **elemptr = luaL_checkudata(L, 2, "amixer_elem");
}


LUAA_FUNC(mixer_elem_index)
{
    luaA_checkmetaindex(L, "amixer_elem");
    lua_mixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");

    lua_mixer_t **mixptr;
    lua_mixer_chan_t *chan;
    snd_mixer_selem_channel_id_t chanid;
    const char *index = luaL_checkstring(L, 2);
    long longval;
    long longvalx;
    int intval;

    if (strcmp(index, "vol") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
        {
            snd_mixer_selem_get_playback_volume((*elemptr)->hdl, SND_MIXER_SCHN_FRONT_LEFT, &longval);
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_capture_volume((*elemptr)->hdl, SND_MIXER_SCHN_FRONT_LEFT, &longval);
        } else {
            return 0;
        }
        lua_pushnumber(L, longval);
    } else if (strcmp(index, "muted") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_SWITCH)
        {
            snd_mixer_selem_get_playback_switch((*elemptr)->hdl, SND_MIXER_SCHN_FRONT_LEFT, &intval);
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_SWITCH) {
            snd_mixer_selem_get_capture_switch((*elemptr)->hdl, SND_MIXER_SCHN_FRONT_LEFT, &intval);
        } else {
            return 0;
        }
        lua_pushboolean(L, intval == 0);
    } else if (strcmp(index, "dB") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
        {
            snd_mixer_selem_get_playback_dB((*elemptr)->hdl, SND_MIXER_SCHN_FRONT_LEFT, &longval);
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_capture_dB((*elemptr)->hdl, SND_MIXER_SCHN_FRONT_LEFT, &longval);
        } else {
            return 0;
        }
        lua_pushnumber(L, longval);
    } else if (strcmp(index, "mixer") == 0) {
        mixptr = lua_newuserdata(L, sizeof(lua_mixer_t *));
        *mixptr = (*elemptr)->mixer;
        (*mixptr)->refcnt++;
    } else if (strcmp(index, "name") == 0) {
        lua_pushstring(L, snd_mixer_selem_get_name((*elemptr)->hdl));
    } else if (strcmp(index, "idx") == 0) {
        lua_pushnumber(L, snd_mixer_selem_get_index((*elemptr)->hdl));
    } else if (strcmp(index, "volrange") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
        {
            snd_mixer_selem_get_playback_volume_range((*elemptr)->hdl, &longval, &longvalx);
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_capture_volume_range((*elemptr)->hdl, &longval, &longvalx);
        } else {
            return 0;
        }

        lua_createtable(L, 2, 0);
        luaA_isettable(L, -2, 1, number, longval);
        luaA_isettable(L, -2, 2, number, longvalx);
    } else if (strcmp(index, "dBrange") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
        {
            snd_mixer_selem_get_playback_dB_range((*elemptr)->hdl, &longval, &longvalx);
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_capture_dB_range((*elemptr)->hdl, &longval, &longvalx);
        } else {
            return 0;
        }

        lua_createtable(L, 2, 0);
        luaA_isettable(L, -2, 1, number, longval);
        luaA_isettable(L, -2, 2, number, longvalx);
    } else if (strcmp(index, "mono") == 0) {
        lua_pushboolean(L, ((*elemptr)->pcaps | (*elemptr)->ccaps) & LUAA_MIX_CAP_MONO);
    } else if (strcmp(index, "joined") == 0) {
        lua_pushboolean(L, ((*elemptr)->pcaps | (*elemptr)->ccaps) & (LUAA_MIX_CAP_VJOINED | LUAA_MIX_CAP_SJOINED));
    } else if (strcmp(index, "playback") == 0) {
        if ((*elemptr)->pcaps) {
            lua_pushboolean(L, 1);
        } else if ((*elemptr)->ccaps) {
            lua_pushboolean(L, 0);
        } else {
            return 0;
        }
    } else {
        chanid = mixer_get_chanid_by_name(index);
        chan = lua_newuserdata(L, sizeof(lua_mixer_chan_t));
        if (chan == NULL) return 0;
        chan->chan = chanid;
        chan->elem = *elemptr;
        (*elemptr)->refcnt++;
    }
    return 1;
}

LUAA_FUNC(mixer_elem_newindex)
{
    lua_mixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");
    lua_mixer_chan_t *chan;
    snd_mixer_selem_channel_id_t chanid;
    const char *index = luaL_checkstring(L, 2);
    long longval;
    long longvalx;
    int intval;

    if (strcmp(index, "vol") == 0) {
        longval = lua_tonumber(L, 3);
        snd_mixer_selem_set_playback_volume_all((*elemptr)->hdl, longval);
    } else if (strcmp(index, "muted") == 0) {
        intval = lua_toboolean(L, 3) == 0;
        snd_mixer_selem_set_playback_switch_all((*elemptr)->hdl, intval);
    } else if (strcmp(index, "dB") == 0) {
        longval = lua_tonumber(L, 3);
        snd_mixer_selem_set_playback_dB_all((*elemptr)->hdl, longval, 1);
    } else if (strcmp(index, "volrange") == 0) {
    } else if (strcmp(index, "dBrange") == 0) {
    } else {
    }
}

LUAA_FUNC(mixer_elem_close)
{
    lua_mixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");
    if (--(*elemptr)->refcnt < 1)
    {
        snd_mixer_elem_free((*elemptr)->hdl);
        free(*elemptr);
    }
}

LUAA_FUNC(mixer_elem_tostring)
{
    lua_mixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");
    lua_pushfstring(L, "amixer_elem [%s:%d]", snd_mixer_selem_get_name((*elemptr)->hdl), snd_mixer_selem_get_index((*elemptr)->hdl));
    return 1;
}
// }}}

// Mixer object methods {{{
LUAA_FUNC(mixer_open)
{
    const char *mixdev = luaL_checkstring(L, 1);
    snd_mixer_t *hdl;
    lua_mixer_t *mixer;
    lua_mixer_t **mixptr;
    //int err;

    if (snd_mixer_open(&hdl, 0) < 0)
        goto mixer_open_failed;

    if (snd_mixer_attach(hdl, mixdev) < 0
            || snd_mixer_selem_register(hdl, NULL, NULL) < 0
            || snd_mixer_load(hdl) < 0)
        goto mixer_alloc_failed;

    mixer = (lua_mixer_t *)malloc(sizeof(lua_mixer_t));
    if (mixer == NULL) goto mixer_alloc_failed;

    mixptr = lua_newuserdata(L, sizeof(lua_mixer_t *));
    if (mixptr == NULL) goto mixer_udata_failed;
    *mixptr = mixer; 

    mixer->hdl = hdl;
    mixer->refcnt = 1;

    luaA_settype(L, -2, "amixer");
    return 1;

mixer_udata_failed:
    free(hdl);

mixer_alloc_failed:
    snd_mixer_close(hdl);

mixer_open_failed:
    return 0;
}

LUAA_FUNC(mixer_close)
{
    lua_mixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    if (--(*mixptr)->refcnt < 1)
    {
        snd_mixer_close((*mixptr)->hdl);
        free(*mixptr);
    }
}

/**
 * Конструируем луа-объект «элемент миксера»
 */
LUAA_FUNC(mixer_index)
{
    luaA_checkmetaindex(L, "amixer");
    lua_mixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    const char *index = luaL_checkstring(L, 2);
    lua_mixer_elem_t *elem;
    lua_mixer_elem_t **elemptr;

	elem = mixer_new_elem(*mixptr, index);
    if (elem == NULL)
        goto mixer_elem_new_failed;

    elemptr = lua_newuserdata(L, sizeof(lua_mixer_elem_t *));
    if (elemptr == NULL)
        goto mixer_elem_udata_failed;

    mixer_fill_caps(elem);
    *elemptr = elem;
    luaA_settype(L, -2, "amixer_elem");

    return 1;

mixer_elem_udata_failed:
    snd_mixer_elem_free(elem->hdl);
    free(elem);

mixer_elem_new_failed:
    return 0;
}

// amixer_elem, int, table of int, bool
LUAA_FUNC(mixer_newindex)
{
    lua_mixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    const char *index = luaL_checkstring(L, 2);
    lua_mixer_elem_t **elemptr;
    snd_mixer_elem_t *elem;
    snd_mixer_selem_channel_id_t chanid;
    int intval;
    unsigned int uintval;
    long longval;

    elem = mixer_find_selem((*mixptr)->hdl, index);
    if (elem == NULL) return 0;

    if (lua_isnumber(L, 3)) { // set volume of all channels
        longval = lua_tonumber(L, 3);
        if (snd_mixer_selem_has_playback_volume(elem))
            snd_mixer_selem_set_playback_volume_all(elem, longval);
        if (snd_mixer_selem_has_capture_volume(elem))
            snd_mixer_selem_set_capture_volume_all(elem, longval);
    } else if (lua_istable(L, 3)) { // @todo configure individual channels
    } else if (lua_isboolean(L, 3)) { // switch all channels on/off
        intval = lua_toboolean(L, 3);
        if (snd_mixer_selem_has_playback_switch(elem))
            snd_mixer_selem_set_playback_switch_all(elem, intval);
        if (snd_mixer_selem_has_capture_switch(elem))
            snd_mixer_selem_set_capture_switch_all(elem, intval);
        snd_mixer_elem_free(elem);
    } else {
        elemptr = luaL_checkudata(L, 3, "amixer_elem");
        for (chanid = SND_MIXER_SCHN_FRONT_LEFT; chanid <= SND_MIXER_SCHN_REAR_CENTER; chanid++)
        {
            if (snd_mixer_selem_has_playback_channel(elem, chanid)
                    && snd_mixer_selem_has_playback_channel((*elemptr)->hdl, chanid))
            {
                snd_mixer_selem_get_playback_volume((*elemptr)->hdl, chanid, &longval);
                snd_mixer_selem_get_playback_switch((*elemptr)->hdl, chanid, &intval);

                snd_mixer_selem_set_playback_volume(elem, chanid, longval);
                snd_mixer_selem_set_playback_switch(elem, chanid, intval);
            }
            if (snd_mixer_selem_has_capture_channel(elem, chanid)
                    && snd_mixer_selem_has_capture_channel((*elemptr)->hdl, chanid))
            {
                snd_mixer_selem_get_capture_volume((*elemptr)->hdl, chanid, &longval);
                snd_mixer_selem_get_capture_switch((*elemptr)->hdl, chanid, &intval);

                snd_mixer_selem_set_capture_volume(elem, chanid, longval);
                snd_mixer_selem_set_capture_switch(elem, chanid, intval);
            }
            snd_mixer_selem_get_enum_item((*elemptr)->hdl, chanid, &uintval);
            snd_mixer_selem_set_enum_item(elem, chanid, uintval);
        }
    }

    snd_mixer_elem_free(elem);
}

LUAA_FUNC(mixer_each)
{
    lua_mixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    lua_pushcfunction(L, luaA_mixer_elem_next);
    /*lua_mixer_elem_t *elem = luaA_mixer_new_elem(L, , );*/
    /*elem->hdl =*/
    lua_pushcfunction(L, luaA_mixer_elem_next);
    return 3;
}

LUAA_FUNC(mixer_tostring)
{
    lua_mixer_elem_t **mixptr = luaL_checkudata(L, 1, "amixer");
    lua_pushfstring(L, "amixer");
    return 1;
}
// }}}


static const luaL_reg amixer_methods[] = {
	{"open", luaA_mixer_open},
	{"close", luaA_mixer_close},
	{NULL, NULL}
};

static const luaL_reg amixer_meta[] = {
    {"__index", luaA_mixer_index},
    {"__newindex", luaA_mixer_newindex},
    {"__gc", luaA_mixer_close},
    {"__tostring", luaA_mixer_tostring},

    {"each", luaA_mixer_each},
	{NULL, NULL}
};

static const luaL_reg amixer_elem_meta[] = {
    {"__index", luaA_mixer_elem_index},
    {"__newindex", luaA_mixer_elem_newindex},
    {"__gc", luaA_mixer_elem_close},
    {"__tostring", luaA_mixer_elem_tostring},

    {"next", luaA_mixer_elem_next},
    {NULL, NULL}
};

LUALIB_API int luaopen_amixer(lua_State *L)
{
    luaA_deftype(L, amixer_elem);

    luaL_newmetatable(L, "amixer");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

    luaL_register(L, NULL, amixer_meta);
    luaL_register(L, "amixer", amixer_methods);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);
    lua_pop(L, 2);

    return 0;
}
