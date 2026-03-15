#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <memory>
#include <random>
#include "components.hpp"

// TODO: Implement Path ORAM access functions for rORAM
// TODO: finished r oram hpp
// TODO: implement roram


class PathORAM {
private:
    int N; // Number of blocks
    int L; // Height of the tree (levels). 

    int num_leaves; // number of leaves (power of 2)
    int num_nodes;  // total nodes = 2*num_leaves - 1
    
    // Server Storage (Disk-backed)
    std::string tree_filename;
    mutable std::unique_ptr<std::fstream> tree_file;

    // Client Storage
    std::unordered_map<int, int> position_map; // block id -> leaf index [0..num_leaves-1]
    std::vector<Block> stash; // temporary blocks during access

    int random_leaf() const;

    // Disk Helpers
    void read_bucket(int node_idx);
    void write_bucket(int node_idx, int leaf_x, int level);
    Bucket read_node(int node_idx) const;
    void write_node(int node_idx, const Bucket& b);

    mutable long long path_read_count{0};
    mutable long long path_write_count{0};
    mutable long long node_read_count{0};
    mutable long long node_write_count{0};

    mutable std::mt19937 rng;

public:
    PathORAM(int N, const std::string& filename = "");
    ~PathORAM();

    void access(int block_id, const uint8_t* data, bool is_write, uint8_t *data_out);

    // functions for rORAM
    // ----------------------------------------------------------------

    // getters and setters
    int get_N() const { return N; }
    int get_L() const { return L; }
    int get_num_leaves() const { return num_leaves; }

    // helper
    int node_at_level(int leaf, int level) const;

    // ----------------------------------------------------------------

    // Test/inspection helpers
    int stash_size() const { return (int)stash.size(); }
    int get_leaf(int block_id) const { return position_map.at(block_id); }
    int num_leaves_count() const { return num_leaves; }

    void print_tree_structure() const;
    void print_path_to_leaf(int leaf) const;

    long long get_path_read_count() const { return (long long)path_read_count; }
    long long get_path_write_count() const { return (long long)path_write_count; }
    long long get_node_read_count() const { return node_read_count; }
    long long get_node_write_count() const { return node_write_count; }

    void reset_counts() { 
        path_read_count = 0; path_write_count = 0; 
        node_read_count = 0; node_write_count = 0;
    }
};
