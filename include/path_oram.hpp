#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <memory>
#include "components.hpp"

// TODO Clean up comments, make sure we aren't overcommenting

class PathORAM {
    friend class rORAM;
private:
    int N; // Number of blocks
    int Z; // Number of blocks per bucket
    int L; // Height of the tree (levels). 
    int eviction_counter{0}; // Tracks progress through bit-reversed eviction sequence

    int num_leaves; // number of leaves (power of 2)
    int num_nodes;  // total nodes = 2*num_leaves - 1
    
    // Server Storage (Disk-backed)
    int tags_count; // Number of tags per block (for rORAM)
    std::string tree_filename;
    mutable std::unique_ptr<std::fstream> tree_file;

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

    // Node-level helpers for BatchEvict
    int node_at_level(int leaf, int level) const;
    void read_node_into_stash(int node_idx);
    void write_node_from_stash(int node_idx);

    // Disk Helpers
    Bucket read_node(int node_idx) const;
    void write_node(int node_idx, const Bucket& b);

public:
    PathORAM(int N, int Z = 4, const std::string& filename = "", int tags_count = 0);
    ~PathORAM();

    // Movable but not copyable (due to unique_ptr)
    PathORAM(PathORAM&&) noexcept = default;
    PathORAM& operator=(PathORAM&&) noexcept = default;
    PathORAM(const PathORAM&) = delete;
    PathORAM& operator=(const PathORAM&) = delete;

    std::string access(int block_id, const char* data = "", bool is_write = false);

    // Test/inspection helpers
    int stash_size() const { return (int)stash.size(); }
    int get_leaf(int block_id) const { return position_map.at(block_id); }
    int num_leaves_count() const { return num_leaves; }

    void print_tree_structure() const;
    void print_path_to_leaf(int leaf) const;
    static void encrypt_block(Block &b);
    static void decrypt_block(Block &b);

    long long get_path_read_count() const { return (long long)path_read_count; }
    long long get_path_write_count() const { return (long long)path_write_count; }
    long long get_node_read_count() const { return node_read_count; }
    long long get_node_write_count() const { return node_write_count; }

    void reset_counts() { 
        path_read_count = 0; path_write_count = 0; 
        node_read_count = 0; node_write_count = 0;
    }

    long node_offset(int node_idx) const;
    int get_bit_reversed_index(int node_idx) const;
    size_t get_block_size() const;
    size_t get_bucket_size() const;

private:
    mutable double path_read_count{0};
    mutable double path_write_count{0};
    mutable long long node_read_count{0};
    mutable long long node_write_count{0};
};
