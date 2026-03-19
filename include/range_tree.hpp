#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include "path_oram.hpp"

// Stored in ORAMmeta — one per super-block at every height
// Serialized as 5 * 4 = 20 bytes
struct MetaNode {
    int as;        // lowest address in super-block
    int am;        // middle address (divides left/right child)
    int at;        // highest address in super-block
    int height;    // which height this node belongs to
    int oram_addr; // index of this super-block in orams[height]

    static constexpr int SIZE = 5 * sizeof(int);  // 20 bytes

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
    RangeTree(int N, int primitive_bs, const uint8_t* data,
              const std::string& prefix = "range_tree");
    ~RangeTree() = default;

    // Read all blocks in [s, t] into data_out
    // data_out must be (t - s + 1) * primitive_bs bytes
    // len = t - s + 1 must be a power of 2
    void access(int s, int t, uint8_t* data_out);

    int get_N() const { return N; }
    int get_L() const { return L; }

private:
    int N;           // number of primitive blocks
    int L;           // log2(N), max height
    int pbs;         // primitive block size in bytes

    // orams[i] stores all height-i super-blocks
    // each super-block is an atomic block of size 2^i * pbs
    // orams[i] holds N >> i blocks
    std::vector<PathORAM> orams;

    // meta_oram stores MetaNode objects (SIZE = 20 bytes each)
    // holds one node per super-block across all heights
    std::unique_ptr<PathORAM> meta_oram;

    // total number of meta nodes = sum over i of N/2^i = 2N - 1
    int total_meta_nodes;

    // ---- Build helpers ----
    void build(const uint8_t* data);

    // construct and load height-i super-blocks into orams[i]
    void build_height(int height,
                      const std::vector<std::vector<uint8_t>>& super_blocks);

    // construct and load metadata BST into meta_oram
    void build_meta(const std::vector<MetaNode>& nodes);

    // ---- Access helpers ----

    // oblivious BST search: find (at most) two height-i super-block
    // oram addresses that intersect [s, t]
    // always performs exactly 2L ORAMmeta accesses
    void find_super_blocks(int s, int t, int height,
                           int& addr1, int& addr2);
};
