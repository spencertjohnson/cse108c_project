#include "path_oram.hpp"
#include <iostream>
#include <cassert>
#include <string>

int main () {
    const int DEFAULT_N = 16;
    const int DEFAULT_Z = 4;

    int N = DEFAULT_N;
    int Z = DEFAULT_Z;

    std::string line;

    std::cout << "Enter number of nodes N [default " << DEFAULT_N << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        try { N = std::stoi(line); }
        catch (...) {
            std::cerr << "Invalid input for N, using default (" << DEFAULT_N << ").\n";
            N = DEFAULT_N;
        }
    }

    std::cout << "Enter nodes per bucket Z [default " << DEFAULT_Z << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        try { Z = std::stoi(line); }
        catch (...) {
            std::cerr << "Invalid input for Z, using default (" << DEFAULT_Z << ").\n";
            Z = DEFAULT_Z;
        }
    }

    std::cout << "Creating PathORAM with N=" << N << ", Z=" << Z << "\n\n";

    PathORAM oram(N, Z);
    oram.print_tree_structure();

    oram.access(3, "hello", true);
    oram.access(7, "world", true);

    std::string a = oram.access(3, "", false);
    std::string b = oram.access(7, "", false);

    std::cout << "read(3) = " << a << "\n";
    std::cout << "read(7) = " << b << "\n";

    assert(a == "hello");
    assert(b == "world");

    oram.print_tree_structure();

    std::cout << "PASS\n";
    return 0;
}