ifndef FFINSTALL
FFINSTALL=/usr
endif
_CFLAGS=-I$(FFINSTALL)/include/arm-linux-gnueabihf -I/usr/include/libdrm
_LDFLAGS=-L$(FFINSTALL)/lib/arm-linux-gnueabihf
_LDLIBS=-lavcodec -lavfilter -lavutil -lavformat -ldrm -lpthread -lGLESv2 -lglfw -lEGL

_CFLAGS+=-O3
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),Linux)
	uname_M := $(shell sh -c 'uname -m 2>/dev/null || echo not')
	ifeq ($(uname_M),armv7l)
 		_CFLAGS += -mfpu=neon -march=armv7-a -marm
	endif
endif

all: hello_drmprime

hello_drmprime: hello_drmprime.cpp drmprime_out.cpp extra.h drmprime_out.h MMapFrame.h extra_avcodec.h egl_out.cpp egl_out.h
	$(CXX) -o $@ $^ $(_CFLAGS) $(_LDFLAGS) $(_LDLIBS) -std=c++17

clean:
	rm -rf hello_drmprime


