#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "path_oram.hpp"

class rORAM {
private:
    int N; // Number of blocks
    int Z; // Number of blocks per bucket
    int ell; // Number of sub-ORAMs - 1 (max range size = 2^ell)
    
    std::vector<PathORAM> sub_orams; // R[0..ell]
    
    // Distributed position map: block_id -> array of bit-reversed positions (one per sub-ORAM)
    std::unordered_map<int, std::vector<int>> rpm;

    static int bit_reverse(int x, int bits);
    std::pair<std::vector<Block>, int> ReadRange(int sub_oram, int start_addr);
    void BatchEvict(int sub_oram, int num_evictions);
    
public:
    rORAM(int N, int Z = 4, int ell = 2, const std::string& filename_prefix = "roram");
    std::vector<std::string> access(int start_addr, int range, bool is_write = false, const std::vector<std::string>& data = {});
    int get_ell() const { return ell; }

    long long get_total_path_reads() const;
    long long get_total_path_writes() const;
    long long get_total_node_reads() const;
    long long get_total_node_writes() const;
    void reset_counts();
};
