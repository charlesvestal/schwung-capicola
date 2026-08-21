# Host-side tests. Builds against the PATCHED lib in build/lib, never src/dsp/lib.
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter
ROOT     := $(shell cd ../.. && pwd)
LIB      := $(ROOT)/build/lib
INC      := -I$(LIB) -I$(ROOT)/src/dsp

BIN := $(ROOT)/build/tests

TESTS := test_lib_smoke

.PHONY: test clean patched
patched:
	@$(ROOT)/scripts/apply_patches.sh >/dev/null

test: patched $(addprefix $(BIN)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "== $$t"; $(BIN)/$$t; done

$(BIN)/test_lib_smoke: test_lib_smoke.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $< -o $@

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
