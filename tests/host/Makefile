# Host-side tests. Builds against the PATCHED lib in build/lib, never src/dsp/lib.
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter
ROOT     := $(shell cd ../.. && pwd)
LIB      := $(ROOT)/build/lib
INC      := -I$(LIB) -I$(ROOT)/src/dsp

BIN := $(ROOT)/build/tests

TESTS := test_lib_smoke test_params test_engine test_plugin

ENGINE_SRC := $(ROOT)/src/dsp/capicola_engine.cpp $(ROOT)/src/dsp/capicola_params.cpp

.PHONY: test clean patched wav
patched:
	@$(ROOT)/scripts/apply_patches.sh >/dev/null

test: patched $(addprefix $(BIN)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "== $$t"; $(BIN)/$$t; done

$(BIN)/test_lib_smoke: test_lib_smoke.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $< -o $@

$(BIN)/test_params: test_params.cpp $(ROOT)/src/dsp/capicola_params.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

$(BIN)/test_engine: test_engine.cpp $(ENGINE_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

$(BIN)/test_plugin: test_plugin.cpp $(ROOT)/src/dsp/capicola_plugin.cpp $(ENGINE_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

$(BIN)/wav_runner: wav_runner.cpp $(ENGINE_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

wav: patched $(BIN)/wav_runner
	@echo "built $(BIN)/wav_runner"

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
