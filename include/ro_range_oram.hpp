#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <random>
#include "path_oram.hpp"


class ReadOnlyRangeORAM {
public:
    // ell        — max range size supported = 2^ell
    ReadOnlyRangeORAM(int N, int ell, const uint8_t* data, const std::string& prefix = "ro_range_oram");
    ~ReadOnlyRangeORAM();

    void read(int start_addr, int range, uint8_t* data_out);

    int get_ell() const { return ell; }
    int get_N()   const { return N; }

    void reset_counts();
    long long get_total_seeks() const { return total_seeks; }

private:
    int N;
    int ell;

    std::vector<PathORAM> sub_orams;

    // Position map: rpm[i][super_block_id] = leaf in R_i
    // super_block_id = block_id / 2^i
    std::vector<std::unordered_map<int, int>> rpm;

    mutable std::mt19937 rng;
    mutable long long total_seeks{0};

    void init_sub_oram(int i, const uint8_t* data);

    void read_super_block(int i, int a, uint8_t* out);
    void evict_super_block(int i, int a, int old_base_leaf, const std::vector<Block>& blocks);

    std::vector<Block> scan_levels(int i, int a, int start_leaf, int super_size);
    void read_level_chunk(int i, int level_base, int from, int count, int a, int super_size, std::vector<Block>& found);
    void remap_super_block(int i, int a, int super_size, int new_base);
    void evict_levels(int i, int start_leaf, int super_size);
};
