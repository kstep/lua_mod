
/* includes {{{ */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#undef STRICT_WARNS
#define USE_FMT_ONLY 1

#ifdef STRICT_WARNS
#include <err.h>
#else
#define warn(fmt, ...) 
#endif

#include <sys/types.h>
#include <sys/sysctl.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/vmmeter.h>
#include <vm/vm_param.h>

#include <netinet/sctp_uio.h>
#include <netinet/ip_var.h>
#include <net/if.h>
#include <net/if_mib.h>

#include "luahelper.h"

/* }}} */

/* type definitions {{{ */

#ifndef CTLTYPE_NONE
#define CTLTYPE_NONE 0
#endif

typedef enum { sm_name = 1, sm_type = 4, sm_desc = 5 } sysctl_meta_t;

typedef enum { st_none, st_clockinfo, st_timeval, st_loadavg, st_vmtotal, st_dev, st_sctpstat, st_ipstat, st_unknown } st_type_t;
static const char* st_names[9] = {
	"",
	"clockinfo",
	"timeval",
	"loadavg",
	"vmtotal",
	"device",
	"sctpstat",
	"ipstat",
	"unknown"
};

typedef struct sysctl_node_t {
	int* mib;
	size_t mlen;
	size_t sz;
	int type;
	st_type_t stype;
	char fmt[2];
	int (*getter) (lua_State *, void *, size_t);
	int (*setter) (lua_State *, struct sysctl_node_t *, int);
} sysctl_node_t;
typedef int (*sysctl_node_getter_t) (lua_State *, void *, size_t);
typedef int (*sysctl_node_setter_t) (lua_State *, sysctl_node_t *, int);

/* }}} */

/* sysctl helper function {{{ */

/* get mib of node by its symbolic name */
static int
sysctl_mib(sysctl_node_t *node, const char* name)
{
	/*char *ptr = name;*/
	int oid[2];
	size_t sz = sizeof(int) * CTL_MAXNAME;
	int mib[CTL_MAXNAME];

	node->mlen = 0;

	oid[0] = 0;
	oid[1] = 3;
	if (sysctl(oid, 2, mib, &sz, (void *)name, strlen(name))) {
		warn("sysctl call failed in sysctl_mib(%s)", name);
		return (-1);
	}

	if ((node->mib = (int*)malloc(sz)) == NULL) {
		warn("malloc failed in sysctl_mib(%s)", name);
		return (-1);
	}
	node->mlen = sz / sizeof(int);
	memcpy(node->mib, mib, sz);
	return (0);
}

/* get first node */
static int
sysctl_first(sysctl_node_t *node)
{
	int name[3], newname[22];
	size_t nlen;
	name[0] = 0;
	name[1] = 2;
	name[2] = 1;
	nlen = sizeof(newname);
	if (sysctl(name, 3, newname, &nlen, NULL, 0)) {
		warn("sysctl call failed in sysctl_first");
		return (-1);
	}
	node->mlen = nlen / sizeof(int);
	if ((node->mib = malloc(nlen)) == NULL) {
		warn("malloc failed in sysctl_first");
		return (-1);
	}
	memcpy(node->mib, newname, nlen);

	return (0);
}

/* get next node */
static int
sysctl_next(sysctl_node_t *node)
{
	int name[22], newname[22], *newmib;
	size_t nlen, len;
	name[0] = 0;
	name[1] = 2;

	memcpy(name + 2, node->mib, node->mlen * sizeof(int));
	len = node->mlen + 2;
	nlen = sizeof(newname);

	if (sysctl(name, len, newname, &nlen, NULL, 0)) {
		warn("sysctl call failed in sysctl_next");
		return (-1);
	}
	len = nlen / sizeof(int);

	if (len != node->mlen) {
		if ((newmib = realloc(node->mib, nlen)) == NULL) {
			warn("realloc failed in sysctl_next");
			return (-1);
		}
		node->mlen = len;
		node->mib = newmib;
	}
	memcpy(node->mib, newname, nlen);
	return (0);
}

/* fill in sysctl_node_t structure with type data */
static int
sysctl_type(sysctl_node_t *node, u_int *flags)
{
	int qoid[CTL_MAXNAME+2];
	u_char buf[BUFSIZ];
	char *str_type;
	u_int *kind;
	size_t sz;

	qoid[0] = 0;
	qoid[1] = 4;
	memcpy(qoid + 2, node->mib, node->mlen * sizeof(int));

	sz = sizeof(buf);
	if (sysctl(qoid, node->mlen + 2, buf, &sz, 0, 0)) {
		warn("sysctl call failed in sysctl_type");
		return (-1);
	}

	str_type = (char *)(buf + sizeof(u_int));
	kind = (u_int*)buf;
	/*printf("str_type: %s\n", str_type);*/

	if (flags) *flags = *kind;
	node->type = *kind & CTLTYPE;

	strncpy(node->fmt, str_type, 2);

	if (*str_type == 'S' || *str_type == 'T') {
		if (strcmp(str_type, "S,clockinfo") == 0)
			node->stype = st_clockinfo;
		else if (strcmp(str_type, "S,timeval") == 0)
			node->stype = st_timeval;
		else if (strcmp(str_type, "S,loadavg") == 0)
			node->stype = st_loadavg;
		else if (strcmp(str_type, "S,vmtotal") == 0)
			node->stype = st_vmtotal;
		else if (strcmp(str_type, "T,dev_t") == 0)
			node->stype = st_dev;
		else if (strcmp(str_type, "S,sctpstat") == 0)
			node->stype = st_sctpstat;
		else if (strcmp(str_type, "S,ipstat") == 0)
			node->stype = st_ipstat;
		else
			node->stype = st_unknown;
	} else {
		node->stype = st_none;
	}

	return (0);
}

/* get symbolic name or description of a node  */
static char*
sysctl_info(sysctl_node_t *node, sysctl_meta_t meta)
{
	int qoid[CTL_MAXNAME+2];
	u_char buf[BUFSIZ];
	char *result;
	size_t sz;

	qoid[0] = 0;
	qoid[1] = (int)meta;
	memcpy(qoid + 2, node->mib, node->mlen * sizeof(int));

	sz = sizeof(buf);
	if (sysctl(qoid, node->mlen + 2, buf, &sz, 0, 0)) {
		warn("sysctl call failed in sysctl_info");
		return NULL;
	}

	if ((result = malloc(sz)) == NULL) {
		warn("malloc failed in sysctl_info");
		return NULL;
	}
	memcpy(result, buf, sz);

	return result;
}

/* get node value */
static void*
sysctl_get(sysctl_node_t *node)
{
	size_t sz;
	void *ptr;
	if (node->sz) {
		sz = node->sz;
	} else {
		if (sysctl(node->mib, node->mlen, NULL, &sz, NULL, 0)) {
			warn("malloc failed in sysctl_get");
			return NULL;
		}
	}
	sz += sz >> 2;
	if ((ptr = malloc(sz)) == NULL) return NULL;
	if (sysctl(node->mib, node->mlen, ptr, &sz, NULL, 0)) {
		warn("sysctl call failed in sysctl_get");
		free(ptr);
		warn("buffer freed in sysctl_get\n");
		return NULL;
	}
	node->sz = sz;
	return ptr;
}

/* }}} */

/* general purpose getters {{{ */

#define LUA_SYSCTL_GETTER(suffx) \
	static int luaA_sysctl_get##suffx (lua_State *L, void *buf, size_t sz)

#define NUMERIC_GETTER(suffx, type) \
	LUA_SYSCTL_GETTER(suffx) { \
		int i, n; \
		type *value = (type *)buf; \
		n = sz / sizeof(type); \
		for (i = 0; i < n; i++) \
			lua_pushnumber(L, value[i]); \
		return n; \
	}

#define NIL_GETTER(suffx) \
	LUA_SYSCTL_GETTER(suffx) { \
		return 0; }

NUMERIC_GETTER(int, int)
NUMERIC_GETTER(uint, unsigned int)
NUMERIC_GETTER(long, long)
NUMERIC_GETTER(ulong, unsigned long)
NUMERIC_GETTER(quad, quad_t)

NIL_GETTER(struct)

LUA_SYSCTL_GETTER(node)
{
	sysctl_node_t *node = lua_touserdata(L, 1);
	/* do some heuristics */
	if (node->mib[0] == CTL_NET && node->mib[1] == PF_LINK
		&& node->mib[2] == NETLINK_GENERIC && node->mib[3] == IFMIB_IFDATA
		&& sz == sizeof(struct ifmibdata)) {

		struct ifmibdata *ifmbd = (struct ifmibdata *)buf;
		lua_createtable(L, 0, 6);

		luaA_settable(L, -2, "name", string, ifmbd->ifmd_name);
		luaA_settable(L, -2, "pcount", number, ifmbd->ifmd_pcount);
		luaA_settable(L, -2, "flags", number, ifmbd->ifmd_flags);
		luaA_settable(L, -2, "snd_len", number, ifmbd->ifmd_snd_len);
		luaA_settable(L, -2, "snd_drops", number, ifmbd->ifmd_snd_drops);

		lua_createtable(L, 0, 25);

		luaA_settable(L, -2, "type", number, ifmbd->ifmd_data.ifi_type);
		luaA_settable(L, -2, "physical", number, ifmbd->ifmd_data.ifi_physical);
		luaA_settable(L, -2, "addrlen", number, ifmbd->ifmd_data.ifi_addrlen);
		luaA_settable(L, -2, "hdrlen", number, ifmbd->ifmd_data.ifi_hdrlen);
		luaA_settable(L, -2, "link_state", number, ifmbd->ifmd_data.ifi_link_state);
		luaA_settable(L, -2, "spare_char1", number, ifmbd->ifmd_data.ifi_spare_char1);
		luaA_settable(L, -2, "spare_char2", number, ifmbd->ifmd_data.ifi_spare_char2);
		luaA_settable(L, -2, "datalen", number, ifmbd->ifmd_data.ifi_datalen);
		luaA_settable(L, -2, "mtu", number, ifmbd->ifmd_data.ifi_mtu);
		luaA_settable(L, -2, "metric", number, ifmbd->ifmd_data.ifi_metric);
		luaA_settable(L, -2, "baudrate", number, ifmbd->ifmd_data.ifi_baudrate);
		luaA_settable(L, -2, "ipackets", number, ifmbd->ifmd_data.ifi_ipackets);
		luaA_settable(L, -2, "ierrors", number, ifmbd->ifmd_data.ifi_ierrors);
		luaA_settable(L, -2, "opackets", number, ifmbd->ifmd_data.ifi_opackets);
		luaA_settable(L, -2, "oerrors", number, ifmbd->ifmd_data.ifi_oerrors);
		luaA_settable(L, -2, "collisions", number, ifmbd->ifmd_data.ifi_collisions);
		luaA_settable(L, -2, "ibytes", number, ifmbd->ifmd_data.ifi_ibytes);
		luaA_settable(L, -2, "obytes", number, ifmbd->ifmd_data.ifi_obytes);
		luaA_settable(L, -2, "imcasts", number, ifmbd->ifmd_data.ifi_imcasts);
		luaA_settable(L, -2, "omcasts", number, ifmbd->ifmd_data.ifi_omcasts);
		luaA_settable(L, -2, "iqdrops", number, ifmbd->ifmd_data.ifi_iqdrops);
		luaA_settable(L, -2, "noproto", number, ifmbd->ifmd_data.ifi_noproto);
		luaA_settable(L, -2, "hwassist", number, ifmbd->ifmd_data.ifi_hwassist);
		luaA_settable(L, -2, "epoch", number, ifmbd->ifmd_data.ifi_epoch);

		lua_createtable(L, 2, 0);
		luaA_isettable(L, -2, 1, number, ifmbd->ifmd_data.ifi_lastchange.tv_sec);
		luaA_isettable(L, -2, 2, number, ifmbd->ifmd_data.ifi_lastchange.tv_usec);
		lua_setfield(L, -2, "lastchange");

		lua_setfield(L, -2, "data");

	} else if (node->mib[0] == CTL_VM && node->mlen == 3 && sz == sizeof(struct xswdev)) {

		struct xswdev *xswd = (struct xswdev *)buf;
		lua_createtable(L, 0, 5);

		luaA_settable(L, -2, "version", number, xswd->xsw_version);
		luaA_settable(L, -2, "flags", number, xswd->xsw_flags);
		luaA_settable(L, -2, "nblks", number, xswd->xsw_nblks);
		luaA_settable(L, -2, "used", number, xswd->xsw_used);

		lua_createtable(L, 2, 0);

		luaA_isettable(L, -2, 1, number, major(xswd->xsw_dev));
		luaA_isettable(L, -2, 2, number, minor(xswd->xsw_dev));

		lua_setfield(L, -2, "dev");

	} else {
		lua_pushlstring(L, (char *)buf, sz - 1);
	}
	return 1;
}

LUA_SYSCTL_GETTER(ipstat)
{
	struct ipstat *value = (struct ipstat*)buf;

	lua_createtable(L, 0, 29);

	luaA_settable(L, -2, "total", number, value->ips_total);
	luaA_settable(L, -2, "badsum", number, value->ips_badsum);
	luaA_settable(L, -2, "tooshort", number, value->ips_tooshort);
	luaA_settable(L, -2, "toosmall", number, value->ips_toosmall);
	luaA_settable(L, -2, "badhlen", number, value->ips_badhlen);
	luaA_settable(L, -2, "badlen", number, value->ips_badlen);
	luaA_settable(L, -2, "fragments", number, value->ips_fragments);
	luaA_settable(L, -2, "fragdropped", number, value->ips_fragdropped);
	luaA_settable(L, -2, "fragtimeout", number, value->ips_fragtimeout);
	luaA_settable(L, -2, "forward", number, value->ips_forward);
	luaA_settable(L, -2, "fastforward", number, value->ips_fastforward);
	luaA_settable(L, -2, "cantforward", number, value->ips_cantforward);
	luaA_settable(L, -2, "redirectsent", number, value->ips_redirectsent);
	luaA_settable(L, -2, "noproto", number, value->ips_noproto);
	luaA_settable(L, -2, "delivered", number, value->ips_delivered);
	luaA_settable(L, -2, "localout", number, value->ips_localout);
	luaA_settable(L, -2, "odropped", number, value->ips_odropped);
	luaA_settable(L, -2, "reassembled", number, value->ips_reassembled);
	luaA_settable(L, -2, "fragmented", number, value->ips_fragmented);
	luaA_settable(L, -2, "ofragments", number, value->ips_ofragments);
	luaA_settable(L, -2, "cantfrag", number, value->ips_cantfrag);
	luaA_settable(L, -2, "badoptions", number, value->ips_badoptions);
	luaA_settable(L, -2, "noroute", number, value->ips_noroute);
	luaA_settable(L, -2, "badvers", number, value->ips_badvers);
	luaA_settable(L, -2, "rawout", number, value->ips_rawout);
	luaA_settable(L, -2, "toolong", number, value->ips_toolong);
	luaA_settable(L, -2, "notmember", number, value->ips_notmember);
	luaA_settable(L, -2, "nogif", number, value->ips_nogif);
	luaA_settable(L, -2, "badaddr", number, value->ips_badaddr);

	return 1;
}

LUA_SYSCTL_GETTER(sctp)
{

	struct sctpstat *value = (struct sctpstat*)buf;

	lua_createtable(L, 0, 127);

	luaA_settable(L, -2, "currestab", number, value->sctps_currestab);
	luaA_settable(L, -2, "activeestab", number, value->sctps_activeestab);
	luaA_settable(L, -2, "restartestab", number, value->sctps_restartestab);
	luaA_settable(L, -2, "collisionestab", number, value->sctps_collisionestab);
	luaA_settable(L, -2, "passiveestab", number, value->sctps_passiveestab);
	luaA_settable(L, -2, "aborted", number, value->sctps_aborted);
	luaA_settable(L, -2, "shutdown", number, value->sctps_shutdown);
	luaA_settable(L, -2, "outoftheblue", number, value->sctps_outoftheblue);
	luaA_settable(L, -2, "checksumerrors", number, value->sctps_checksumerrors);
	luaA_settable(L, -2, "outcontrolchunks", number, value->sctps_outcontrolchunks);
	luaA_settable(L, -2, "outorderchunks", number, value->sctps_outorderchunks);
	luaA_settable(L, -2, "outunorderchunks", number, value->sctps_outunorderchunks);
	luaA_settable(L, -2, "incontrolchunks", number, value->sctps_incontrolchunks);
	luaA_settable(L, -2, "inorderchunks", number, value->sctps_inorderchunks);
	luaA_settable(L, -2, "inunorderchunks", number, value->sctps_inunorderchunks);
	luaA_settable(L, -2, "fragusrmsgs", number, value->sctps_fragusrmsgs);
	luaA_settable(L, -2, "reasmusrmsgs", number, value->sctps_reasmusrmsgs);
	luaA_settable(L, -2, "outpackets", number, value->sctps_outpackets);
	luaA_settable(L, -2, "inpackets", number, value->sctps_inpackets);
	luaA_settable(L, -2, "recvpackets", number, value->sctps_recvpackets);
	luaA_settable(L, -2, "recvdatagrams", number, value->sctps_recvdatagrams);
	luaA_settable(L, -2, "recvpktwithdata", number, value->sctps_recvpktwithdata);
	luaA_settable(L, -2, "recvsacks", number, value->sctps_recvsacks);
	luaA_settable(L, -2, "recvdata", number, value->sctps_recvdata);
	luaA_settable(L, -2, "recvdupdata", number, value->sctps_recvdupdata);
	luaA_settable(L, -2, "recvheartbeat", number, value->sctps_recvheartbeat);
	luaA_settable(L, -2, "recvheartbeatack", number, value->sctps_recvheartbeatack);
	luaA_settable(L, -2, "recvecne", number, value->sctps_recvecne);
	luaA_settable(L, -2, "recvauth", number, value->sctps_recvauth);
	luaA_settable(L, -2, "recvauthmissing", number, value->sctps_recvauthmissing);
	luaA_settable(L, -2, "recvivalhmacid", number, value->sctps_recvivalhmacid);
	luaA_settable(L, -2, "recvivalkeyid", number, value->sctps_recvivalkeyid);
	luaA_settable(L, -2, "recvauthfailed", number, value->sctps_recvauthfailed);
	luaA_settable(L, -2, "recvexpress", number, value->sctps_recvexpress);
	luaA_settable(L, -2, "recvexpressm", number, value->sctps_recvexpressm);
	luaA_settable(L, -2, "sendpackets", number, value->sctps_sendpackets);
	luaA_settable(L, -2, "sendsacks", number, value->sctps_sendsacks);
	luaA_settable(L, -2, "senddata", number, value->sctps_senddata);
	luaA_settable(L, -2, "sendretransdata", number, value->sctps_sendretransdata);
	luaA_settable(L, -2, "sendfastretrans", number, value->sctps_sendfastretrans);
	luaA_settable(L, -2, "sendmultfastretrans", number, value->sctps_sendmultfastretrans);
	luaA_settable(L, -2, "sendheartbeat", number, value->sctps_sendheartbeat);
	luaA_settable(L, -2, "sendecne", number, value->sctps_sendecne);
	luaA_settable(L, -2, "sendauth", number, value->sctps_sendauth);
	luaA_settable(L, -2, "senderrors", number, value->sctps_senderrors);
	luaA_settable(L, -2, "pdrpfmbox", number, value->sctps_pdrpfmbox);
	luaA_settable(L, -2, "pdrpfehos", number, value->sctps_pdrpfehos);
	luaA_settable(L, -2, "pdrpmbda", number, value->sctps_pdrpmbda);
	luaA_settable(L, -2, "pdrpmbct", number, value->sctps_pdrpmbct);
	luaA_settable(L, -2, "pdrpbwrpt", number, value->sctps_pdrpbwrpt);
	luaA_settable(L, -2, "pdrpcrupt", number, value->sctps_pdrpcrupt);
	luaA_settable(L, -2, "pdrpnedat", number, value->sctps_pdrpnedat);
	luaA_settable(L, -2, "pdrppdbrk", number, value->sctps_pdrppdbrk);
	luaA_settable(L, -2, "pdrptsnnf", number, value->sctps_pdrptsnnf);
	luaA_settable(L, -2, "pdrpdnfnd", number, value->sctps_pdrpdnfnd);
	luaA_settable(L, -2, "pdrpdiwnp", number, value->sctps_pdrpdiwnp);
	luaA_settable(L, -2, "pdrpdizrw", number, value->sctps_pdrpdizrw);
	luaA_settable(L, -2, "pdrpbadd", number, value->sctps_pdrpbadd);
	luaA_settable(L, -2, "pdrpmark", number, value->sctps_pdrpmark);
	luaA_settable(L, -2, "timoiterator", number, value->sctps_timoiterator);
	luaA_settable(L, -2, "timodata", number, value->sctps_timodata);
	luaA_settable(L, -2, "timowindowprobe", number, value->sctps_timowindowprobe);
	luaA_settable(L, -2, "timoinit", number, value->sctps_timoinit);
	luaA_settable(L, -2, "timosack", number, value->sctps_timosack);
	luaA_settable(L, -2, "timoshutdown", number, value->sctps_timoshutdown);
	luaA_settable(L, -2, "timoheartbeat", number, value->sctps_timoheartbeat);
	luaA_settable(L, -2, "timocookie", number, value->sctps_timocookie);
	luaA_settable(L, -2, "timosecret", number, value->sctps_timosecret);
	luaA_settable(L, -2, "timopathmtu", number, value->sctps_timopathmtu);
	luaA_settable(L, -2, "timoshutdownack", number, value->sctps_timoshutdownack);
	luaA_settable(L, -2, "timoshutdownguard", number, value->sctps_timoshutdownguard);
	luaA_settable(L, -2, "timostrmrst", number, value->sctps_timostrmrst);
	luaA_settable(L, -2, "timoearlyfr", number, value->sctps_timoearlyfr);
	luaA_settable(L, -2, "timoasconf", number, value->sctps_timoasconf);
	luaA_settable(L, -2, "timodelprim", number, value->sctps_timodelprim);
	luaA_settable(L, -2, "timoautoclose", number, value->sctps_timoautoclose);
	luaA_settable(L, -2, "timoassockill", number, value->sctps_timoassockill);
	luaA_settable(L, -2, "timoinpkill", number, value->sctps_timoinpkill);
	luaA_settable(L, -2, "earlyfrstart", number, value->sctps_earlyfrstart);
	luaA_settable(L, -2, "earlyfrstop", number, value->sctps_earlyfrstop);
	luaA_settable(L, -2, "earlyfrmrkretrans", number, value->sctps_earlyfrmrkretrans);
	luaA_settable(L, -2, "earlyfrstpout", number, value->sctps_earlyfrstpout);
	luaA_settable(L, -2, "earlyfrstpidsck1", number, value->sctps_earlyfrstpidsck1);
	luaA_settable(L, -2, "earlyfrstpidsck2", number, value->sctps_earlyfrstpidsck2);
	luaA_settable(L, -2, "earlyfrstpidsck3", number, value->sctps_earlyfrstpidsck3);
	luaA_settable(L, -2, "earlyfrstpidsck4", number, value->sctps_earlyfrstpidsck4);
	luaA_settable(L, -2, "earlyfrstrid", number, value->sctps_earlyfrstrid);
	luaA_settable(L, -2, "earlyfrstrout", number, value->sctps_earlyfrstrout);
	luaA_settable(L, -2, "earlyfrstrtmr", number, value->sctps_earlyfrstrtmr);
	luaA_settable(L, -2, "hdrops", number, value->sctps_hdrops);
	luaA_settable(L, -2, "badsum", number, value->sctps_badsum);
	luaA_settable(L, -2, "noport", number, value->sctps_noport);
	luaA_settable(L, -2, "badvtag", number, value->sctps_badvtag);
	luaA_settable(L, -2, "badsid", number, value->sctps_badsid);
	luaA_settable(L, -2, "nomem", number, value->sctps_nomem);
	luaA_settable(L, -2, "fastretransinrtt", number, value->sctps_fastretransinrtt);
	luaA_settable(L, -2, "markedretrans", number, value->sctps_markedretrans);
	luaA_settable(L, -2, "naglesent", number, value->sctps_naglesent);
	luaA_settable(L, -2, "naglequeued", number, value->sctps_naglequeued);
	luaA_settable(L, -2, "maxburstqueued", number, value->sctps_maxburstqueued);
	luaA_settable(L, -2, "ifnomemqueued", number, value->sctps_ifnomemqueued);
	luaA_settable(L, -2, "windowprobed", number, value->sctps_windowprobed);
	luaA_settable(L, -2, "lowlevelerr", number, value->sctps_lowlevelerr);
	luaA_settable(L, -2, "lowlevelerrusr", number, value->sctps_lowlevelerrusr);
	luaA_settable(L, -2, "datadropchklmt", number, value->sctps_datadropchklmt);
	luaA_settable(L, -2, "datadroprwnd", number, value->sctps_datadroprwnd);
	luaA_settable(L, -2, "ecnereducedcwnd", number, value->sctps_ecnereducedcwnd);
	luaA_settable(L, -2, "vtagexpress", number, value->sctps_vtagexpress);
	luaA_settable(L, -2, "vtagbogus", number, value->sctps_vtagbogus);
	luaA_settable(L, -2, "primary_randry", number, value->sctps_primary_randry);
	luaA_settable(L, -2, "cmt_randry", number, value->sctps_cmt_randry);
	luaA_settable(L, -2, "slowpath_sack", number, value->sctps_slowpath_sack);
	luaA_settable(L, -2, "wu_sacks_sent", number, value->sctps_wu_sacks_sent);
	luaA_settable(L, -2, "sends_with_flags", number, value->sctps_sends_with_flags);
	luaA_settable(L, -2, "sends_with_unord", number, value->sctps_sends_with_unord);
	luaA_settable(L, -2, "sends_with_eof", number, value->sctps_sends_with_eof);
	luaA_settable(L, -2, "sends_with_abort", number, value->sctps_sends_with_abort);
	luaA_settable(L, -2, "protocol_drain_calls", number, value->sctps_protocol_drain_calls);
	luaA_settable(L, -2, "protocol_drains_done", number, value->sctps_protocol_drains_done);
	luaA_settable(L, -2, "read_peeks", number, value->sctps_read_peeks);
	luaA_settable(L, -2, "cached_chk", number, value->sctps_cached_chk);
	luaA_settable(L, -2, "cached_strmoq", number, value->sctps_cached_strmoq);
	luaA_settable(L, -2, "left_abandon", number, value->sctps_left_abandon);
	luaA_settable(L, -2, "send_burst_avoid", number, value->sctps_send_burst_avoid);
	luaA_settable(L, -2, "send_cwnd_avoid", number, value->sctps_send_cwnd_avoid);
	luaA_settable(L, -2, "fwdtsn_map_over", number, value->sctps_fwdtsn_map_over);

	lua_createtable(L, 2, 0);
	luaA_isettable(L, -2, 1, number, value->sctps_discontinuitytime.tv_sec);
	luaA_isettable(L, -2, 2, number, value->sctps_discontinuitytime.tv_usec);

	lua_setfield(L, -2, "discontinuitytime");

	return 1;
}

LUA_SYSCTL_GETTER(string)
{
	/*char *value = (char*)buf;*/

	lua_pushlstring(L, buf, sz);
	return 1;
}

LUA_SYSCTL_GETTER(therm)
{
	int (*value) = (int*)buf;
	lua_pushnumber(L, (*value - 2732.0) / 10.0);
	return 1;
}

LUA_SYSCTL_GETTER(clock)
{
	struct clockinfo *value = (struct clockinfo*)buf;
	lua_pushnumber(L, value->hz);
	lua_pushnumber(L, value->tick);
	lua_pushnumber(L, value->profhz);
	lua_pushnumber(L, value->stathz);
	return 4;
}

LUA_SYSCTL_GETTER(time)
{
	struct timeval *value = (struct timeval*)buf;
	lua_pushnumber(L, value->tv_sec);
	lua_pushnumber(L, value->tv_usec);
	return 2;
}

LUA_SYSCTL_GETTER(loadavg)
{
	struct loadavg *value = (struct loadavg*)buf;
	lua_pushnumber(L, (double)value->ldavg[0] / (double)value->fscale);
	lua_pushnumber(L, (double)value->ldavg[1] / (double)value->fscale);
	lua_pushnumber(L, (double)value->ldavg[2] / (double)value->fscale);
	return 3;
}

LUA_SYSCTL_GETTER(vmtotal)
{
	struct vmtotal *value = (struct vmtotal*)buf;
	int pagesize = getpagesize();

	lua_createtable(L, 0, 12);

	luaA_settable(L, -2, "runq", number, value->t_rq);
	luaA_settable(L, -2, "diskw", number, value->t_dw);
	luaA_settable(L, -2, "pagew", number, value->t_pw);
	luaA_settable(L, -2, "sleep", number, value->t_sl);

	lua_createtable(L, 2, 0);
	luaA_isettable(L, -2, 1, number, value->t_vm);
	luaA_isettable(L, -2, 2, number, value->t_avm);
	lua_setfield(L, -2, "vmem");

	lua_createtable(L, 2, 0);
	luaA_isettable(L, -2, 1, number, value->t_rm);
	luaA_isettable(L, -2, 2, number, value->t_arm);
	lua_setfield(L, -2, "rmem");

	lua_createtable(L, 2, 0);
	luaA_isettable(L, -2, 1, number, value->t_vmshr);
	luaA_isettable(L, -2, 2, number, value->t_avmshr);
	lua_setfield(L, -2, "vsmem");

	lua_createtable(L, 2, 0);
	luaA_isettable(L, -2, 1, number, value->t_rmshr);
	luaA_isettable(L, -2, 2, number, value->t_armshr);
	lua_setfield(L, -2, "rsmem");

	luaA_settable(L, -2, "free", number, value->t_free);

	luaA_settable(L, -2, "pagesz", number, pagesize);
	return 1;
}

LUA_SYSCTL_GETTER(dev)
{
	dev_t *value = (dev_t*)buf;
	lua_pushnumber(L, major(*value));
	lua_pushnumber(L, minor(*value));
	return 2;
}

/* }}} */

/* general purpose setters {{{ */

#define LUA_SYSCTL_SETTER(suffx) \
	static int luaA_sysctl_set##suffx (lua_State *L, sysctl_node_t *node, int validx)

#define NUMERIC_SETTER(suffx, type) \
	LUA_SYSCTL_SETTER(suffx) { \
		type value = luaL_checknumber(L, validx); \
		if (sysctl(node->mib, node->mlen, NULL, 0, &value, sizeof(type))) return 0; \
		lua_pushboolean(L, 1); \
		return 1; \
	}

#define NIL_SETTER(suffx) \
	LUA_SYSCTL_SETTER(suffx) { \
		return 0; }

NUMERIC_SETTER(int, int)
NUMERIC_SETTER(uint, unsigned int)
NUMERIC_SETTER(long, long)
NUMERIC_SETTER(ulong, unsigned long)
NUMERIC_SETTER(quad, quad_t)

NIL_SETTER(struct)
NIL_SETTER(node)

LUA_SYSCTL_SETTER(string)
{
	size_t strsz;
	const char *value = luaL_checklstring(L, validx, &strsz);
	if (sysctl(node->mib, node->mlen, NULL, 0, (void *)value, strsz)) return 0;
	lua_pushboolean(L, 1);
	return 1;
}
/* }}} */

/* node helpers {{{ */
static sysctl_node_getter_t
get_getter_by_type(int type, char* fmt, st_type_t stype)
{
#ifndef USE_FMT_ONLY
	switch (type) {
	case CTLTYPE_INT:
		return luaA_sysctl_getint;
	case CTLTYPE_UINT:
		return luaA_sysctl_getuint;
	case CTLTYPE_LONG:
		return luaA_sysctl_getlong;
	case CTLTYPE_ULONG:
		return luaA_sysctl_getulong;
	case CTLTYPE_QUAD:
		return luaA_sysctl_getquad;
	case CTLTYPE_STRING:
		return luaA_sysctl_getstring;
	case CTLTYPE_NODE:
		return luaA_sysctl_getnode;
	case CTLTYPE_OPAQUE:
	case CTLTYPE_NONE:
#endif
		switch (*fmt) {
		case 'I':
			return fmt[1] == 'K'? luaA_sysctl_gettherm: (fmt[1] == 'U'? luaA_sysctl_getuint: luaA_sysctl_getint);
		case 'L':
			return fmt[1] == 'U'? luaA_sysctl_getulong: luaA_sysctl_getlong;
		case 'Q':
			return luaA_sysctl_getquad;
		case 'A':
			return luaA_sysctl_getstring;
		case 'N':
			return luaA_sysctl_getnode;
		case 'S':
			switch (stype) {
			case st_clockinfo:
				return luaA_sysctl_getclock;
			case st_timeval:
				return luaA_sysctl_gettime;
			case st_vmtotal:
				return luaA_sysctl_getvmtotal;
			case st_dev:
				return luaA_sysctl_getdev;
			case st_sctpstat:
				return luaA_sysctl_getsctp;
			case st_ipstat:
				return luaA_sysctl_getipstat;
			case st_loadavg:
				return luaA_sysctl_getloadavg;
			}
		default:
			return luaA_sysctl_getstruct;
		}
#ifndef USE_FMT_ONLY
	}
#endif
	return NULL;
}
static sysctl_node_setter_t
get_setter_by_type(int type, char* fmt, st_type_t stype)
{
#ifndef USE_FMT_ONLY
	switch (type) {
	case CTLTYPE_INT:
		return luaA_sysctl_setint;
	case CTLTYPE_UINT:
		return luaA_sysctl_setuint;
	case CTLTYPE_LONG:
		return luaA_sysctl_setlong;
	case CTLTYPE_ULONG:
		return luaA_sysctl_setulong;
	case CTLTYPE_QUAD:
		return luaA_sysctl_setquad;
	case CTLTYPE_STRING:
		return luaA_sysctl_setstring;
	case CTLTYPE_NODE:
		return luaA_sysctl_setnode;
	case CTLTYPE_OPAQUE:
#endif
		switch (*fmt) {
		case 'I':
			return fmt[1] == 'U'? luaA_sysctl_setuint: luaA_sysctl_setint;
		case 'L':
			return fmt[1] == 'U'? luaA_sysctl_setulong: luaA_sysctl_setlong;
		case 'Q':
			return luaA_sysctl_setquad;
		case 'A':
			return luaA_sysctl_setstring;
		case 'N':
			return luaA_sysctl_setnode;
		default:
			return luaA_sysctl_setstruct;
		}
#ifndef USE_FMT_ONLY
	}
#endif
	return NULL;
}

static int
sysctl_newnode(sysctl_node_t **node, const char* nodename)
{
	int freeonerr = 0;
	int flags;

	if (*node == NULL) {
		if ((*node = malloc(sizeof(sysctl_node_t))) == NULL) {
			warn("malloc failed in sysctl_newnode(%s)", nodename);
			return (-1);
		}
		freeonerr = 1;
	}

	if (nodename && sysctl_mib(*node, nodename) || sysctl_type(*node, &flags)) {
		warn("node population failed in sysctl_newnode(%s)", nodename);
		if (freeonerr) {
			free(*node);
		 	*node = NULL;
			warn("buffer freed in sysctl_newnode(%s)", nodename);
		}
		return (-1);
	}

	(*node)->sz = 0;
	sysctl((*node)->mib, (*node)->mlen, NULL, &((*node)->sz), NULL, 0); /* prefetch size */
	(*node)->getter = (flags & CTLFLAG_RD)? get_getter_by_type((*node)->type, (*node)->fmt, (*node)->stype): NULL;
	(*node)->setter = (flags & CTLFLAG_WR)? get_setter_by_type((*node)->type, (*node)->fmt, (*node)->stype): NULL;

	return (0);
}
/* }}} */

/* sysctl node methods {{{ */

#define SYSCTL_NODE_METHOD(name) static int luaA_sysctl_node_##name (lua_State *L)

SYSCTL_NODE_METHOD(tostring)
{
	sysctl_node_t *node = luaL_checkudata(L, 1, "sysctl_node");
	char *name = sysctl_info(node, sm_name);

	if (name) {
		char access[3] = "--\0";
		char fmt[3] = "\0\0\0";
		if (node->getter) access[0] = 'r';
		if (node->setter) access[1] = 'w';
		fmt[0] = node->fmt[0];
		fmt[1] = node->fmt[1];

		lua_pushfstring(L, "[udata sysctl_node(%s) %s%s(%d) %s]", name, fmt, st_names[node->stype], node->sz, access);
		free(name);
		return 1;
	}
	return 0;
}

SYSCTL_NODE_METHOD(set)
{
	sysctl_node_t *node = luaL_checkudata(L, 1, "sysctl_node");

	if (node->setter)
		return node->setter(L, node, 2);

	return 0;
}

SYSCTL_NODE_METHOD(get)
{
	sysctl_node_t *node = luaL_checkudata(L, 1, "sysctl_node");
	int result = 0;

	if (node->getter) {
		void *buf = sysctl_get(node);
		if (buf) {
			result = node->getter(L, buf, node->sz);
			free(buf);
		}
	}

	return result;
}

SYSCTL_NODE_METHOD(next)
{
	sysctl_node_t *node = NULL;
	sysctl_node_t *state = NULL;
	sysctl_node_t *newnode;

	int i;
	u_int flags;

	if (lua_isuserdata(L, 2)) {
		state = lua_touserdata(L, 1);
		node = luaL_checkudata(L, 2, "sysctl_node");
	} else {
		node = luaL_checkudata(L, 1, "sysctl_node");
	}

	newnode = lua_newuserdata(L, sizeof(sysctl_node_t));

	memcpy(newnode, node, sizeof(sysctl_node_t));
	if ((newnode->mib = calloc(node->mlen, sizeof(int))) == NULL) goto node_next_failed; 
	memcpy(newnode->mib, node->mib, node->mlen * sizeof(int));

	if (sysctl_next(newnode) || sysctl_type(newnode, &flags))
		goto node_next_failed;

	if (state) {
		if (newnode->mlen < state->mlen) goto node_next_failed;
		for (i = 0; i < state->mlen; i++)
			if (newnode->mib[i] != state->mib[i]) goto node_next_failed;
	}

	newnode->getter = (flags & CTLFLAG_RD)? get_getter_by_type(node->type, node->fmt, node->stype): NULL;
	newnode->setter = (flags & CTLFLAG_WR)? get_setter_by_type(node->type, node->fmt, node->stype): NULL;
	newnode->sz = 0;

	luaL_getmetatable(L, "sysctl_node");
	lua_setmetatable(L, -2);

	return 1;

node_next_failed:
	lua_pop(L, 1);
	return 0;
}

SYSCTL_NODE_METHOD(node)
{
	sysctl_node_t *node = luaL_checkudata(L, 1, "sysctl_node");
	sysctl_node_t *newnode;

	int i, top = lua_gettop(L) - 1;
	if (top < 1) return 0;

	newnode = lua_newuserdata(L, sizeof(sysctl_node_t));
	newnode->mlen = node->mlen + top;
	if ((newnode->mib = calloc(newnode->mlen, sizeof(int))) == NULL)
		goto sysctl_node_node_failed_nofree;

	memcpy(newnode->mib, node->mib, node->mlen * sizeof(int));
	for (i = 0; i < top; i++) {
		if (!lua_isnumber(L, i + 2))
			goto sysctl_node_node_failed;
		newnode->mib[node->mlen + i] = lua_tonumber(L, i + 2);
	}
	if (sysctl_newnode(&newnode, NULL))
		goto sysctl_node_node_failed;

	luaL_getmetatable(L, "sysctl_node");
	lua_setmetatable(L, -2);
	return 1;

sysctl_node_node_failed:
	free(newnode->mib);
sysctl_node_node_failed_nofree:
	lua_pop(L, 1);
	return 0;
}

SYSCTL_NODE_METHOD(index)
{
	int result;
	const char *index;

	lua_getmetatable(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);

	if (!lua_isnil(L, -1)) {
		lua_remove(L, -2);
		return 1;
	}

	lua_pop(L, 2);
	index = luaL_checkstring(L, 2);
	result = 1;
	if (strcmp(index, "value") == 0)
		return luaA_sysctl_node_get(L);
	else {
		sysctl_node_t *node = luaL_checkudata(L, 1, "sysctl_node");
		if (strcmp(index, "name") == 0 || strcmp(index, "desc") == 0) {
			char *name = sysctl_info(node, *index == 'n'? sm_name: sm_desc);
			if (name) {
				lua_pushstring(L, name);
				free(name);
			} else {
				result = 0;
			}
		} else if (strcmp(index, "type") == 0) {
			lua_pushnumber(L, node->type);
		} else if (strcmp(index, "struct") == 0) {
			lua_pushstring(L, st_names[node->stype]);
		} else if (strcmp(index, "readonly") == 0) {
			lua_pushboolean(L, node->setter == NULL);
		} else if (strcmp(index, "format") == 0) {
			lua_pushlstring(L, node->fmt, 2);
		} else {
			result = 0;
		}
	}

	return result;
}

SYSCTL_NODE_METHOD(newindex)
{
	const char *index = luaL_checkstring(L, 2);
	if (strcmp(index, "value") == 0) {
		lua_remove(L, 2);
		return luaA_sysctl_node_set(L);
	}

	return 0;
}

SYSCTL_NODE_METHOD(eq)
{
	sysctl_node_t *node1 = luaL_checkudata(L, 1, "sysctl_node");
	sysctl_node_t *node2 = luaL_checkudata(L, 2, "sysctl_node");
	int i, result = 1;

	if (node1->mib != node2->mib) {
		if (node1->mlen == node2->mlen) {
			for (i = 0; i < node1->mlen; i++) {
				if (node1->mib[i] != node2->mib[i]) {
					result = 0;
					break;
				}
			}
		} else {
			result = 0;
		}
	}

	lua_pushboolean(L, result);
	return 1;
}

SYSCTL_NODE_METHOD(gc)
{
	sysctl_node_t *node = luaL_checkudata(L, 1, "sysctl_node");
	if (node->mib) free(node->mib);
	return 0;
}

/* }}} */

/* sysctl methods {{{ */

#define SYSCTL_METHOD(name) static int luaA_sysctl_##name (lua_State *L)

SYSCTL_METHOD(set)
{
	const char* nodename = luaL_checkstring(L, 1);
	sysctl_node_t *node = NULL;
	int result = 0;

	if (sysctl_newnode(&node, nodename) == 0) {
		if (node->setter)
			result = node->setter(L, node, 2);
		if (node->mib) free(node->mib);
		free(node);
	}

	return result;
}

SYSCTL_METHOD(get)
{
	const char* nodename = luaL_checkstring(L, 1);
	sysctl_node_t *node = NULL;
	int result = 0;
	
	if (sysctl_newnode(&node, nodename) == 0) {
		if (node->getter) {
			void *buf = sysctl_get(node);
			if (buf) {
				result = node->getter(L, buf, node->sz);
				free(buf);
			}
		}
		if (node->mib) free(node->mib);
		free(node);
	}

	return result;
}

SYSCTL_METHOD(node)
{
	const char* nodename = luaL_checkstring(L, 1);
	sysctl_node_t *node = lua_newuserdata(L, sizeof(sysctl_node_t));

	if (sysctl_newnode(&node, nodename)) {
		lua_pop(L, 1);
		/*return luaL_error(L, "not a sysctl node name %s", nodename);*/
		return 0;
	} else {
		luaL_getmetatable(L, "sysctl_node");
		lua_setmetatable(L, -2);
	}


	return 1;
}

SYSCTL_METHOD(each)
{
	sysctl_node_t *node;
	int stateless = 0;

	lua_pushcfunction(L, luaA_sysctl_node_next);

	if (lua_isuserdata(L, 1)) {
		node = luaL_checkudata(L, 1, "sysctl_node");
		lua_pushvalue(L, 1);
	} else {
		node = lua_newuserdata(L, sizeof(sysctl_node_t));
		if (lua_isstring(L, 1)) {
			const char* nodename = luaL_checkstring(L, 1);
			if (sysctl_newnode(&node, nodename)) {
				lua_pop(L, 2);
				/*return luaL_error(L, "not a sysctl node name %s", nodename);*/
				return 0;
			}
		} else {
			if (sysctl_first(node) || sysctl_newnode(&node, NULL)) {
				lua_pop(L, 2);
				/*return luaL_error(L, "couldn't fetch first node");*/
				return 0;
			}
			stateless = 1;
		}
		luaL_getmetatable(L, "sysctl_node");
		lua_setmetatable(L, -2);
	}

	if (stateless) {
		lua_pushnil(L);
		lua_pushvalue(L, -2);
		lua_remove(L, -3);
	} else {
		lua_pushvalue(L, -1);
	}
	return 3;
}
/* }}} */

/* lua register structs {{{ */
#define SYSCTL_REG(name) {#name, luaA_sysctl_##name}
#define SYSCTL_NODE_META(name) {"__" #name, luaA_sysctl_node_##name}
#define SYSCTL_NODE_REG(name) {#name, luaA_sysctl_node_##name}
#define SYSCTL_ENDREG {NULL, NULL}

static const luaL_reg sysctl_methods[] = {
	SYSCTL_REG(get),
	SYSCTL_REG(set),
	SYSCTL_REG(node),
	SYSCTL_REG(each),

	SYSCTL_ENDREG
};

static const luaL_reg sysctl_meta[] = {
	SYSCTL_NODE_META(index),
	SYSCTL_NODE_META(newindex),
	SYSCTL_NODE_META(gc),
	SYSCTL_NODE_META(tostring),
	SYSCTL_NODE_META(eq),

	SYSCTL_NODE_REG(get),
	SYSCTL_NODE_REG(set),
	SYSCTL_NODE_REG(node),
	SYSCTL_NODE_REG(next),

	SYSCTL_ENDREG
};
/* }}} */

LUAA_OPEN(sysctl, "sysctl_node", "1.3")

/* vim: set fdm=marker: */
