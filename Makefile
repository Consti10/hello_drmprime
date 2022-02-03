ifndef FFINSTALL
FFINSTALL=/usr
endif
CFLAGS=-I$(FFINSTALL)/include/arm-linux-gnueabihf -I/usr/include/libdrm
LDFLAGS=-L$(FFINSTALL)/lib/arm-linux-gnueabihf
LDLIBS=-lavcodec -lavfilter -lavutil -lavformat -ldrm -lpthread -lwiringPi

all: hello_drmprime

hello_drmprime: hello_drmprime.cpp drmprime_out.cpp extra.h drmprime_out.h
	$(CXX) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LDLIBS) -std=c++17

clean:
	rm -rf hello_drmprime


