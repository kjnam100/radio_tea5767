CXXFLAGS=-Wall
OBJECTS=radio_tea5767.o
BINARIES=radio_tea5767
ALL_BINARIES=$(BINARIES)

# Where our library resides. It is split between includes and the binary
# library in lib
LDFLAGS+= -lm -lwiringPi

all : $(BINARIES)

radio : radio_tea5767.o
	$(CXX) $(CXXFLAGS) radio_tea5767.o -o $@ $(LDFLAGS)

%.o : %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(ALL_BINARIES)

FORCE:
.PHONY: FORCE
