#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "components.hpp"

class PathORAM {
private:
    int N; // Number of blocks
    int Z; // Number of blocks per bucket
    int L; // Height of the tree (levels). Example: L=4 => 8 leaves if root is level 0? (we define below)

    int num_leaves; // number of leaves (power of 2)
    int num_nodes;  // total nodes = 2*num_leaves - 1

    // Server Storage
    std::vector<Bucket> tree; // The ORAM tree

    // Client Storage
    std::unordered_map<int, int> position_map; // block id -> leaf index [0..num_leaves-1]
    std::vector<Block> stash; // temporary blocks during access

    int random_leaf() const;
    std::vector<int> get_path(int leaf) const; // returns node indices root->leaf

public:
    PathORAM(int N, int Z = 4);

    void access(int block_id, const std::string& data = "", bool is_write = false);

    void print_tree_structure() const;
    void print_path_to_leaf(int leaf) const;
};
