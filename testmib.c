#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/if_mib.h>

static char*
sysctl_info(int *node, int mlen)
{
	int qoid[CTL_MAXNAME+2];
	u_char buf[BUFSIZ];
	char *result;
	size_t sz;

	qoid[0] = 0;
	qoid[1] = 1;
	memcpy(qoid + 2, node, mlen * sizeof(int));

	sz = sizeof(buf);
	if (sysctl(qoid, mlen + 2, buf, &sz, 0, 0)) {
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

int main (int argc, char* argv[]) {
	int name[6], i, r, len;
	struct ifmibdata ifmd;
	char *strname;

	name[0] = CTL_NET;
	name[1] = PF_LINK;
	name[2] = NETLINK_GENERIC;
	name[3] = IFMIB_IFDATA;
	name[5] = IFDATA_GENERAL;

	for (i = 0; i < 10; i++) {
		name[4] = i;
		len = sizeof(ifmd);
		r = sysctl(name, 6, &ifmd, &len, 0, 0);
		strname = sysctl_info(name, 6);
		printf("sysctl result=%d; errno=%d; len=%d; name=%s\n", r, errno, len, strname == NULL? "": strname);
		errno = 0;
		if (strname) free(strname);
	}
	len = 6;
	//r = sysctlnametomib("net.link.generic.ifdata.1.general", name, &len);
	r = sysctlnametomib("net.link.generic.ifdata.4.1", name, &len);
	printf("name2mib result=%d; errno=%d; len=%d; mib=%d,%d,%d,%d,%d,%d\n", r, errno, len, name[0], name[1], name[2], name[3], name[4], name[5]);
}

