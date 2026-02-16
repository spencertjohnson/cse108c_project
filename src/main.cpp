#include "path_oram.hpp"
#include <iostream>
#include <cassert>

int main () {
    PathORAM oram(16, 4);

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