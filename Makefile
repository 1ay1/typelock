CXX      := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -O2
LDFLAGS  :=

# Dependencies
WL_CFLAGS  := $(shell pkg-config --cflags wayland-client)
WL_LIBS    := $(shell pkg-config --libs wayland-client)
CAIRO_FLAGS := $(shell pkg-config --cflags pangocairo cairo)
CAIRO_LIBS  := $(shell pkg-config --libs pangocairo cairo)
XKB_CFLAGS  := $(shell pkg-config --cflags xkbcommon)
XKB_LIBS    := $(shell pkg-config --libs xkbcommon)
PAM_LIBS    := -lpam

ALL_CFLAGS := $(CXXFLAGS) $(WL_CFLAGS) $(CAIRO_FLAGS) $(XKB_CFLAGS) -Isrc -Ibuild
ALL_LIBS   := $(WL_LIBS) $(CAIRO_LIBS) $(XKB_LIBS) $(PAM_LIBS) -lpthread

# Scanner
WAYLAND_SCANNER := wayland-scanner

# Sources
SRCS := src/main.cpp \
        src/wayland/client.cpp \
        src/render/renderer.cpp \
        src/auth/pam.cpp

OBJS := $(patsubst src/%.cpp,build/%.o,$(SRCS))

# Protocol generated files
PROTO_XML := protocols/ext-session-lock-v1.xml
PROTO_H   := build/ext-session-lock-v1-client.h
PROTO_C   := build/ext-session-lock-v1-protocol.c
PROTO_O   := build/ext-session-lock-v1-protocol.o

TARGET := build/typelock

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(PROTO_O) $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(ALL_LIBS)

# Protocol generation
$(PROTO_H): $(PROTO_XML)
	$(WAYLAND_SCANNER) client-header $< $@

$(PROTO_C): $(PROTO_XML)
	$(WAYLAND_SCANNER) private-code $< $@

$(PROTO_O): $(PROTO_C) $(PROTO_H)
	$(CC) -c -o $@ $< $(WL_CFLAGS)

# C++ compilation (depends on generated protocol header)
build/%.o: src/%.cpp $(PROTO_H)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -rf build/*

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/typelock
