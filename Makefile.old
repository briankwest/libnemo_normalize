# Makefile for libnemo_normalize
#
# Expects OpenFst and Thrax headers/libs to be available.
# In the Docker container, conda installs them to /opt/conda.

CXX       ?= g++
CC        ?= gcc
CXXFLAGS  ?= -std=c++17 -O2 -Wall -fPIC
CFLAGS    ?= -std=c11 -O2 -Wall

# Conda paths (set by Docker build or override on command line)
CONDA_PREFIX ?= /opt/conda
INC          = -I$(CONDA_PREFIX)/include -I.
LDFLAGS_LIB  = -L$(CONDA_PREFIX)/lib -lfst -lfstfar -ldl
RPATH        = -Wl,-rpath,$(CONDA_PREFIX)/lib -Wl,-rpath,.

.PHONY: all clean test

all: libnemo_normalize.so test_normalize

# Shared library
libnemo_normalize.so: nemo_normalize.cpp nemo_normalize.h
	$(CXX) $(CXXFLAGS) $(INC) -shared -o $@ nemo_normalize.cpp $(LDFLAGS_LIB) $(RPATH)

# Test program (compiled as C, linked against C++ shared lib)
test_normalize: test_normalize.c nemo_normalize.h libnemo_normalize.so
	$(CC) $(CFLAGS) -I. -o $@ test_normalize.c -L. -lnemo_normalize $(RPATH) -lm

clean:
	rm -f libnemo_normalize.so test_normalize

test: test_normalize
	LD_LIBRARY_PATH=.:$(CONDA_PREFIX)/lib ./test_normalize
