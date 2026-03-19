#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include "path_oram.hpp"

struct MetaNode {
    int as;        // lowest address in super-block
    int am;        // middle address (divides left/right child)
    int at;        // highest address in super-block
    int height;    // which height this node belongs to
    int oram_addr; // index of this super-block in orams[height]

    static constexpr int SIZE = 5 * sizeof(int);

    void serialize(uint8_t* buf) const {
        std::memcpy(buf,      &as,        4);
        std::memcpy(buf +  4, &am,        4);
        std::memcpy(buf +  8, &at,        4);
        std::memcpy(buf + 12, &height,    4);
        std::memcpy(buf + 16, &oram_addr, 4);
    }

    void deserialize(const uint8_t* buf) {
        std::memcpy(&as,        buf,      4);
        std::memcpy(&am,        buf +  4, 4);
        std::memcpy(&at,        buf +  8, 4);
        std::memcpy(&height,    buf + 12, 4);
        std::memcpy(&oram_addr, buf + 16, 4);
    }

    bool is_dummy() const { return as == -1; }
};

class RangeTree {
public:
    // N               : number of primitive blocks (power of 2)
    // primitive_bs    : size of one primitive block in bytes
    // data            : raw data, N * primitive_bs bytes
    // prefix          : filename prefix for ORAM backing files
    RangeTree(int N, int primitive_bs, const uint8_t* data, const std::string& prefix = "range_tree");
    ~RangeTree() = default;

    void access(int s, int t, uint8_t* data_out);

    int get_N() const { return N; }
    int get_L() const { return L; }

    // aggregate counters across all internal ORAMs + meta_oram
    long long get_seek_count() const {
        long long total = meta_oram->get_seek_count();
        for (const auto& o : orams) total += o.get_seek_count();
        return total;
    }
    long long get_bytes_read() const {
        long long total = meta_oram->get_bytes_read();
        for (const auto& o : orams) total += o.get_bytes_read();
        return total;
    }
    long long get_bytes_written() const {
        long long total = meta_oram->get_bytes_written();
        for (const auto& o : orams) total += o.get_bytes_written();
        return total;
    }
    long long get_bandwidth() const { return get_bytes_read() + get_bytes_written(); }

    void reset_counts() {
        meta_oram->reset_counts();
        for (auto& o : orams) o.reset_counts();
    }

private:
    int N;           // number of primitive blocks
    int L;           // log2(N), max height
    int pbs;         // primitive block size in bytes
    mutable std::mt19937 rng;

    std::vector<PathORAM> orams;
    std::unique_ptr<PathORAM> meta_oram;

    int total_meta_nodes;

    // ---- Build helpers ----
    void build(const uint8_t* data, int N_actual);
    void build_height(int height, const std::vector<std::vector<uint8_t>>& super_blocks);
    void build_meta(const std::vector<MetaNode>& nodes);

    // ---- Access helpers ----
    void find_super_blocks(int s, int t, int height, int& addr1, int& addr2);
};
