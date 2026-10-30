# Builds hyprweave.so against the installed Hyprland headers.

ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b
PKGS      = pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon

SRC = $(wildcard src/*.cpp)
HDR = $(wildcard src/*.hpp)

all: hyprweave.so

# Build to a temp file and rename, so a copy that Hyprland has loaded is never rewritten in place.
hyprweave.so: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(SRC) -o $@.tmp `pkg-config --cflags $(PKGS)`
	mv $@.tmp $@

clean:
	rm -f hyprweave.so hyprweave.so.tmp

.PHONY: all clean
