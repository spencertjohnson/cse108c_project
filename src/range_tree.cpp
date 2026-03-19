#include "range_tree.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <filesystem>

RangeTree::RangeTree(int N_in, int primitive_bs, const uint8_t* data, const std::string& prefix) : N(N_in), pbs(primitive_bs) {
    if (N <= 0 || (N & (N - 1)) != 0)
        throw std::invalid_argument("N must be a power of 2 and > 0");
    if (pbs <= 0)
        throw std::invalid_argument("primitive block size must be > 0");

    L = (int)std::log2((double)N);

    orams.reserve(L + 1);

    for (int i = 0; i <= L; ++i) {
        int num_super_blocks = N >> i;
        int super_block_size = (1 << i) * pbs;
        std::string fname = prefix + "_height_" + std::to_string(i) + ".bin";
        orams.emplace_back(num_super_blocks, super_block_size, fname);
    }

    total_meta_nodes = 2 * N - 1;
    std::string meta_fname = prefix + "_meta.bin";
    meta_oram = std::make_unique<PathORAM>(total_meta_nodes, MetaNode::SIZE, meta_fname);

    build(data);
}

RangeTree::~RangeTree() = default;
