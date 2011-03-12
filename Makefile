GCC_FLAGS=-I/usr/include -I/usr/include/lua5.1
# GCC_FLAGS=-O0 -fno-inline -I/usr/include -I/usr/local/include/lua51

lmntinfo.so:
	gcc ${GCC_FLAGS} -c lmntinfo.c && \
	gcc -o lmntinfo.so -shared lmntinfo.o && \
	strip lmntinfo.so

lstatfs.so:
	gcc ${GCC_FLAGS} -c lstatfs.c && \
	gcc -o lstatfs.so -shared lstatfs.o && \
	strip lstatfs.so

lsysctl.so:
	gcc ${GCC_FLAGS} -c lsysctl.c && \
	gcc -o lsysctl.so -shared lsysctl.o && \
	strip lsysctl.so

lifaddrs.so:
	gcc ${GCC_FLAGS} -c lifaddrs.c && \
	gcc -o lifaddrs.so -shared lifaddrs.o && \
	strip lifaddrs.so

lmixer.so:
	gcc ${GCC_FLAGS} -c lmixer.c && \
	gcc -o lmixer.so -shared lmixer.o && \
	strip lmixer.so

lmixer4.so:
	gcc -I/usr/lib/oss/include ${GCC_FLAGS} -c lmixer4.c && \
	gcc -o lmixer4.so -shared lmixer4.o && \
	strip lmixer4.so

lamixer.so:
	gcc ${GCC_FLAGS} -c lamixer.c && \
	gcc -o lamixer.so -lasound -shared lamixer.o && \
	strip lamixer.so

lmpdc.so:
	gcc ${GCC_FLAGS} -c lmpdc.c && \
	gcc -o lmpdc.so -shared lmpdc.o && \
	strip lmpdc.so

test.so:
	gcc ${GCC_FLAGS} -c test.c && \
	gcc -o test.so -shared test.o && \
	strip test.so

lbit.so:
	gcc ${GCC_FLAGS} -c lbit.c && \
	gcc -o lbit.so -shared lbit.o && \
	strip lbit.so

lsocket.so:
	gcc ${GCC_FLAGS} -c lsocket.c && \
	gcc -o lsocket.so -shared lsocket.o && \
	strip lsocket.so

#all: lsysctl.so lifaddrs.so lmixer.so lmpdc.so lbit.so lsocket.so
all: lmpdc.so lbit.so lmixer.so

install: all
	sudo cp lmixer.so lmpdc.so lbit.so /usr/lib/lua/5.1/
	#sudo cp lsysctl.so lifaddrs.so lmixer.so lmpdc.so lbit.so lsocket.so /usr/local/lib/lua/5.1/
	#sudo cp lmpdc.so /usr/lib/lua/5.1/

clean:
	rm -f *.so *.o

.PHONY: all install clean

.DEFAULT: all
