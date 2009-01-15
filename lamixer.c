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
} lua_amixer_channel_name_t;

typedef struct {
	snd_mixer_t *hdl;
    /* char *devname; */
	int refcnt;
} lua_amixer_t;

typedef struct {
    long min; long dBmin;
    long max; long dBmax;
} lua_amixer_range_t;

typedef struct {
	lua_amixer_t *mixer;
    snd_mixer_elem_t *hdl;

#define LUAA_MIX_CAP_VOLUME   1
#define LUAA_MIX_CAP_SWITCH   2
#define LUAA_MIX_CAP_VJOINED  4
#define LUAA_MIX_CAP_SJOINED  8
#define LUAA_MIX_CAP_MONO     16
#define LUAA_MIX_CAP_ENUM     32

    short int pcaps;
    short int ccaps;
    lua_amixer_range_t prange;
    lua_amixer_range_t crange;
    int refcnt;
} lua_amixer_elem_t;

typedef struct {
    lua_amixer_elem_t *elem;
    snd_mixer_selem_channel_id_t hdl;
} lua_amixer_chan_t;

static lua_amixer_channel_name_t lua_amixer_channel_names[SND_MIXER_SCHN_LAST];

// }}}

// Helper functions {{{
/**
 * Initialize channel id -> name conversion array.
 * We need it b/c there's only snd_mixer_selem_channel_name() function
 * which allow us to get channel name by id, but we don't have any means
 * to get channel id by its name.
 * @return void
 * @internal
 */
static void amixer_init_chanid_cache()
{
    snd_mixer_selem_channel_id_t i;
    for (i = SND_MIXER_SCHN_FRONT_LEFT; i < SND_MIXER_SCHN_LAST; i++)
    {
        lua_amixer_channel_names[i].chan = i;
        lua_amixer_channel_names[i].name = snd_mixer_selem_channel_name(i);
    }
}

/**
 * Get channel numeric id by its name.
 * @see static void amixer_init_chanid_cache()
 * @internal
 * @param char *channame
 * @return snd_mixer_selem_channel_id_t
 */
static snd_mixer_selem_channel_id_t amixer_get_chanid_by_name(const char *channame)
{
    snd_mixer_selem_channel_id_t i;
    for (i = SND_MIXER_SCHN_FRONT_LEFT; i < SND_MIXER_SCHN_LAST; i++)
    {
        if (strcmp(channame, lua_amixer_channel_names[i].name) == 0)
            return i;
    }
}

/**
 * Find mixer element by its name.
 * @internal
 * @param snd_mixer_t *mixer
 * @param char *elemname
 * @return snd_mixer_elem_t*
 */
static snd_mixer_elem_t* amixer_find_selem(snd_mixer_t *mixer, const char *elemname)
{
    snd_mixer_elem_t *melem;
    snd_mixer_selem_id_t *id;

    if (snd_mixer_selem_id_malloc(&id) < 0)
        goto amixer_selem_failed;

    snd_mixer_selem_id_set_index(id, 0);
    snd_mixer_selem_id_set_name(id, elemname);

    melem = snd_mixer_find_selem(mixer, id);
    snd_mixer_selem_id_free(id);

    return melem;

amixer_selem_failed:
    return NULL;
}

/**
 * Mixer object destructor.
 * @internal
 * @param lua_amixer_t *mixer
 * @return void
 */
static void amixer_dtor(lua_amixer_t *mixer)
{
    mixer->refcnt--;
    if (mixer->refcnt < 1)
    {
        if (mixer->hdl)
            snd_mixer_close(mixer->hdl);
        free(mixer);
    }
}

/**
 * Mixer element object destructor.
 * @internal
 * @param lua_amixer_elem_t *elem
 * @return void
 */
static void amixer_elem_dtor(lua_amixer_elem_t *elem)
{
    /*amixer_dtor(elem->mixer);*/
    elem->refcnt--;
    if (elem->refcnt < 1)
    {
        if (elem->hdl)
            snd_mixer_elem_free(elem->hdl);
        free(elem);
    }
}

/**
 * Mixer channel object destructor.
 * @internal
 * @param lua_amixer_chan_t *chan
 * @return void
 */
static void amixer_chan_dtor(lua_amixer_chan_t *chan)
{
    //amixer_elem_dtor(chan->elem);
}

/**
 * Collect mixer element capabilities & put them into cache.
 * @internal
 * @param lua_amixer_elem_t *elem
 * @return void
 */
void amixer_fill_caps(lua_amixer_elem_t *elem)
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

/**
 * Construct new mixer element object.
 * - Wraps snd_mixer_elem_t into lua_amixer_elem_t structure,
 * - create new Lua userdata of type "amixer_elem" and links it to created struct,
 * - runs amixer_fill_caps() to collect element's capabilities.
 * @internal
 * @param lua_State *L
 * @param lua_amixer_t *mixer
 * @param snd_mixer_elem_t *melem
 * @return lua_amixer_elem_t*
 */
lua_amixer_elem_t *luaA_amixer_new_elem(lua_State *L, lua_amixer_t *mixer, snd_mixer_elem_t *melem)
{
    //int err;
    lua_amixer_elem_t *elem = NULL;
    lua_amixer_elem_t **elemptr;

    if (melem == NULL)
        goto amixer_elem_find_failed;

    elem = (lua_amixer_elem_t *)malloc(sizeof(lua_amixer_elem_t));
    if (elem == NULL)
        goto amixer_elem_malloc_failed;

    elem->hdl = melem;
    elem->mixer = mixer;
    elem->refcnt = 1;

    elem->pcaps = elem->ccaps = 0;
    elem->prange.min = elem->prange.max = 0;
    elem->crange.min = elem->crange.max = 0;
    elem->prange.dBmin = elem->prange.dBmax = 0;
    elem->crange.dBmin = elem->crange.dBmax = 0;
    amixer_fill_caps(elem);

    mixer->refcnt++;

    elemptr = lua_newuserdata(L, sizeof(lua_amixer_elem_t *));
    *elemptr = elem;
    luaA_settype(L, -2, "amixer_elem");

    return elem;

amixer_elem_malloc_failed:
    snd_mixer_elem_free(melem);

amixer_elem_find_failed:
    return NULL;
}

// }}}

// Mixer element object {{{
/**
 * Get next mixer element after this.
 * @param amixer_elem state - optional
 * @param amixer_elem elem
 * @return amixer_elem
 */
LUAA_FUNC(amixer_elem_next)
{
    int argc = lua_gettop(L);
    lua_amixer_elem_t **elemptr = luaL_checkudata(L, argc, "amixer_elem");

    return luaA_amixer_new_elem(L, (*elemptr)->mixer, snd_mixer_elem_next((*elemptr)->hdl)) != NULL;
}

/**
 * Get property value of mixer element object.
 * - number vol - first channel's volume, nil if there're no volume controls,
 * - number dB - first channel's dB gain, nil if there're no volume controls,
 * - boolean muted - true if element is muted, nil if element can't be muted,
 * - string name - element's name, readonly,
 * - number idx - element's index, readonly,
 * - amixer mixer - parent mixer object of this element, readonly,
 * - table volrange - two number elements: min & max volume values, nil if there're no volume controls,
 * - table dBrange - two number elements: min & max dB values, nil if there're no volume controls, readonly,
 * - boolean mono - true if element has mono channel, readonly,
 * - boolean playback - true if element has playback channels, readonly,
 * - boolean capture - true if element has capture channels, readonly.
 * @param amixer_elem elem
 * @param string index
 * @return mixed
 */
LUAA_FUNC(amixer_elem_index)
{
    luaA_checkmetaindex(L, "amixer_elem");
    lua_amixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");

    lua_amixer_t **mixptr;
    lua_amixer_chan_t *chan;
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
        mixptr = lua_newuserdata(L, sizeof(lua_amixer_t *));
        *mixptr = (*elemptr)->mixer;
        (*mixptr)->refcnt++;
    } else if (strcmp(index, "name") == 0) {
        lua_pushstring(L, snd_mixer_selem_get_name((*elemptr)->hdl));
    } else if (strcmp(index, "idx") == 0) {
        lua_pushnumber(L, snd_mixer_selem_get_index((*elemptr)->hdl));
    } else if (strcmp(index, "volrange") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
        {
            longval = (*elemptr)->prange.min;
            longvalx = (*elemptr)->prange.max;
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME) {
            longval = (*elemptr)->crange.min;
            longvalx = (*elemptr)->crange.max;
        } else {
            return 0;
        }

        lua_createtable(L, 2, 0);
        luaA_isettable(L, -2, 1, number, longval);
        luaA_isettable(L, -2, 2, number, longvalx);
    } else if (strcmp(index, "dBrange") == 0) {
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
        {
            longval = (*elemptr)->prange.dBmin;
            longvalx = (*elemptr)->prange.dBmax;
        } else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME) {
            longval = (*elemptr)->crange.dBmin;
            longvalx = (*elemptr)->crange.dBmax;
        } else {
            return 0;
        }

        lua_createtable(L, 2, 0);
        luaA_isettable(L, -2, 1, number, longval);
        luaA_isettable(L, -2, 2, number, longvalx);
    } else if (strcmp(index, "mono") == 0) {
        lua_pushboolean(L, ((*elemptr)->pcaps | (*elemptr)->ccaps) & LUAA_MIX_CAP_MONO);
    } else if (strcmp(index, "playback") == 0) {
        lua_pushboolean(L, (*elemptr)->pcaps);
    } else if (strcmp(index, "capture") == 0) {
        lua_pushboolean(L, (*elemptr)->ccaps);
    } else {
        chanid = amixer_get_chanid_by_name(index);
        chan = lua_newuserdata(L, sizeof(lua_amixer_chan_t));
        if (chan == NULL) return 0;
        chan->hdl = chanid;
        chan->elem = *elemptr;
        (*elemptr)->refcnt++;

        luaA_settype(L, -2, "amixer_chan");
    }
    return 1;
}

/**
 * Set property values for mixer element object.
 * @see amixer_elem_index
 * @param amixer_elem elem
 * @param string index
 * @param mixed value
 * @return void
 */
LUAA_FUNC(amixer_elem_newindex)
{
    lua_amixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");
    lua_amixer_chan_t *chan;
    snd_mixer_selem_channel_id_t chanid;
    const char *index = luaL_checkstring(L, 2);
    long longval;
    long longvalx;
    int intval;

    if (strcmp(index, "vol") == 0) {
        longval = lua_tonumber(L, 3);
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
            snd_mixer_selem_set_playback_volume_all((*elemptr)->hdl, longval);
        else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME)
            snd_mixer_selem_set_capture_volume_all((*elemptr)->hdl, longval);
    } else if (strcmp(index, "muted") == 0) {
        intval = lua_toboolean(L, 3) == 0;
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_SWITCH)
            snd_mixer_selem_set_playback_switch_all((*elemptr)->hdl, intval);
        else if ((*elemptr)->ccaps & LUAA_MIX_CAP_SWITCH)
            snd_mixer_selem_set_capture_switch_all((*elemptr)->hdl, intval);
    } else if (strcmp(index, "dB") == 0) {
        longval = lua_tonumber(L, 3);
        if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
            snd_mixer_selem_set_playback_dB_all((*elemptr)->hdl, longval, 1);
        else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME)
            snd_mixer_selem_set_capture_dB_all((*elemptr)->hdl, longval, 1);
    } else if (strcmp(index, "volrange") == 0) {
        if (lua_istable(L, 3))
        {
            luaA_igettable(L, 3, 1, number, longval);
            luaA_igettable(L, 3, 2, number, longvalx);
            if ((*elemptr)->pcaps & LUAA_MIX_CAP_VOLUME)
                snd_mixer_selem_set_playback_volume_range((*elemptr)->hdl, longval, longvalx);
            else if ((*elemptr)->ccaps & LUAA_MIX_CAP_VOLUME)
                snd_mixer_selem_set_capture_volume_range((*elemptr)->hdl, longval, longvalx);
        }
    }
}

/**
 * Destroy mixer element, free all its resources.
 * @param amixer_elem elem
 * @return void
 */
LUAA_FUNC(amixer_elem_close)
{
    lua_amixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");
    amixer_elem_dtor(*elemptr);
}

/**
 * Convert mixer element object into string.
 * @param amixer_elem elem
 * @return string
 */
LUAA_FUNC(amixer_elem_tostring)
{
    lua_amixer_elem_t **elemptr = luaL_checkudata(L, 1, "amixer_elem");
    lua_pushfstring(L, "[udata amixer_elem (%s:%d)]", snd_mixer_selem_get_name((*elemptr)->hdl), snd_mixer_selem_get_index((*elemptr)->hdl));
    return 1;
}
// }}}

// Mixer object methods {{{

/**
 * Open mixer object, initialize all necessary resources.
 * @param string devname
 * @return amixer
 */
LUAA_FUNC(amixer_open)
{
    const char *mixdev = luaL_checkstring(L, 1);
    snd_mixer_t *hdl;
    lua_amixer_t *mixer;
    lua_amixer_t **mixptr;
    //int err;

    if (snd_mixer_open(&hdl, 0) < 0)
        goto mixer_open_failed;

    if (snd_mixer_attach(hdl, mixdev) < 0
            || snd_mixer_selem_register(hdl, NULL, NULL) < 0
            || snd_mixer_load(hdl) < 0)
        goto mixer_alloc_failed;

    mixer = (lua_amixer_t *)malloc(sizeof(lua_amixer_t));
    if (mixer == NULL) goto mixer_alloc_failed;

    mixptr = lua_newuserdata(L, sizeof(lua_amixer_t *));
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

/**
 * Close mixer object, free all its resources.
 * @param amixer mixer
 * @return void
 */
LUAA_FUNC(amixer_close)
{
    lua_amixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    amixer_dtor(*mixptr);
}

/**
 * Get mixer element object by its name.
 * @param amixer mixer
 * @param string index - element's name
 * @return amixer_elem
 */
LUAA_FUNC(amixer_index)
{
    luaA_checkmetaindex(L, "amixer");
    lua_amixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    const char *index = luaL_checkstring(L, 2);

	return luaA_amixer_new_elem(L, *mixptr, amixer_find_selem((*mixptr)->hdl, index)) != NULL;
}

/**
 * Set mixer element's properties.
 * Depending on value type this method can do following things:
 * - number - set all element's channels volume to this value,
 * - table - assumed to be a table of "channel name/id" => "number/boolean" pairs,
 *   sets individual channel's volume/muted state,
 * - boolean - switch on/off all element's channels,
 * - amixer_elem - copy all settings from this element to designated one.
 * @todo make table value really work.
 * @param amixer mixer
 * @param string index - element's name
 * @param mixed value
 */
LUAA_FUNC(amixer_newindex)
{
    lua_amixer_t **mixptr = luaL_checkudata(L, 1, "amixer");
    const char *index = luaL_checkstring(L, 2);
    lua_amixer_elem_t **elemptr;
    snd_mixer_elem_t *elem;
    snd_mixer_selem_channel_id_t chanid;
    int intval;
    unsigned int uintval;
    long longval;

    elem = amixer_find_selem((*mixptr)->hdl, index);
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

/**
 * Iterator function: enumerate all mixer's elements.
 * @param amixer mixer
 * @return function {@see amixer_elem_next}, nil, amixer_elem
 */
LUAA_FUNC(amixer_each)
{
    lua_amixer_t **mixptr = luaL_checkudata(L, 1, "amixer");

    lua_pushcfunction(L, luaA_amixer_elem_next);
    lua_pushnil(L);

    if (luaA_amixer_new_elem(L, (*mixptr), snd_mixer_first_elem((*mixptr)->hdl)) == NULL)
    {
        lua_pop(L, 1);
        return 0;
    }
    
    return 3;
}

/**
 * Convert mixer object to string.
 * @param amixer mixer
 * @return string
 */
LUAA_FUNC(amixer_tostring)
{
    lua_amixer_elem_t **mixptr = luaL_checkudata(L, 1, "amixer");
    lua_pushfstring(L, "[udata amixer]");
    return 1;
}
// }}}

// Mixer channel methods {{{

/**
 * Convert mixer channel object to string.
 * @param amixer_chan chan
 * @return string
 */
LUAA_FUNC(amixer_chan_tostring)
{
    lua_amixer_chan_t *chan = luaL_checkudata(L, 1, "amixer_chan");
    lua_pushfstring(L, "[udata amixer_chan (%s)]", snd_mixer_selem_channel_name(chan->hdl));
    return 1;
}

/**
 * Get mixer channel's properties values.
 * - number vol - channel's volume,
 * - number dB - channel's dB gain,
 * - boolean muted - channel's muted state,
 * - string name - channel's name, readonly,
 * - number idx - channel's numeric id, readonly,
 * - amixer_elem elem - parent mixer element, readonly.
 * @param amixer_chan chan
 * @param string index
 * @return mixed
 */
LUAA_FUNC(amixer_chan_index)
{
    lua_amixer_chan_t *chan = luaL_checkudata(L, 1, "amixer_chan");
    lua_amixer_elem_t **elemptr;
    const char *index = luaL_checkstring(L, 2);
    long longval;
    int intval;

    if (strcmp(index, "vol") == 0) {
        if (chan->elem->pcaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_playback_volume(chan->elem->hdl, chan->hdl, &longval);
        } else if (chan->elem->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_capture_volume(chan->elem->hdl, chan->hdl, &longval);
        } else {
            return 0;
        }
        lua_pushnumber(L, longval);
    } else if (strcmp(index, "dB") == 0) {
        if (chan->elem->pcaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_playback_dB(chan->elem->hdl, chan->hdl, &longval);
        } else if (chan->elem->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_get_capture_dB(chan->elem->hdl, chan->hdl, &longval);
        } else {
            return 0;
        }
        lua_pushnumber(L, longval);
    } else if (strcmp(index, "muted") == 0) {
        if (chan->elem->pcaps & LUAA_MIX_CAP_SWITCH) {
            snd_mixer_selem_get_playback_switch(chan->elem->hdl, chan->hdl, &intval);
        } else if (chan->elem->ccaps & LUAA_MIX_CAP_SWITCH) {
            snd_mixer_selem_get_capture_switch(chan->elem->hdl, chan->hdl, &intval);
        } else {
            return 0;
        }
        lua_pushboolean(L, intval == 0);
    } else if (strcmp(index, "name") == 0) {
        lua_pushstring(L, snd_mixer_selem_channel_name(chan->hdl));
    } else if (strcmp(index, "idx") == 0) {
        lua_pushnumber(L, chan->hdl);
    } else if (strcmp(index, "elem") == 0) {
        elemptr = lua_newuserdata(L, sizeof(lua_amixer_elem_t));    
        *elemptr = chan->elem;
        luaA_settype(L, -2, "amixer_elem");
        chan->elem->refcnt++;
    } else {
        return 0;
    }

    return 1;
}

/**
 * Set mixer channel's properties values.
 * @see amixer_chan_index
 * @param amixer_chan chan
 * @param string index
 * @param mixed value
 * @return void
 */
LUAA_FUNC(amixer_chan_newindex)
{
    lua_amixer_chan_t *chan = luaL_checkudata(L, 1, "amixer_chan");
    lua_amixer_elem_t **elemptr;
    const char *index = luaL_checkstring(L, 2);
    long longval;
    int intval;

    if (strcmp(index, "vol") == 0) {
        longval = luaL_checknumber(L, 3);
        if (chan->elem->pcaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_set_playback_volume(chan->elem->hdl, chan->hdl, longval);
        } else if (chan->elem->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_set_capture_volume(chan->elem->hdl, chan->hdl, longval);
        }
    } else if (strcmp(index, "dB") == 0) {
        longval = luaL_checknumber(L, 3);
        if (chan->elem->pcaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_set_playback_dB(chan->elem->hdl, chan->hdl, longval, 1);
        } else if (chan->elem->ccaps & LUAA_MIX_CAP_VOLUME) {
            snd_mixer_selem_set_capture_dB(chan->elem->hdl, chan->hdl, longval, 1);
        }
    } else if (strcmp(index, "muted") == 0) {
        intval = lua_toboolean(L, 3) == 0;
        if (chan->elem->pcaps & LUAA_MIX_CAP_SWITCH) {
            snd_mixer_selem_set_playback_switch(chan->elem->hdl, chan->hdl, intval);
        } else if (chan->elem->ccaps & LUAA_MIX_CAP_SWITCH) {
            snd_mixer_selem_set_capture_switch(chan->elem->hdl, chan->hdl, intval);
        }
    }

    return 0;
}

/**
 * Destructs mixer channel object.
 * @param amixer_chan chan
 * @return void
 */
LUAA_FUNC(amixer_chan_close)
{
    lua_amixer_chan_t *chan = luaL_checkudata(L, 1, "amixer_chan");
    amixer_chan_dtor(chan);
}

// }}}

static const luaL_reg amixer_methods[] = {
	{"open", luaA_amixer_open},
	{"close", luaA_amixer_close},
	{NULL, NULL}
};

static const luaL_reg amixer_meta[] = {
    {"__index", luaA_amixer_index},
    {"__newindex", luaA_amixer_newindex},
    {"__gc", luaA_amixer_close},
    {"__tostring", luaA_amixer_tostring},

    {"each", luaA_amixer_each},
	{NULL, NULL}
};

static const luaL_reg amixer_elem_meta[] = {
    {"__index", luaA_amixer_elem_index},
    {"__newindex", luaA_amixer_elem_newindex},
    {"__gc", luaA_amixer_elem_close},
    {"__tostring", luaA_amixer_elem_tostring},

    {"next", luaA_amixer_elem_next},
    {NULL, NULL}
};

static const luaL_reg amixer_chan_meta[] = {
    {"__index", luaA_amixer_chan_index},
    {"__newindex", luaA_amixer_chan_newindex},
    {"__gc", luaA_amixer_chan_close},
    {"__tostring", luaA_amixer_chan_tostring},

    {NULL, NULL}
};

LUALIB_API int luaopen_amixer(lua_State *L)
{
    amixer_init_chanid_cache();

    luaA_deftype(L, amixer_elem);
    luaA_deftype(L, amixer_chan);

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
