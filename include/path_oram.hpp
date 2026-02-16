#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "components.hpp"

// TODO Clean up comments, make sure we aren't overcommenting

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

    std::string access(int block_id, const char* data = "", bool is_write = false);
    void read_path(int leaf);
    bool bucket_on_path(int bucket_node, int leaf) const;
    void write_path(std::vector<int> path);
    void remap_block(int block_id);
    int stash_update(int block_id, const char* data);

    void print_tree_structure() const;
    void print_path_to_leaf(int leaf) const;
    void encrypt_block(Block &b);
    void decrypt_block(Block &b);
};
