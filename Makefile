CXX      := g++
CC       := gcc
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -O2
LDFLAGS  :=

# Dependencies
WL_CFLAGS   := $(shell pkg-config --cflags wayland-client)
WL_LIBS     := $(shell pkg-config --libs wayland-client)
CAIRO_FLAGS := $(shell pkg-config --cflags pangocairo cairo)
CAIRO_LIBS  := $(shell pkg-config --libs pangocairo cairo)
XKB_CFLAGS  := $(shell pkg-config --cflags xkbcommon)
XKB_LIBS    := $(shell pkg-config --libs xkbcommon)
SD_CFLAGS   := $(shell pkg-config --cflags libsystemd)
SD_LIBS     := $(shell pkg-config --libs libsystemd)
PAM_LIBS    := -lpam

ALL_CFLAGS := $(CXXFLAGS) $(WL_CFLAGS) $(CAIRO_FLAGS) $(XKB_CFLAGS) $(SD_CFLAGS) -Isrc -Ibuild
ALL_LIBS   := $(WL_LIBS) $(CAIRO_LIBS) $(XKB_LIBS) $(SD_LIBS) $(PAM_LIBS) -lpthread

# Scanner
WAYLAND_SCANNER := wayland-scanner

# Sources
SRCS := src/main.cpp \
        src/wayland/client.cpp \
        src/render/renderer.cpp \
        src/render/blur.cpp \
        src/auth/pam.cpp \
        src/auth/fingerprint.cpp \
        src/config/parser.cpp

OBJS := $(patsubst src/%.cpp,build/%.o,$(SRCS))

# Protocol files
PROTOCOLS := ext-session-lock-v1 \
             wlr-screencopy-unstable-v1 \
             wlr-output-power-management-unstable-v1

PROTO_HEADERS := $(patsubst %,build/%-client.h,$(PROTOCOLS))
PROTO_SOURCES := $(patsubst %,build/%-protocol.c,$(PROTOCOLS))
PROTO_OBJS    := $(patsubst %,build/%-protocol.o,$(PROTOCOLS))

TARGET      := build/typelock
TEST_TARGET := build/test_core

TEST_SRCS := tests/main.cpp \
             tests/test_machine.cpp \
             tests/test_types.cpp \
             tests/test_proofs.cpp
TEST_OBJS := $(patsubst tests/%.cpp,build/tests/%.o,$(TEST_SRCS))

.PHONY: all clean install test

all: $(TARGET)

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

build/tests/%.o: tests/%.cpp $(wildcard src/core/*.hpp) tests/harness.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc -Itests -c -o $@ $<

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

$(TARGET): $(PROTO_OBJS) $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(ALL_LIBS)

# Protocol generation (pattern rules)
build/%-client.h: protocols/%.xml
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) client-header $< $@

build/%-protocol.c: protocols/%.xml
	@mkdir -p $(dir $@)
	$(WAYLAND_SCANNER) private-code $< $@

build/%-protocol.o: build/%-protocol.c build/%-client.h
	$(CC) -c -o $@ $< $(WL_CFLAGS)

# C++ compilation (depends on all generated protocol headers)
build/%.o: src/%.cpp $(PROTO_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -rf build/*

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/typelock
