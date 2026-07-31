# pikostore -- Software Center for the piko Zaurus ROM.
#
# This is normally built by tools/build-pikostore.sh in the piko repo,
# which knows where the cross toolchain and the staged X11/FLTK tree live.
# This Makefile exists so the app can also be built directly, either by
# that script or by hand when iterating on it.
#
# It deliberately links against a STAGED libfltk rather than a host one:
# there is no host build of this app that means anything, since it reads
# /etc/zaurus/* and runs /usr/sbin/piko-update.
#
#   make STAGE=/path/to/userspace/stage-target \
#        CXX=arm-unknown-linux-uclibcgnueabi-g++
#
# FLTK_LDLIBS should be the LDLIBS line out of the FLTK build's
# makeinclude -- that is the exact library set configure settled on (Xft,
# Xrender, fontconfig, X11, ...) and re-deriving it by hand here would
# drift from build-fltk.sh's --disable-* choices.

STAGE       ?= ../../stage-target
CXX         ?= g++
CXXFLAGS    ?= -O2 -Wall -Wextra
FLTK_LDLIBS ?= -lXft -lXrender -lfontconfig -lXext -lX11 -lm -lpthread

BIN  := pikostore
SRC  := pikostore.cxx

all: $(BIN)

# -isystem, not -I: FLTK 1.3's own headers emit hundreds of
# -Wunused-parameter warnings that would bury any real warning in
# pikostore.cxx itself.
$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) \
	    -isystem $(STAGE)/usr/include \
	    -o $@ $(SRC) \
	    -L$(STAGE)/usr/lib -Wl,-rpath-link=$(STAGE)/usr/lib \
	    -lfltk $(FLTK_LDLIBS)

clean:
	rm -f $(BIN)

.PHONY: all clean
