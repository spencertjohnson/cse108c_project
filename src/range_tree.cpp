#include "range_tree.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <filesystem>

RangeTree::RangeTree(int N_actual, int primitive_bs, const uint8_t* data, const std::string& prefix) : pbs(primitive_bs) {
    if (N_actual <= 0)
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
    for (int j = 0; j < N; ++j) {
        if (j < N_actual)
            height_data[0][j].assign(data + (long)j * pbs, data + (long)(j + 1) * pbs);
        else
            height_data[0][j].assign(pbs, 0);
    }

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
            mn.as = j * sb_size;
            mn.at = (j + 1) * sb_size - 1;
            mn.am = mn.as + std::max(1, sb_size >> 1) - 1;
            mn.height = i;
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

void RangeTree::find_super_blocks(int s, int t, int height, int& addr1, int& addr2) {
    int offset = 0;
    for (int k = 0; k < height; ++k)
        offset += (N >> k);

    int num_sb = N >> height;
    int sb1    = s >> height;
    int sb2    = t >> height;

    addr1 = sb1;
    addr2 = (sb1 == sb2) ? -1 : sb2;

    // Always perform exactly 2L meta accesses for obliviousness
    std::vector<uint8_t> buf(MetaNode::SIZE);
    int accesses = 0;

    for (int level = 0; level < L; ++level) {
        // Access sb1's meta node
        int meta_idx1 = offset + sb1;
        meta_oram->access(meta_idx1, nullptr, false, buf.data());
        ++accesses;

        // Access sb2's meta node if different, else dummy
        int meta_idx2 = (sb1 != sb2)
                        ? offset + sb2
                        : offset + ((sb1 + 1) % num_sb);
        meta_oram->access(meta_idx2, nullptr, false, buf.data());
        ++accesses;

        if (accesses >= 2 * L) break;
    }
}

void RangeTree::access(int s, int t, uint8_t* data_out) {
    if (s < 0 || t >= N || s > t)
        throw std::out_of_range("range [s,t] out of bounds");

    int len = t - s + 1;

    // picking height
    int height = 0;
    while ((1 << height) < len) ++height;
    height = std::min(height, L);

    int sb_size = 1 << height;

    int addr1, addr2;
    find_super_blocks(s, t, height, addr1, addr2);

    int super_block_bytes = sb_size * pbs;
    std::vector<uint8_t> buf1(super_block_bytes, 0);
    std::vector<uint8_t> buf2(super_block_bytes, 0);

    orams[height].access(addr1, nullptr, false, buf1.data());

    if (addr2 != -1) {
        orams[height].access(addr2, nullptr, false, buf2.data());
    } else {
        std::uniform_int_distribution<int> dist(0, (N >> height) - 1);
        int dummy = dist(rng);
        if (dummy == addr1) dummy = (addr1 + 1) % (N >> height);
        orams[height].access(dummy, nullptr, false, buf2.data());
    }

    // Extract [s, t] from the two fetched super-blocks
    int sb1_start = addr1 * sb_size;
    int sb2_start = (addr2 != -1) ? addr2 * sb_size : -1;

    for (int addr = s; addr <= t; ++addr) {
        int out_offset = (addr - s) * pbs;
        if (addr >= sb1_start && addr < sb1_start + sb_size) {
            int src_offset = (addr - sb1_start) * pbs;
            std::memcpy(data_out + out_offset, buf1.data() + src_offset, pbs);
        } else {
            int src_offset = (addr - sb2_start) * pbs;
            std::memcpy(data_out + out_offset, buf2.data() + src_offset, pbs);
        }
    }
}
