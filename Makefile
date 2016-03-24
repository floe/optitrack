NAME=optitrack
EXTRA_CFLAGS+=-Wall

obj-m := ${NAME}.o

all:
	make -C /usr/src/linux SUBDIRS=`pwd` modules

clean:
	-rm libusb main -f ${NAME}.mod.c ${NAME}.mod.o ${NAME}.o ${NAME}.ko .${NAME}.* modules.order Module.symvers

main: main.cc
	g++ -ggdb -Wall -lGL -lGLU -lglut main.cc -o main

libusb: libusb.c
	g++ -Wall -o libusb libusb.c -lusb

