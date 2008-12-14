// includes {{{
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>

#include "luahelper.h"
// }}}

// macro definitions {{{
#define MAXCNT 50

#define DO_SIMPLE_MPD_CMD(func, cmd) \
	static int luaA_mpdc_##func (lua_State *L) { \
		mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client"); \
		return luaA_mpdc_command(L, mpdc->sh, cmd "\n"); }


#define DO_SET_MPD_CMD(func, cmd) \
	static int luaA_mpdc_##func (lua_State *L) { \
		mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client"); \
		int value = luaL_checknumber(L, 2); \
		return luaA_mpdc_command(L, mpdc->sh, cmd " %d\n", value); }
// }}}

// typedef {{{
typedef struct {
	struct sockaddr_in addr;
	int sh;
} mpdc_type;
typedef mpdc_type* mpdc_type_t;
// }}

// open & close {{{

static int luaA_mpdc_connect(struct sockaddr_in *addr) {
	int sh;

	sh = socket(PF_INET, SOCK_STREAM, 6);
	if (sh < 0 || connect(sh, (struct sockaddr *)addr, sizeof(struct sockaddr_in)) < 0) {
		return -1;
	}

	char buf[BUFSIZ];
	size_t recvsz;
	if ((recvsz = recv(sh, buf, BUFSIZ, 0)) < 0 || recvsz < 8 || strncmp("OK MPD ", buf, 7) != 0) {
		close(sh);
		return -2;
	}

	return sh;
}

static int luaA_mpdc_open(lua_State *L) {
	const char* host = luaL_checkstring(L, 1);
	int port = luaL_checknumber(L, 2);

	struct hostent *peer = gethostbyname(host);

	if (peer == NULL)
		return 0; //luaL_error(L, "unknown host %s", host);

	mpdc_type_t mpdc = lua_newuserdata(L, sizeof(mpdc_type));
	mpdc->addr.sin_family = AF_INET;
	mpdc->addr.sin_port = htons(port);
	memcpy((char *)&(mpdc->addr).sin_addr.s_addr, (char *)peer->h_addr_list[0], peer->h_length);
	
	mpdc->sh = luaA_mpdc_connect(&(mpdc->addr));
	/*if (mpdc->sh < 0) {
		lua_pop(L, 1);
		return 0; //luaL_error(L, (mpdc->sh == -1)? "unable to connect to host %s:%d": "unable to find MPD at host %s:%d", host, port);
	}*/

	luaA_settype(L, -2, "mpd_client");
	return 1;
}

static int luaA_mpdc_close(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	if (mpdc->sh >= 0) close(mpdc->sh);
	return 0;
}

static int luaA_mpdc_reconnect(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	if (mpdc->sh >= 0) close(mpdc->sh);

	mpdc->sh = luaA_mpdc_connect(&(mpdc->addr));
	lua_pushboolean(L, mpdc->sh >= 0);
	return 1;
}
// }}}

// conversion functions {{{
static int luaA_mpdc_command(lua_State *L, int mpdc, const char *cmd, ...) {
	char buf[BUFSIZ];
	int cnt = 0;
	ssize_t recvsz;
	va_list vargs;
	va_start(vargs, cmd);
	recvsz = vsnprintf(buf, BUFSIZ, cmd, vargs);
	va_end(vargs);

	if (send(mpdc, buf, recvsz, 0) < 0) return 0;
	while ((recvsz = recv(mpdc, buf, BUFSIZ, 0)) > 0) {
		if (cnt++ > MAXCNT) {
			lua_concat(L, cnt - 1);
			cnt = 2;
		}

		if (strncmp(buf + recvsz - 3, "OK\n", 3) == 0) {
			lua_pushlstring(L, buf, recvsz - 3);
			break;
		} else if (cnt == 1 && strncmp(buf, "ACK [", 5) == 0) {
			lua_pushlstring(L, buf + 4, recvsz - 5);
			return lua_error(L);
		} else {
			lua_pushlstring(L, buf, recvsz);
		}

	}

	if (cnt > 1) {
		lua_concat(L, cnt);
		cnt = 1;
	}

	return cnt;
}

static int luaA_mpdc_string_to_table(lua_State *L) {
	size_t len;
	const char* buf = lua_tolstring(L, -1, &len);
	char *ptr = (char *)buf, *eol, *eon;

	lua_newtable(L);

	while (len > 0 && (eon = strchr(ptr, ':'))) {
		eol = strchr(eon, '\n');
		lua_pushlstring(L, ptr, eon - ptr);
		lua_pushlstring(L, eon + 2, eol - eon - 2);
		lua_settable(L, -3);
		len -= eol - ptr + 1;
		ptr = eol + 1;
	}
	lua_remove(L, -2);
	return 1;
}

static int luaA_mpdc_string_to_list_of_tables(lua_State *L, const char *firstname) {
	size_t len;
	const char* buf = lua_tolstring(L, -1, &len);
	char *ptr = (char *)buf, *eol, *eon;
	int i = 0;

	lua_newtable(L);

	while (len > 0 && (eon = strchr(ptr, ':'))) {
		if (strncmp(ptr, firstname, eon - ptr) == 0) {
			if (i > 0)
				lua_settable(L, -3);

			lua_pushnumber(L, ++i);
			lua_newtable(L);
		}

		eol = strchr(eon, '\n');

		lua_pushlstring(L, ptr, eon - ptr);
		lua_pushlstring(L, eon + 2, eol - eon - 2);
		lua_settable(L, -3);

		len -= eol - ptr + 1;
		ptr = eol + 1;
	}
	lua_settable(L, -3);
	lua_remove(L, -2);
	return 1;
}

static int luaA_mpdc_string_to_list(lua_State *L, const char *filter) {
	size_t len;
	const char* buf = lua_tolstring(L, -1, &len);
	char *ptr = (char *)buf, *eol, *eon;
	int i = 0;

	lua_newtable(L);

	while (len > 0 && (eon = strchr(ptr, ':'))) {
		eol = strchr(eon, '\n');
		if (strncmp(ptr, filter, eon - ptr) == 0) {
			lua_pushnumber(L, ++i);
			lua_pushlstring(L, eon + 2, eol - eon - 2);
			lua_settable(L, -3);
		}
		len -= eol - ptr + 1;
		ptr = eol + 1;
	}
	lua_remove(L, -2);
	return 1;
}
// }}}

// get current status {{{
static int luaA_mpdc_current_song(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	if (luaA_mpdc_command(L, mpdc->sh, "currentsong\n"))
		return luaA_mpdc_string_to_table(L);
	return 0;
}

static int luaA_mpdc_status(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	if (luaA_mpdc_command(L, mpdc->sh, "status\n"))
		return luaA_mpdc_string_to_table(L);
	return 0;
}
// }}}

// songs listing {{{
static int luaA_mpdc_list_all_songs(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	const char* filter = lua_tostring(L, 2);

	if (luaA_mpdc_command(L, mpdc->sh, "listall %s\n", filter == NULL? "": filter))
		return luaA_mpdc_string_to_list(L, "file");
	return 0;
}

static int luaA_mpdc_list_songs_by_id(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	int songid = lua_tonumber(L, 2);
	int result = songid?
				 luaA_mpdc_command(L, mpdc->sh, "playlistid %d\n", songid)
				:luaA_mpdc_command(L, mpdc->sh, "playlistid\n");

	if (result)
		return luaA_mpdc_string_to_list_of_tables(L, "file");
	return 0;
}
// }}}

// playback control {{{
DO_SIMPLE_MPD_CMD(shuffle, "shuffle")
DO_SIMPLE_MPD_CMD(play, "play")
DO_SIMPLE_MPD_CMD(stop, "stop")
DO_SIMPLE_MPD_CMD(toggle, "pause")

DO_SET_MPD_CMD(pause, "pause")

DO_SIMPLE_MPD_CMD(next_song, "next")
DO_SIMPLE_MPD_CMD(prev_song, "previous")
// }}}

// play

DO_SIMPLE_MPD_CMD(clear_songs, "clear")

// playback options {{{
DO_SET_MPD_CMD(set_random, "random")
DO_SET_MPD_CMD(set_xfade, "crossfade")
DO_SET_MPD_CMD(set_repeat, "repeat")
// }}}

DO_SET_MPD_CMD(delete_song_by_id, "deleteid")

static int luaA_mpdc_add_song(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	const char *filename = luaL_checkstring(L, 2);
	if (luaA_mpdc_command(L, mpdc->sh, "addid \"%s\"\n", filename))
		return 1;
	return 0;
}

static int luaA_mpdc_seek_song_by_id(lua_State *L) {
	mpdc_type_t mpdc = luaL_checkudata(L, 1, "mpd_client");
	int songid = luaL_checknumber(L, 2);
	int pos = luaL_checknumber(L, 3);

	return luaA_mpdc_command(L, mpdc->sh, "seekid %d %+d\n", songid, pos);
}

static int luaA_mpdc_index(lua_State *L) {
	luaL_getmetatable(L, "mpd_client");
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);

	if (lua_isnil(L, -1)) {
		lua_pop(L, 2);
		return 0;
	} else {
		lua_remove(L, -2);
		return 1;
	}
}

static const luaL_reg mpdc_methods[] = {
	{"open", luaA_mpdc_open},
	{"close", luaA_mpdc_close},
	{NULL, NULL}
};

static const luaL_reg mpdc_meta[] = {
	{"__index", luaA_mpdc_index},
	{"__gc", luaA_mpdc_close},

	{"currentsong", luaA_mpdc_current_song},
	{"status", luaA_mpdc_status},
	{"listall", luaA_mpdc_list_all_songs},
	{"playlistid", luaA_mpdc_list_songs_by_id},

	{"next", luaA_mpdc_next_song},
	{"prev", luaA_mpdc_prev_song},

	{"set_random", luaA_mpdc_set_random},
	{"set_xfade", luaA_mpdc_set_xfade},
	{"set_repeat", luaA_mpdc_set_repeat},

	{"seekid", luaA_mpdc_seek_song_by_id},
	{"delid", luaA_mpdc_delete_song_by_id},
	{"add", luaA_mpdc_add_song},
	{"clear", luaA_mpdc_clear_songs},

	{"shuffle", luaA_mpdc_shuffle},
	{"play", luaA_mpdc_play},
	{"pause", luaA_mpdc_pause},
	{"toggle", luaA_mpdc_toggle},
	{"stop", luaA_mpdc_stop},

	{"reconnect", luaA_mpdc_reconnect},

	{NULL, NULL}
};

LUAA_OPEN(mpdc, "mpd_client", "1.0")

