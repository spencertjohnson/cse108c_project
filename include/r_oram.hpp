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
    int N;
    int ell; // Number of sub-ORAMs - 1 (max range size = 2^ell)
    
    std::vector<PathORAM> sub_orams;
    
    // Eviction counters for each sub-ORAM to determine eviction paths
    int cnt{0};

    static int bit_reverse(int x, int bits);
    long       node_offset(int node_idx) const;

    std::vector<Bucket> read_buckets(int sub_oram_idx, int level, int p);
    void write_buckets(int sub_oram_idx, int level, int p, const std::vector<Bucket>& buckets);

    std::pair<std::vector<Block>, int> ReadRange(int sub_oram_idx, int start_addr);
    void BatchEvict(int sub_oram_idx, int num_evictions);

    mutable std::mt19937 rng;
    std::vector<uint8_t> io_buf;

    mutable long long total_seeks{0};
public:
    rORAM(int N, int ell = 2, const std::string& filename_prefix = "roram");
    ~rORAM();

    void access(int start_addr, int range, const uint8_t* data_in, bool is_write, uint8_t* data_out);

    int get_ell() const { return ell; }

    long long get_total_seeks() const { return total_seeks; }
    void reset_counts() { total_seeks = 0; }
};
