CC=gcc
UNAME_S := $(shell uname -s)
CFLAGS = -g 

ifeq ($(UNAME_S),Darwin)
CFLAGS +=  -I/usr/local/include/libusb-1.0
LIBS += -lusb-1.0
endif

ifeq ($(UNAME_S),Linux)
CFLAGS +=  -I/usr//include/libusb-1.0
LIBS += -lusb-1.0 -L/usr/lib/x86_64-linux-gnu/ -lavahi-client -lavahi-common
endif

# uncomment this for SunOS
# LIBS = -lsocket -lnsl

all: maws

maws: maws.o 
	$(CC) -o maws maws.o $(LIBS)


clean:
	rm -f maws maws.o 
