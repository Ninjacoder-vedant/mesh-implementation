CXX      ?= g++
CC       ?= gcc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -fPIC -fvisibility=hidden -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter
CFLAGS   ?= -O2 -g
CFLAGS   += -Wall -Wextra

SRCS = src/arena.cc src/heap.cc src/meshing.cc src/wrapper.cc
HDRS = $(wildcard src/*.h)
TESTS = tests/test_basic tests/test_frag tests/test_threads

all: libmymesh.so $(TESTS)

# Link with gcc (not g++) so the library does not depend on libstdc++.
libmymesh.so: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -shared -nostdlib++ -o $@ $(SRCS) -ldl -lpthread

tests/%: tests/%.c
	$(CC) $(CFLAGS) -o $@ $< -lpthread -ldl

test: all
	./tests/run_tests.sh

clean:
	rm -f libmymesh.so $(TESTS)

.PHONY: all test clean
