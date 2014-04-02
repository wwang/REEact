CC=/usr/bin/gcc
CFLAGS=-DAUTOTUNER_WRAP -Wl,--wrap,pthread_create -Wl,--wrap,pthread_join  -D_STRATA_PTHREAD -pthread 
LIBS=-lrt -lpfm -m32  -O3 
AS=/usr/bin/nasm
ASFLAGS=-felf 
ARCH=x86_linux
DIR=.
LIB=libreeact.a
DLIB=libreeact.so
OS=posix
TARG=$(OS)/$(ARCH)

OBJS=autotuner.o at_policy.o at_static_linked_list.o autotuner_perfm.o at_wrap.o at_opt.o at_cpu_util.o

.SUFFIXES: .o .c

.c.o:
	$(CC) $(CFLAGS) -D__$(ARCH) $(INCLUDE) -c $< $(LIBS)

all: $(OBJS) 
	ar -r $(LIB) $(OBJS) $(LIBS)

clean: 
	rm -f *.o
