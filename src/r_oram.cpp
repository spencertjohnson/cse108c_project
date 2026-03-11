#include "r_oram.hpp"
#include <iostream>
#include <cmath>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <cstring>

rORAM::rORAM(int N_in, int Z_in, int ell_in, const std::string& prefix) : N(N_in), Z(Z_in), ell(ell_in) {
    if (N <= 0) throw std::invalid_argument("N must be > 0");
    if (Z <= 0) throw std::invalid_argument("Z must be > 0");
    if (ell < 0) throw std::invalid_argument("ell must be >= 0");

    for (int i = 0; i <= ell; ++i) {
        std::string fname = prefix + "_sub_" + std::to_string(i) + ".bin";
        sub_orams.emplace_back(N, Z, fname, ell + 1);
    }
    
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, sub_orams[0].num_leaves_count() - 1);
    
    for (int id = 0; id < N; id++) {
        rpm[id] = std::vector<int>(ell + 1, 0);
    }
    
    for (int i = 0; i <= ell; ++i) {
        std::uniform_int_distribution<int> dist(0, sub_orams[i].num_leaves_count() - 1);
        int chunk_size = 1 << i;
        for (int start = 0; start < N; start += chunk_size) {
            int base_p = dist(rng);
            for (int k = 0; k < chunk_size && start + k < N; ++k) {
                rpm[start + k][i] = (base_p + k) % sub_orams[i].num_leaves_count();
            }
        }
    }

    // Sync sub_oram position_maps to initial rpm
    for (int i = 0; i <= ell; ++i) {
        for (int id = 0; id < N; id++) {
            sub_orams[i].position_map[id] = bit_reverse(rpm[id][i], sub_orams[i].L);
        }
    }
}

int rORAM::bit_reverse(int x, int bits) {
    int res = 0;
    for (int i = 0; i < bits; ++i) {
        res = (res << 1) | (x & 1);
        x >>= 1;
    }
    return res;
}

std::pair<std::vector<Block>, int> rORAM::ReadRange(int sub_oram, int start_addr) {
    int L = sub_orams[sub_oram].L;
    int num_leaves = sub_orams[sub_oram].num_leaves_count();
    int actual_range = 1 << sub_oram; 
    int old_br_start = (start_addr < N) ? rpm[start_addr][sub_oram] : 0; // Use dummy path 0 if OOB
    
    // Generate a single new random base position for this sub-ORAM (p')
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);
    int p_prime = dist(rng);
    
    // Read actual_range consecutive paths in bit-reversed order
    for (int k = 0; k < actual_range; ++k) {
        int phys_idx = (old_br_start + k) % num_leaves;
        int leaf = bit_reverse(phys_idx, L);
        sub_orams[sub_oram].read_path(leaf);
    }
    
    // Retrieve blocks
    std::vector<Block> range_blocks;
    for (int k = 0; k < actual_range; ++k) {
        int addr = start_addr + k;
        bool found = false;
        for (Block& b : sub_orams[sub_oram].stash) {
            if (b.id == addr && !b.is_dummy) {
                range_blocks.push_back(b);
                found = true;
                break;
            }
        }
        if (!found) {
            Block empty_block(addr, "");
            range_blocks.push_back(empty_block);
        }
    }
    
    return {range_blocks, p_prime};
}

void rORAM::BatchEvict(int sub_oram, int num_evictions) {
    PathORAM& po = sub_orams[sub_oram];
    for (int i = 0; i < num_evictions; ++i) {
        int leaf = bit_reverse(po.eviction_counter % po.num_leaves, po.L);
        po.read_path(leaf);
        po.write_path(po.get_path(leaf));
        po.eviction_counter++;
    }
}

std::vector<std::string> rORAM::access(int start_addr, int range, bool is_write, const std::vector<std::string>& data) {
    if (start_addr < 0 || start_addr + range > N) throw std::out_of_range("range out of bounds");
    if (range <= 0) throw std::invalid_argument("range size must be > 0");
    if (is_write && data.size() != (size_t)range) throw std::invalid_argument("data size mismatch");
    
    int i = (range > 1) ? (int)std::ceil(std::log2((double)range)) : 0;
    if (i > ell) throw std::invalid_argument("range size exceeds max supported");
    int actual_range = 1 << i;
    int a0 = (start_addr / actual_range) * actual_range;
    int L = sub_orams[i].L;
    
    // 1. & 2. Perform two ReadRanges to cover the range [start_addr, start_addr + range)
    std::vector<Block> fetched_blocks;
    std::vector<int> p_primes;
    for (int a_prime : {a0, a0 + actual_range}) {
        auto read_res = ReadRange(i, a_prime);
        for (const auto& b : read_res.first) {
            fetched_blocks.push_back(b);
        }
        p_primes.push_back(read_res.second);
    }
    
    // 3. Remap
    int idx = 0;
    for (int a_prime : {a0, a0 + actual_range}) {
        int p_prime = p_primes[idx++];
        
        for (int k = 0; k < actual_range; ++k) {
            int addr = a_prime + k;
            if (addr < N) {
                // Update position map for all sub-ORAMs as requested
                for (int j = 0; j <= ell; ++j) {
                    rpm[addr][j] = (p_prime + k) % sub_orams[j].num_leaves_count();
                    sub_orams[j].position_map[addr] = bit_reverse(rpm[addr][j], sub_orams[j].L);
                }
            }
        }
    }
    
    // 4. Update data if writing
    std::vector<std::string> results;
    if (!is_write) {
        results.resize(range);
    }
    
    for (auto& b : fetched_blocks) {
        if (b.id >= start_addr && b.id < start_addr + range) {
            if (is_write) {
                std::memset(b.data, 0, BLOCK_SIZE);
                std::snprintf(b.data, BLOCK_SIZE, "%s", data[b.id - start_addr].c_str());
                b.is_dummy = false;
            } else if (!b.is_dummy) {
                results[b.id - start_addr] = std::string(b.data);
            }
        }
    }
    
    // 5. Update stashes and evict in each tree
    for (int j = 0; j <= ell; ++j) {
        auto it = std::remove_if(sub_orams[j].stash.begin(), sub_orams[j].stash.end(),
            [a0, actual_range](const Block& b) {
                return b.id >= a0 && b.id < a0 + 2 * actual_range;
            });
        sub_orams[j].stash.erase(it, sub_orams[j].stash.end());
        
        for (auto b : fetched_blocks) {  // pass by value to modify locally if needed
            if (!b.is_dummy) {
                b.tags = rpm[b.id]; // Sync the Block's internal distributed tag map
                sub_orams[j].stash.push_back(b);
            }
        }
        
        int num_evictions = 2 * actual_range;
        BatchEvict(j, num_evictions);
    }
    
    return results;
}

long long rORAM::get_total_path_reads() const {
    long long total = 0;
    for (const auto& oram : sub_orams) {
        total += oram.get_path_read_count();
    }
    return total;
}

long long rORAM::get_total_path_writes() const {
    long long total = 0;
    for (const auto& oram : sub_orams) {
        total += oram.get_path_write_count();
    }
    return total;
}

void rORAM::reset_counts() {
    for (auto& oram : sub_orams) {
        oram.reset_counts();
    }
}
