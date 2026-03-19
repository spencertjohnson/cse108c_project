#include "ro_range_oram.hpp"
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>


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
    PathORAM& oram    = sub_orams[i];
    int super_size    = 1 << i;         // 2^i blocks
    int L             = oram.get_L();
    int num_leaves    = oram.get_num_leaves();
    int start_leaf = sub_orams[i].get_position(a);

    // Read level by level — at each level, super_size consecutive
    // buckets are physically adjacent due to locality-sensitive mapping
    // so we can read them all in one seek
    std::vector<Block> found_blocks;

    for (int level = 0; level <= L; ++level) {
        int nodes_at_level = 1 << level;
        int num_buckets    = std::min(super_size, nodes_at_level);
        int start_pos      = start_leaf % nodes_at_level;

        long offset = (long)(oram.node_at_level(start_leaf, level) - 1)
                      * DISK_BUCKET_SIZE;

        std::vector<uint8_t> buf(num_buckets * DISK_BUCKET_SIZE);
        std::fstream& f = oram.get_file();
        f.seekg(offset, std::ios::beg);
        ++total_seeks;
        f.read(reinterpret_cast<char*>(buf.data()),
               num_buckets * DISK_BUCKET_SIZE);

        // Scan all buckets at this level for blocks in our range
        for (int b = 0; b < num_buckets; ++b) {
            Bucket bucket;
            bucket.deserialize(buf.data() + b * DISK_BUCKET_SIZE);
            for (int k = 0; k < Z; ++k) {
                const Block& blk = bucket.blocks[k];
                if (!blk.is_dummy()) {
                    // Check not already found (keep newest via position map)
                    bool already = false;
                    for (const Block& fb : found_blocks)
                        if (fb.id == blk.id) { already = true; break; }
                    if (!already)
                        found_blocks.push_back(blk);
                }
            }
        }
    }

    // Copy found blocks into output buffer in order
    for (const Block& blk : found_blocks) {
        // Compute offset within the super-block
        // super-block starts at a0 which is start_leaf for R_i
        int offset_in_sb = blk.id % super_size;
        std::memcpy(out + (long)offset_in_sb * BLOCK_SIZE,
                    blk.data, BLOCK_SIZE);
    }

    // Remap all blocks in this super-block (PathORAM invariant)
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);
    int new_base_leaf = dist(rng);
    for (int k = 0; k < super_size; ++k) {
        int block_id = (start_leaf / super_size) * super_size + k;
        if (block_id < N)
            oram.set_position(block_id, (new_base_leaf + k) % num_leaves);
    }
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
