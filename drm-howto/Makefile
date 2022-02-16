FLAGS=`pkg-config --cflags --libs libdrm`
#FLAGS+=-Wall -O0 -g
FLAGS+=-Wall -g
FLAGS+=-D_FILE_OFFSET_BITS=64

_CFLAGS=-O3
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),Linux)
	uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
	ifeq ($(uname_M),armv7l)
 		#_CFLAGS += -mfpu=neon -march=armv7-a -marm
	endif
endif

all:
	gcc -o modeset modeset.c $(FLAGS)
	gcc -o modeset-double-buffered modeset-double-buffered.c $(FLAGS)
	gcc -o modeset-vsync modeset-vsync.c $(FLAGS)
	gcc -o modeset-atomic modeset-atomic.c $(FLAGS)
	$(CXX) -o modeset_latency modeset_latency.cpp $(FLAGS) $(_CFLAGS)
