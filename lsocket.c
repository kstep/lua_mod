#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#include "luahelper.h"

LUAA_FUNC(socket_open)
{
	const char *hostname = luaL_checkstring(L, 1);
	const char *port = luaL_checkstring(L, 2);

	int *sock, result;
	struct addrinfo *addr, hints;
	struct timeval timeout;

	hints.ai_family = PF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = 0;
	hints.ai_addrlen = 0;
	hints.ai_addr = NULL;
	hints.ai_canonname = NULL;
	hints.ai_next = NULL;

	if (getaddrinfo(hostname, port, &hints, &addr) != 0) return 0;

	sock = lua_newuserdata(L, sizeof(int));
	*sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if ((*sock < 0) || (connect(*sock, addr->ai_addr, addr->ai_addrlen) < 0)) {
		lua_pop(L, 1);
		result = 0;
	} else {
		luaA_settype(L, -2, "socket");
		result = 1;
	}

	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	freeaddrinfo(addr);

	return result;
}

LUAA_FUNC(socket_gc)
{
	int *sockh = luaL_checkudata(L, 1, "socket");
	close(*sockh);
}

LUAA_FUNC(socket_send)
{
	ssize_t len;
	int *sockh = luaL_checkudata(L, 1, "socket");
	const char *buf = luaL_checklstring(L, 2, &len);

	len = send(*sockh, (void *)buf, len, 0);
	if (len < 0) return 0;

	lua_pushnumber(L, len);
	return 1;
}

LUAA_FUNC(socket_recv)
{
	ssize_t len;
	int *sockh = luaL_checkudata(L, 1, "socket");
	char buf[BUFSIZ];
	int num = 0;//, snum;

	while ((len = recv(*sockh, buf, BUFSIZ, 0)) > 0) {
		//if (len < 1) { if (++snum > 0) break; else continue; }
		//snum = 0;
		lua_pushlstring(L, buf, len);
		if (++num > 50) {
			lua_concat(L, num);
			num = 1;
		}
		if (len < BUFSIZ) break;
	}

	if (num > 1) {
		lua_concat(L, num);
		num = 1;
	}
	return num;
}

LUAA_FUNC(socket_index)
{
	const char *index = luaL_checkstring(L, 2);
	if (strcmp(index, "recv") == 0)
		lua_pushcfunction(L, luaA_socket_recv);
	else if (strcmp(index, "send") == 0)
		lua_pushcfunction(L, luaA_socket_send);
	else
		return 0;

	return 1;
}

LUAA_SREG(socket_methods)
LUAA_REG(socket, open)
LUAA_EREG

LUAA_SREG(socket_meta)
LUAA_MREG(socket, gc)
LUAA_MREG(socket, index)
LUAA_EREG

LUAA_OPEN(socket, "socket", "0.2")

