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
    int super_size = 1 << i;
    int num_leaves = sub_orams[i].get_num_leaves();
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);

    for (int start = 0; start < N; start += super_size) {
        int base_leaf = dist(rng);
        for (int k = 0; k < super_size && start + k < N; ++k) {
            int block_id         = start + k;
            int leaf             = (base_leaf + k) % num_leaves;
            const uint8_t* block = data + (long)block_id * BLOCK_SIZE;
            // Write with specific leaf — maintains consecutive layout
            sub_orams[i].access_with_remap(block_id, block, true, nullptr, leaf);
        }
    }
}

void ReadOnlyRangeORAM::read_super_block(int i, int a, uint8_t* out) {
    int super_size = 1 << i;
    int num_leaves = sub_orams[i].get_num_leaves();
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);

    // Pick new base leaf for consecutive remap
    int new_base = dist(rng);

    for (int k = 0; k < super_size && a + k < N; ++k) {
        int block_id  = a + k;
        int new_leaf  = (new_base + k) % num_leaves;
        uint8_t block_out[BLOCK_SIZE];

        // Read with specific new leaf — maintains consecutive layout
        sub_orams[i].access_with_remap(block_id, nullptr, false,
                                        block_out, new_leaf);
        std::memcpy(out + (long)k * BLOCK_SIZE, block_out, BLOCK_SIZE);
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
