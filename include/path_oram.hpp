#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <memory>
#include <random>
#include "components.hpp"


class PathORAM {
private:
    int N; // Number of blocks
    int L; // Height of the tree (levels). 

    int num_leaves; // number of leaves (power of 2)
    int num_nodes;  // total nodes = 2*num_leaves - 1

    int eviction_cnt = 0; // for eviction scheduling
    
    // Server Storage (Disk-backed)
    std::string tree_filename;
    mutable std::unique_ptr<std::fstream> tree_file;

    // Client Storage
    std::unordered_map<int, int> position_map; // block id -> leaf index [0..num_leaves-1]
    std::vector<Block> stash; // temporary blocks during access

    int random_leaf() const;

    // Disk Helpers
    void read_level(int leaf, int level, int k);
    void write_level(int leaf, int level, int k);
    void read_bucket(int node_idx);
    void write_bucket(int node_idx, int leaf_x, int level);
    void write_node(int node_idx, const Bucket& b);
    Bucket read_node(int node_idx) const;

    int get_bit_reversed_index(int idx, int bits) const;

    mutable long long path_read_count{0};
    mutable long long path_write_count{0};
    mutable long long node_read_count{0};
    mutable long long node_write_count{0};
    mutable long long seek_count{0};

    mutable std::mt19937 rng;

public:
    PathORAM(int N, const std::string& filename = "");
    ~PathORAM();

    void batch_evict(int k);

    PathORAM(PathORAM&&) noexcept = default;
    PathORAM& operator=(PathORAM&&) noexcept = default;

    void access(int block_id, const uint8_t* data, bool is_write, uint8_t *data_out);

    // functions for rORAM
    // ----------------------------------------------------------------

    // getters and setters
    int get_N() const { return N; }
    int get_L() const { return L; }
    int get_num_leaves() const { return num_leaves; }
    void set_position(int block_id, int leaf) { position_map[block_id] = leaf; }
    std::vector<Block>& get_stash() { return stash; }
    bool has_position(int block_id) const { return position_map.count(block_id) > 0; }
    int get_position(int block_id) const { return position_map.at(block_id); }



    // helper
    int node_at_level(int leaf, int level) const;
    std::fstream& get_file() const { return *tree_file; }

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
    long long get_seek_count() const { return seek_count; }

    void reset_counts() { 
        path_read_count = 0; path_write_count = 0; 
        node_read_count = 0; node_write_count = 0;
        seek_count = 0;
    }
};
