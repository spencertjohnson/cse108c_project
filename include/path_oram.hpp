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
    int L; // Height of the tree (levels). 

    int num_leaves; // number of leaves (power of 2)
    int num_nodes;  // total nodes = 2*num_leaves - 1

    // Server Storage
    std::vector<Bucket> tree; // The ORAM tree

    // Client Storage
    std::unordered_map<int, int> position_map; // block id -> leaf index [0..num_leaves-1]
    std::vector<Block> stash; // temporary blocks during access

    int random_leaf() const;
    std::vector<int> get_path(int leaf) const;

    void read_path(int leaf);
    void write_path(const std::vector<int>& path);
    bool bucket_on_path(int bucket_node, int leaf) const;
    void remap_block(int block_id);
    int stash_update(int block_id, const char* data);

public:
    PathORAM(int N, int Z = 4);

    std::string access(int block_id, const char* data = "", bool is_write = false);

    int stash_size() const { return (int)stash.size(); }
    int get_leaf(int block_id) const { return position_map.at(block_id); }
    int num_leaves_count() const { return num_leaves; }

    void print_tree_structure() const;
    void print_path_to_leaf(int leaf) const;
    static void encrypt_block(Block &b);
    static void decrypt_block(Block &b);
};
