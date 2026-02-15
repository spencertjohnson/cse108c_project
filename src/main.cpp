#include "path_oram.hpp"
#include <iostream>

int main () {
    PathORAM oram(16, 4);

    oram.print_tree_structure();
    oram.print_path_to_leaf(0);
    oram.print_path_to_leaf(3);
    oram.print_path_to_leaf(7);
    return 0;
}