ifndef FFINSTALL
FFINSTALL=/usr
endif
CFLAGS=-I$(FFINSTALL)/include/arm-linux-gnueabihf -I/usr/include/libdrm
LDFLAGS=-L$(FFINSTALL)/lib/arm-linux-gnueabihf
LDLIBS=-lavcodec -lavfilter -lavutil -lavformat -ldrm -lpthread

all: hello_drmprime

hello_drmprime: hello_drmprime.c drmprime_out.c
	$(CXX) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf hello_drmprime


