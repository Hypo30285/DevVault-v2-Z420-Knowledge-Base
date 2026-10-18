# DevVault v2.0 — HP Z420 Native C++17 Build System
# Current CPU: Intel Xeon E5-2690 v1 (8C/16T Sandy Bridge, AVX 1.0, NO AVX2/FMA3/F16C)
# Planned CPU: Intel Xeon E5-2697 v2 (12C/24T Ivy Bridge, AVX 1.0 + F16C + RDRAND)

CXX := /usr/bin/g++
CXXFLAGS_COMMON := -std=c++17 -O3 -s -Wall -Wextra -Wno-unused-parameter -pthread
LDFLAGS := -ldl -pthread

# Default target for current Xeon E5-2690 v1 (8-core Sandy Bridge)
SANDYBRIDGE_FLAGS := -march=sandybridge -mavx -msse4.2 -mno-avx2 -mno-fma -mno-f16c

# Upgrade target for Xeon E5-2697 v2 (12-core Ivy Bridge)
IVYBRIDGE_FLAGS := -march=ivybridge -mavx -msse4.2 -mf16c -mno-avx2 -mno-fma

.PHONY: all sandybridge ivybridge clean

all: sandybridge

sandybridge: src/main.cpp src/db.hpp src/embed_avx.hpp src/mcp_server.hpp src/scrubber.hpp src/sha256.hpp src/sqlite_dl.hpp src/subsystems.hpp src/vision_minicpm.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS_COMMON) $(SANDYBRIDGE_FLAGS) src/main.cpp -o bin/devvault $(LDFLAGS)

ivybridge: src/main.cpp src/db.hpp src/embed_avx.hpp src/mcp_server.hpp src/scrubber.hpp src/sha256.hpp src/sqlite_dl.hpp src/subsystems.hpp src/vision_minicpm.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS_COMMON) $(IVYBRIDGE_FLAGS) src/main.cpp -o bin/devvault $(LDFLAGS)

clean:
	rm -f bin/devvault
