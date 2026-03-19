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
    int N;
    int L;
    int block_size;
    int num_leaves;
    int num_nodes;

    std::string tree_filename;
    mutable std::unique_ptr<std::fstream> tree_file;

    std::unordered_map<int, int> position_map;
    std::vector<Block> stash;

    int random_leaf() const;

    void read_bucket(int node_idx);
    void write_bucket(int node_idx, int leaf_x, int level);
    Bucket read_node(int node_idx) const;
    void write_node(int node_idx, const Bucket& b);

    mutable long long seek_count{0};
    mutable long long bytes_read{0};
    mutable long long bytes_written{0};
    mutable std::mt19937 rng;

public:
    PathORAM(int N, int block_size, const std::string& filename = "");
    ~PathORAM();
    PathORAM(PathORAM&&) noexcept = default;
    PathORAM& operator=(PathORAM&&) noexcept = default;

    void access(int block_id, const uint8_t* data_in, bool is_write, uint8_t* data_out);

    int get_N()         const { return N; }
    int get_L()         const { return L; }
    int get_block_size() const { return block_size; }
    int get_num_leaves() const { return num_leaves; }

    void set_position(int block_id, int leaf) { position_map[block_id] = leaf; }
    int  get_position(int block_id)  const { return position_map.at(block_id); }
    bool has_position(int block_id)  const { return position_map.count(block_id) > 0; }

    int  node_at_level(int leaf, int level) const;
    std::fstream& get_file() const { return *tree_file; }
    std::vector<Block>& get_stash() { return stash; }

    int  stash_size()   const { return (int)stash.size(); }
    int  get_leaf(int block_id) const { return position_map.at(block_id); }

    void print_tree_structure() const;
    void print_path_to_leaf(int leaf) const;

    long long get_seek_count()  const { return seek_count; }
    long long get_bytes_read()  const { return bytes_read; }
    long long get_bytes_written() const { return bytes_written; }
    long long get_bandwidth() const { return bytes_read + bytes_written; }
    void reset_counts() {
        seek_count = bytes_read = bytes_written = 0;
    }
};
