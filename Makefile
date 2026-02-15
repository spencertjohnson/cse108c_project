CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O0 -g
INCLUDES := -Iinclude

SRC_DIR  := src
OBJ_DIR  := build
BIN_DIR  := bin
TARGET   := $(BIN_DIR)/oram

SRCS := $(SRC_DIR)/main.cpp $(SRC_DIR)/path_oram.cpp
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
