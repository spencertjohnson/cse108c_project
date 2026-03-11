CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O0 -g
INCLUDES := -Iinclude

SRC_DIR  := src
OBJ_DIR  := build
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/oram
BENCHMARK := $(BIN_DIR)/benchmark

COMMON_SRCS := $(SRC_DIR)/path_oram.cpp $(SRC_DIR)/r_oram.cpp
COMMON_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(COMMON_SRCS))

MAIN_SRC := $(SRC_DIR)/main.cpp
MAIN_OBJ := $(OBJ_DIR)/main.o

BENCH_SRC := $(SRC_DIR)/benchmark.cpp
BENCH_OBJ := $(OBJ_DIR)/benchmark.o

.PHONY: all run clean benchmark

all: $(TARGET) $(BENCHMARK)

$(TARGET): $(MAIN_OBJ) $(COMMON_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BENCHMARK): $(BENCH_OBJ) $(COMMON_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

benchmark: $(BENCHMARK)
	./$(BENCHMARK)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
