CC=gcc
CFLAGS=-Wall 
OBJECTS=radio_tea5767.o
BINARIES=radio_tea5767
LDFLAGS+= -lm -lwiringPi

all : $(BINARIES)

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $< $(LDFLAGS)

clean:
	rm -f $(OBJECTS) $(BINARIES)

FORCE:
.PHONY: FORCE
