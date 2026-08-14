# Standalone build for the SRO stream test tools.
# Target platform includes RHEL with gcc 4.8, hence strict -std=c++11.
# No dependencies beyond libc, pthread and POSIX sockets.
#
#   make            -> builds sro_send and sro_recv in this directory
#   make clean

CXX      ?= g++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++11 -Wall -Wextra -pthread

all: sro_send sro_recv

sro_send: sro_send.cc sro_stream_common.h
	$(CXX) $(CXXFLAGS) sro_send.cc -o $@

sro_recv: sro_recv.cc sro_stream_common.h
	$(CXX) $(CXXFLAGS) sro_recv.cc -o $@

clean:
	rm -f sro_send sro_recv

.PHONY: all clean
