#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <memory>
#include <random>
#include "path_oram.hpp"

class rORAM {
private:
    int ell; // Number of sub-ORAMs - 1 (max range size = 2^ell)
    
    std::vector<PathORAM> sub_orams;
    
    // Distributed position map: block_id -> array of bit-reversed positions (one per sub-ORAM)
    std::unordered_map<int, std::vector<int>> rpm;

    // Eviction counters for each sub-ORAM to determine eviction paths
    std::vector<int> eviction_counters;

    static int bit_reverse(int x, int bits);
    int        next_eviction_leaf(int sub_oram_idx);
    long       node_offset(int sub_oram_idx, int node_idx) const;

    Bucket read_node (int sub_oram_idx, int node_idx) const;
    void   write_node(int sub_oram_idx, int node_idx, const Bucket& b);
    std::vector<Bucket> read_bucket (int sub_oram_idx, int level, int p);
    void write_bucket(int sub_oram_idx, int level, int p, const std::vector<Bucket>& buckets);

    std::pair<std::vector<Block>, int> ReadRange(int sub_oram_idx, int start_addr);
    void BatchEvict(int sub_oram_idx, int num_evictions);

    mutable std::mt19937 rng;

    mutable long long total_node_read_count{0};
    mutable long long total_node_write_count{0};
public:
    rORAM(int N, int ell = 2, const std::string& filename_prefix = "roram");
    ~rORAM();

    void access(int start_addr, int range, const uint8_t* data_in, bool is_write, uint8_t* data_out);

    int get_ell() const { return ell; }

    long long get_total_path_reads() const;
    long long get_total_path_writes() const;
    long long get_total_node_reads() const;
    long long get_total_node_writes() const;
    void reset_counts();
};
