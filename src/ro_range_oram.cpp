#include "ro_range_oram.hpp"
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <iostream>


ReadOnlyRangeORAM::ReadOnlyRangeORAM(int N_in, int ell_in, const uint8_t* data, const std::string& prefix)
    : N(N_in), ell(ell_in)
{
    if (N <= 0)   throw std::invalid_argument("N must be > 0");
    if (ell < 0)  throw std::invalid_argument("ell must be >= 0");

    rng = std::mt19937{std::random_device{}()};

    sub_orams.reserve(ell + 1);
    rpm.resize(ell + 1);

    // Create ell+1 sub-ORAMs and initialize each one
    for (int i = 0; i <= ell; ++i) {
        std::string fname = prefix + "_sub_" + std::to_string(i) + ".bin";
        sub_orams.emplace_back(N, fname);
        init_sub_oram(i, data);
    }
}


ReadOnlyRangeORAM::~ReadOnlyRangeORAM() = default;


void ReadOnlyRangeORAM::init_sub_oram(int i, const uint8_t* data) {
    PathORAM& oram    = sub_orams[i];
    int super_size    = 1 << i;
    int num_leaves    = oram.get_num_leaves();
    int num_groups    = num_leaves / super_size;
    int num_sb        = (N + super_size - 1) / super_size;

    std::uniform_int_distribution<int> dist(0, num_groups - 1);
    std::fstream& f = oram.get_file();

    for (int sb = 0; sb < num_sb; ++sb) {
        int a         = sb * super_size;
        int base_leaf = dist(rng) * super_size;

        for (int k = 0; k < super_size; ++k) {
            int block_id = a + k;
            if (block_id >= N) break;

            int leaf = base_leaf + k;
            oram.set_position(block_id, leaf);

            Bucket b;
            b.blocks[0].id = block_id;
            std::memcpy(b.blocks[0].data,
                        data + (long)block_id * BLOCK_SIZE,
                        BLOCK_SIZE);

            int  node   = num_leaves + leaf;
            long offset = (long)(node - 1) * DISK_BUCKET_SIZE;
            uint8_t buf[DISK_BUCKET_SIZE];
            b.serialize(buf);
            f.seekp(offset, std::ios::beg);
            f.write(reinterpret_cast<char*>(buf), DISK_BUCKET_SIZE);
        }
    }
    f.flush();
}


void ReadOnlyRangeORAM::read_super_block(int i, int a, uint8_t* out) {
    PathORAM& oram  = sub_orams[i];
    int super_size  = 1 << i;
    int num_leaves  = oram.get_num_leaves();
    int num_groups  = num_leaves / super_size;
    int L           = oram.get_L();

    // Get old position then immediately remap
    int old_base_leaf = oram.get_position(a);
    std::uniform_int_distribution<int> dist(0, num_groups - 1);
    int new_base_leaf = dist(rng) * super_size;
    for (int k = 0; k < super_size; ++k) {
        int block_id = a + k;
        if (block_id >= N) break;
        oram.set_position(block_id, new_base_leaf + k);
    }

    // Read top-down, one seek per level
    std::vector<Block> found(super_size);
    std::vector<bool>  got(super_size, false);
    std::fstream& f = oram.get_file();

    for (int level = 0; level <= L; ++level) {
        int first_node = oram.node_at_level(old_base_leaf,                  level);
        int last_node  = oram.node_at_level(old_base_leaf + super_size - 1, level);
        int n_nodes    = last_node - first_node + 1;

        long offset = (long)(first_node - 1) * DISK_BUCKET_SIZE;
        std::vector<uint8_t> buf((long)n_nodes * DISK_BUCKET_SIZE);
        f.seekg(offset, std::ios::beg);
        ++total_seeks;
        f.read(reinterpret_cast<char*>(buf.data()), buf.size());

        for (int n = 0; n < n_nodes; ++n) {
            Bucket bucket;
            bucket.deserialize(buf.data() + (long)n * DISK_BUCKET_SIZE);
            for (int k = 0; k < Z; ++k) {
                const Block& blk = bucket.blocks[k];
                if (blk.is_dummy()) continue;
                int off = blk.id - a;
                if (off < 0 || off >= super_size) continue;
                if (!got[off]) {
                    found[off] = blk;
                    got[off]   = true;
                }
            }
        }
    }

    // Copy to output
    for (int k = 0; k < super_size; ++k)
        if (got[k])
            std::memcpy(out + (long)k * BLOCK_SIZE, found[k].data, BLOCK_SIZE);

    evict_super_block(i, a, old_base_leaf, found);
}


// Scan stash and tree level by level, return all blocks in range [a, a+super_size)
std::vector<Block> ReadOnlyRangeORAM::scan_levels(int i, int a, int start_leaf, int super_size) {
    PathORAM& oram     = sub_orams[i];
    int L              = oram.get_L();
    int nodes_at_level, num_buckets, start_pos, level_base;

    std::vector<Block> found;

    // Check stash first
    for (const Block& b : oram.get_stash())
        if (!b.is_dummy() && b.id >= a && b.id < a + super_size)
            found.push_back(b);

    // Read each level
    for (int level = 0; level <= L; ++level) {
        nodes_at_level = 1 << level;
        num_buckets    = std::min(super_size, nodes_at_level);
        start_pos      = start_leaf % nodes_at_level;
        level_base     = nodes_at_level;

        if (start_pos + num_buckets <= nodes_at_level) {
            read_level_chunk(i, level_base, start_pos, num_buckets, a, super_size, found);
        } else {
            int first_part  = nodes_at_level - start_pos;
            int second_part = num_buckets - first_part;
            read_level_chunk(i, level_base, start_pos, first_part,  a, super_size, found);
            read_level_chunk(i, level_base, 0,          second_part, a, super_size, found);
        }
    }
    return found;
}

// Read count buckets starting at from within a level, collect relevant blocks
void ReadOnlyRangeORAM::read_level_chunk(int i, int level_base, int from, int count, int a, int super_size, std::vector<Block>& found) {
    PathORAM& oram = sub_orams[i];
    std::fstream& f = oram.get_file();

    long offset = (long)(level_base + from - 1) * DISK_BUCKET_SIZE;
    std::vector<uint8_t> buf(count * DISK_BUCKET_SIZE);
    f.seekg(offset, std::ios::beg);
    ++total_seeks;
    f.read(reinterpret_cast<char*>(buf.data()), count * DISK_BUCKET_SIZE);

    for (int b = 0; b < count; ++b) {
        Bucket bucket;
        bucket.deserialize(buf.data() + b * DISK_BUCKET_SIZE);
        for (int k = 0; k < Z; ++k) {
            const Block& blk = bucket.blocks[k];
            if (blk.is_dummy()) continue;

            bool in_range = (blk.id >= a && blk.id < a + super_size);
            bool already  = false;
            for (const Block& fb : found)
                if (fb.id == blk.id) { already = true; break; }

            if (in_range && !already) {
                found.push_back(blk);
            } else if (!in_range) {
                bool in_stash = false;
                for (const Block& s : oram.get_stash())
                    if (s.id == blk.id) { in_stash = true; break; }
                if (!in_stash)
                    oram.get_stash().push_back(blk);
            }
        }
    }
}

// Update position map and stash with new consecutive leaves
void ReadOnlyRangeORAM::remap_super_block(int i, int a,
                                           int super_size, int new_base) {
    PathORAM& oram     = sub_orams[i];
    int num_leaves     = oram.get_num_leaves();

    for (int k = 0; k < super_size && a + k < N; ++k)
        oram.set_position(a + k, (new_base + k) % num_leaves);
}

// Evict bottom-up level by level
void ReadOnlyRangeORAM::evict_levels(int i, int start_leaf, int super_size) {
    PathORAM& oram = sub_orams[i];
    int L          = oram.get_L();

    for (int level = L; level >= 0; --level)
        oram.evict_level(start_leaf, level, std::min(super_size, 1 << level));
}


void ReadOnlyRangeORAM::read(int start_addr, int range, uint8_t* data_out) {
    if (start_addr < 0 || start_addr + range > N)
        throw std::out_of_range("range out of bounds");
    if (range <= 0)
        throw std::invalid_argument("range must be > 0");

    // i = ceil(log2(range))
    int i = (range > 1) ? (int)std::ceil(std::log2((double)range)) : 0;
    if (i > ell)
        throw std::invalid_argument("range exceeds max supported");

    int super_block_size = 1 << i;  // 2^i

    // a0 = aligned start address
    int a0 = (start_addr / super_block_size) * super_block_size;
    int a1 = (a0 + super_block_size) % N;

    // Read both super-blocks
    std::vector<uint8_t> buf0(super_block_size * BLOCK_SIZE);
    std::vector<uint8_t> buf1(super_block_size * BLOCK_SIZE);

    read_super_block(i, a0, buf0.data());
    read_super_block(i, a1, buf1.data());

    // Extract [start_addr, start_addr + range) from the two super-blocks
    for (int k = 0; k < range; ++k) {
        int addr       = start_addr + k;
        uint8_t* src;
        int      offset;

        if (addr >= a0 && addr < a0 + super_block_size) {
            src    = buf0.data();
            offset = addr - a0;
        } else {
            src    = buf1.data();
            offset = addr - a1;
        }

        std::memcpy(data_out + (long)k * BLOCK_SIZE,
                    src + (long)offset * BLOCK_SIZE,
                    BLOCK_SIZE);
    }
}


void ReadOnlyRangeORAM::reset_counts() {
    total_seeks = 0;
    for (auto& oram : sub_orams)
        oram.reset_counts();
}
