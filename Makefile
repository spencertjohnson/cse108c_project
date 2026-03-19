CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O0 -g
INCLUDES := -Iinclude
SRC_DIR  := src
OBJ_DIR  := build
BIN_DIR  := bin
DATA_DIR := data

# -----------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------

TARGET           := $(BIN_DIR)/oram
BENCHMARK        := $(BIN_DIR)/benchmark
PATHORAM_TARGET  := $(BIN_DIR)/oram_test

# -----------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------

# Full build — PathORAM + rORAM
COMMON_SRCS := $(SRC_DIR)/path_oram.cpp
COMMON_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(COMMON_SRCS))

# PathORAM only — rORAM excluded
PATHORAM_SRCS := $(SRC_DIR)/path_oram.cpp
PATHORAM_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(PATHORAM_SRCS))

MAIN_SRC  := $(SRC_DIR)/main.cpp
MAIN_OBJ  := $(OBJ_DIR)/main.o

BENCH_SRC := $(SRC_DIR)/benchmark.cpp
BENCH_OBJ := $(OBJ_DIR)/benchmark.o

VERIFY_LAYOUT_OBJ := $(OBJ_DIR)/verify_layout.o

# -----------------------------------------------------------------------
# Phony targets
# -----------------------------------------------------------------------

.PHONY: all pathoram run run_test benchmark verify clean

# -----------------------------------------------------------------------
# Full build
# -----------------------------------------------------------------------

all: $(TARGET) $(BENCHMARK) $(VERIFY_LAYOUT)

$(TARGET): $(MAIN_OBJ) $(COMMON_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BENCHMARK): $(BENCH_OBJ) $(COMMON_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(VERIFY_LAYOUT): $(VERIFY_LAYOUT_OBJ) $(COMMON_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

# -----------------------------------------------------------------------
# PathORAM only (rORAM not compiled)
# -----------------------------------------------------------------------

pathoram: $(PATHORAM_TARGET)

$(PATHORAM_TARGET): $(MAIN_OBJ) $(PATHORAM_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

# -----------------------------------------------------------------------
# Compile rules
# -----------------------------------------------------------------------

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# -----------------------------------------------------------------------
# Run
# -----------------------------------------------------------------------

run: $(TARGET)
	./$(TARGET)

run_test: $(PATHORAM_TARGET)
	./$(PATHORAM_TARGET)

benchmark: $(BENCHMARK)
	./$(BENCHMARK)

verify: $(VERIFY_LAYOUT)
	./$(VERIFY_LAYOUT)

# -----------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(DATA_DIR)
