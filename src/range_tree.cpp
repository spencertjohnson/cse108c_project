#include "range_tree.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <filesystem>

RangeTree::RangeTree(int N_actual, int primitive_bs, const uint8_t* data, const std::string& prefix) : pbs(primitive_bs) {
    if (N_actual <= 0 || (N_actual & (N_actual - 1)) != 0)
        throw std::invalid_argument("N must be a power of 2 and > 0");
    if (pbs <= 0)
        throw std::invalid_argument("primitive block size must be > 0");

    N = 1;
    while (N < N_actual)
        N <<= 1;

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

    build(data, N_actual);
}

RangeTree::~RangeTree() = default;

void RangeTree::build(const uint8_t* data, int N_actual) {
    std::vector<std::vector<std::vector<uint8_t>>> height_data(L + 1);

    // Height 0: just the primitive blocks directly from data
    height_data[0].resize(N);
    for (int j = 0; j < N_actual; ++j)
        height_data[0][j].assign(data + (long)j * pbs, data + (long)(j + 1) * pbs);

    // Heights 1..L: each super-block = left child concatenated with right child
    for (int i = 1; i <= L; ++i) {
        int num_sb    = N >> i;
        int child_size = (1 << (i - 1)) * pbs;
        int sb_size    = 2 * child_size;
        height_data[i].resize(num_sb);
        for (int j = 0; j < num_sb; ++j) {
            height_data[i][j].resize(sb_size);
            std::memcpy(height_data[i][j].data(), height_data[i - 1][2 * j].data(), child_size);
            std::memcpy(height_data[i][j].data() + child_size, height_data[i - 1][2 * j + 1].data(), child_size);
        }
    }

    // Load each height into its ORAM
    for (int i = 0; i <= L; ++i)
        build_height(i, height_data[i]);

    // Build and load metadata
    std::vector<MetaNode> nodes;
    nodes.reserve(total_meta_nodes);
    for (int i = 0; i <= L; ++i) {
        int num_sb  = N >> i;
        int sb_size = 1 << i;
        for (int j = 0; j < num_sb; ++j) {
            MetaNode mn;
            mn.as        = j * sb_size;
            mn.at        = (j + 1) * sb_size - 1;
            mn.am        = mn.as + (sb_size >> 1) - 1;
            mn.height    = i;
            mn.oram_addr = j;
            nodes.push_back(mn);
        }
    }
    build_meta(nodes);
}

void RangeTree::build_height(int height, const std::vector<std::vector<uint8_t>>& super_blocks) {
    for (int j = 0; j < (int)super_blocks.size(); ++j)
        orams[height].access(j, super_blocks[j].data(), true, nullptr);
}

void RangeTree::build_meta(const std::vector<MetaNode>& nodes) {
    std::vector<uint8_t> buf(MetaNode::SIZE);
    for (int idx = 0; idx < (int)nodes.size(); ++idx) {
        nodes[idx].serialize(buf.data());
        meta_oram->access(idx, buf.data(), true, nullptr);
    }
}

