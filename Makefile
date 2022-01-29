_LDFLAGS := $(LDFLAGS) -I/usr/local/include -L/usr/local/lib -lavformat -lavfilter -lavutil -lm -latomic -lz -lavcodec -pthread -lm -latomic -lz -lswresample -lm -latomic -lswscale -lm -latomic -lavutil -pthread -lm -latomic -I/usr/include/libdrm  #`pkg-config --cflags --libs libavformat libswscale`
_CPPFLAGS := $(CFLAGS)  -std=c++17

all: hello_drmprime

hello_drmprime: hello_drmprime.c drmprime_out.c
	$(CXX) -o $@ $^ $(_LDFLAGS) $(_CPPFLAGS)

clean:
	rm -rf hello_drmprime


