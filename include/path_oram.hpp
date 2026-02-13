#pragma once
#include <vector>
#include <unordered_map>
#include "components.hpp"

class PathORAM {
private: 
    int N; // Number of blocks
    int Z; // Number of blocks per bucket
    int L; // Height of the tree

    // Server Storage
    std::vector<Bucket> tree; // The ORAM tree

    // Client Storage
    std::unordered_map<int, int> position_map; // Maps block ID to leaf index
    std::vector<Block> stash; // Temporary storage for blocks during access

    int random_leaf();
    std::vector<int> get_path(int leaf);

public:
    PathORAM(int N, int Z = 4);

    void access(int block_id, const std::string& data = "", bool is_write = false);
    
    void print_tree_structure();
};
