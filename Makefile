CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
INCLUDES = -Iinclude
SRC = src/main.cpp src/path_oram.cpp
OUT = oram

all:
	$(CXX) $(CXXFLAGS) $(SRC) $(INCLUDES) -o $(OUT)

clean:
	rm -f $(OUT)
