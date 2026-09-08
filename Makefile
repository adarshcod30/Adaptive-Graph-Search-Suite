# Convenience wrapper. CMake is the supported build (see CMakeLists.txt);
# this exists so the project also builds with nothing but make + a compiler.

CXX      ?= c++
CXXSTD   ?= -std=c++20
OPT      ?= -O2
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wno-missing-field-initializers
INCLUDE  := -Iinclude -Ithird_party
CXXFLAGS ?= $(CXXSTD) $(OPT) $(WARN) $(INCLUDE)

BUILD    := build/make
CORE_SRC := $(wildcard src/*.cpp) $(wildcard src/algorithms/*.cpp) \
            $(wildcard src/analysis/*.cpp) $(wildcard src/transit/*.cpp)
CORE_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(CORE_SRC))
CLI_OBJ  := $(BUILD)/src/cli/main.o
TEST_SRC := $(wildcard tests/*.cpp)
TEST_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(TEST_SRC))

BIN      := bin/agss
TEST_BIN := bin/agss_tests

.PHONY: all clean test bench verify sanitize format help

all: $(BIN)

$(BIN): $(CORE_OBJ) $(CLI_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TEST_BIN): $(CORE_OBJ) $(TEST_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

test: $(TEST_BIN)
	./$(TEST_BIN)

## Build everything with ASan+UBSan, then run the suite under them.
sanitize:
	$(MAKE) clean
	$(MAKE) test OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"

bench: $(BIN)
	./$(BIN) bench

verify: $(BIN)
	./$(BIN) verify --graph data/cities/Gorakhpur --geo --samples 40

## Formats in place. CI pins clang-format 18.1.8; other versions disagree,
## so install the same one (pip install clang-format==18.1.8) to match.
format:
	@command -v clang-format >/dev/null && \
	  find src include tests \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
	    | xargs -0 clang-format -i && echo "formatted" || \
	  echo "clang-format not installed; pip install clang-format==18.1.8"

clean:
	rm -rf $(BUILD) $(BIN) $(TEST_BIN)

help:
	@echo "make            build bin/agss"
	@echo "make test       build and run the test suite"
	@echo "make sanitize   run the suite under ASan + UBSan"
	@echo "make bench      benchmark every algorithm across the bundled maps"
	@echo "make verify     differential correctness check"
