GCC_FLAGS=-I/usr/include -I/usr/local/include/lua51
# GCC_FLAGS=-O0 -fno-inline -I/usr/include -I/usr/local/include/lua51

mntinfo:
	gcc ${GCC_FLAGS} -c lmntinfo.c && \
	gcc -o lmntinfo.so -shared lmntinfo.o && \
	strip lmntinfo.so

sysctl:
	gcc ${GCC_FLAGS} -c lsysctl.c && \
	gcc -o lsysctl.so -shared lsysctl.o && \
	strip lmntinfo.so

ifaddrs:
	gcc ${GCC_FLAGS} -c lifaddrs.c && \
	gcc -o lifaddrs.so -shared lifaddrs.o && \
	strip lmntinfo.so

mixer:
	gcc ${GCC_FLAGS} -c lmixer.c && \
	gcc -o lmixer.so -shared lmixer.o && \
	strip lmntinfo.so

mpdc:
	gcc ${GCC_FLAGS} -c lmpdc.c && \
	gcc -o lmpdc.so -shared lmpdc.o && \
	strip lmntinfo.so

test:
	gcc ${GCC_FLAGS} -c test.c && \
	gcc -o test.so -shared test.o && \
	strip lmntinfo.so

bit:
	gcc ${GCC_FLAGS} -c lbit.c && \
	gcc -o lbit.so -shared lbit.o && \
	strip lmntinfo.so

socket:
	gcc ${GCC_FLAGS} -c lsocket.c && \
	gcc -o lsocket.so -shared lsocket.o && \
	strip lmntinfo.so

all:
	make mntinfo \
	sysctl \
	ifaddrs \
	mixer \
	mpdc \
	bit \
	socket

install:
	make all
	sudo cp lsysctl.so lifaddrs.so lmixer.so lmpdc.so lbit.so lsocket.so /usr/local/lib/lua/5.1/

clean:
	rm -f *.so *.o
