#include "r_oram.hpp"
#include <iostream>
#include <cmath>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <cstring>

rORAM::rORAM(int N_in,  int ell_in, const std::string& prefix) : N(N_in), ell(ell_in) {
    if (N <= 0) throw std::invalid_argument("N must be > 0");
    if (ell < 0) throw std::invalid_argument("ell must be >= 0");
    
    rng = std::mt19937{std::random_device{}()};

    for (int i = 0; i <= ell; ++i) {
        std::string fname = prefix + "_sub_" + std::to_string(i) + ".bin";
        sub_orams.emplace_back(N, fname);
    }

    eviction_counters.resize(ell + 1, 0);
    int num_leaves = sub_orams[0].get_num_leaves();

    for (int i = 0; i <= ell; ++i) {
        std::uniform_int_distribution<int> dist(0, num_leaves - 1);
        int chunk_size = 1 << i;  // 2^i blocks per range in R_i

        for (int start = 0; start < N; start += chunk_size) {
            // Pick a random starting leaf for this range
            int base_p = dist(rng);

            // Assign consecutive leaves to all blocks in the range
            for (int k = 0; k < chunk_size && start + k < N; ++k) {
                sub_orams[i].set_position(start + k, (base_p + k) % num_leaves);
            }
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

int rORAM::next_eviction_leaf(int sub_oram_idx) {
    int L = sub_orams[sub_oram_idx].get_L();
    int cnt = eviction_counters[sub_oram_idx];
    int leaf = bit_reverse(cnt % sub_orams[sub_oram_idx].get_num_leaves(), L);
    eviction_counters[sub_oram_idx]++;
    return leaf;
}

long rORAM::node_offset(int sub_oram_idx, int node_idx) const {
    int L          = sub_orams[sub_oram_idx].get_L();
    int num_leaves = sub_orams[sub_oram_idx].get_num_leaves();

    // Find the level of this node (root = level 0)
    // Node 1 = level 0, nodes 2-3 = level 1, nodes 4-7 = level 2, etc.
    int level = 0;
    int boundary = 1;
    while (node_idx >= 2 * boundary) {
        boundary <<= 1;
        level++;
    }

    // Position within this level (0-indexed)
    int pos_in_level = node_idx - boundary;

    // Bit-reverse the position within this level
    // Level l has 2^l nodes, so we reverse l bits
    int br_pos = bit_reverse(pos_in_level, level);

    // Count all nodes in levels before this one
    // Level 0 has 1 node, level 1 has 2, ..., level l has 2^l
    // Total before level l = 2^l - 1
    long nodes_before = (1 << level) - 1;

    return (nodes_before + br_pos) * (long)DISK_BUCKET_SIZE;
}

std::pair<std::vector<Block>, int> rORAM::ReadRange(int sub_oram_idx, int start_addr) {
    PathORAM& oram   = sub_orams[sub_oram_idx];
    int range_size   = 1 << sub_oram_idx;  // 2^i
    int num_leaves   = oram.get_num_leaves();
    int L            = oram.get_L();

    // Step 1: U = [start_addr, start_addr + 2^i)
    // Step 2: scan stash first for blocks in U
    std::vector<Block> result;
    for (const Block& b : oram.get_stash()) {
        if (!b.is_dummy() && b.id >= start_addr && b.id < start_addr + range_size)
            result.push_back(b);
    }

    // Step 3: p <- PM_i.query(start_addr)
    int p = oram.get_position(start_addr);

    // Steps 4-5: p' <- random, PM_i.update(start_addr, p')
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);
    int p_prime = dist(rng);
    oram.set_position(start_addr, p_prime);

    // Steps 6-9: read level by level, collect blocks in range
    // V = {v^(t mod 2^j)_j : t ∈ [p, p + 2^i)}
    for (int level = 0; level <= L; ++level) {
        std::vector<Bucket> buckets = read_buckets(sub_oram_idx, level, p);

        for (const Bucket& bucket : buckets) {
            for (int k = 0; k < Z; ++k) {
                const Block& b = bucket.blocks[k];
                if (b.is_dummy()) continue;

                // Step 9: add if in range U and not already found (handles duplicates)
                bool in_range = (b.id >= start_addr && b.id < start_addr + range_size);
                bool already_found = false;
                for (const Block& r : result)
                    if (r.id == b.id) { already_found = true; break; }

                if (in_range && !already_found)
                    result.push_back(b);
            }
        }
    }

    return {result, p_prime};
}

std::vector<Bucket> rORAM::read_buckets(int sub_oram_idx, int level, int p) {
    PathORAM& oram  = sub_orams[sub_oram_idx];
    int range_size  = 1 << sub_oram_idx;  // 2^i buckets to read
    int num_leaves  = oram.get_num_leaves();
    int L           = oram.get_L();

    // Find the starting physical offset — the first bucket in the range at this level
    int first_node = oram.node_at_level(p % num_leaves, level);
    long offset = node_offset(sub_oram_idx, first_node);

    // Calculate how many unique buckets we need at this level
    // At level j there are 2^j nodes, so we need min(2^i, 2^j) buckets
    int nodes_at_level = 1 << (L - level);  // wait, this isn't right
    int num_buckets = std::min(range_size, nodes_at_level);

    // Read all num_buckets contiguously in one seek
    std::vector<uint8_t> buf(num_buckets * DISK_BUCKET_SIZE);
    std::fstream& f = oram.get_file();
    f.seekg(offset, std::ios::beg);
    if (!f.good())
        throw std::runtime_error("read_bucket: seek failed at level " + std::to_string(level));
    f.read(reinterpret_cast<char*>(buf.data()), num_buckets * DISK_BUCKET_SIZE);
    if (!f.good())
        throw std::runtime_error("read_bucket: read failed at level " + std::to_string(level));

    // Deserialize each bucket
    std::vector<Bucket> buckets(num_buckets);
    for (int i = 0; i < num_buckets; ++i) {
        buckets[i].deserialize(buf.data() + i * DISK_BUCKET_SIZE);
    }

    return buckets;
}

void rORAM::BatchEvict(int sub_oram_idx, int num_evictions) {
    PathORAM& po = sub_orams[sub_oram_idx];
    int L = po.L;

    // Step 1: Determine target leaves for eviction
    std::vector<int> leaves;
    for (int k = 0; k < num_evictions; ++k) {
        int leaf = bit_reverse(po.eviction_counter % po.num_leaves, L);
        leaves.push_back(leaf);
        po.eviction_counter++;
    }

    // Step 2: Read Top-Down (0 to L)
    for (int level = 0; level <= L; ++level) {
        std::vector<int> unique_nodes;
        for (int leaf : leaves) {
            int node = po.node_at_level(leaf, level);
            if (std::find(unique_nodes.begin(), unique_nodes.end(), node) == unique_nodes.end()) {
                unique_nodes.push_back(node);
            }
        }
        for (int node : unique_nodes) {
            po.read_node_into_stash(node);
        }
    }

    // Step 3: Write Bottom-Up (L down to 0)
    for (int level = L; level >= 0; --level) {
        std::vector<int> unique_nodes;
        for (int leaf : leaves) {
            int node = po.node_at_level(leaf, level);
            if (std::find(unique_nodes.begin(), unique_nodes.end(), node) == unique_nodes.end()) {
                unique_nodes.push_back(node);
            }
        }
        for (int node : unique_nodes) {
            po.write_node_from_stash(node);
        }
    }

    // Flush once per batch eviction
    if (po.tree_file && po.tree_file->is_open()) po.tree_file->flush();
}

std::vector<std::string> rORAM::access(int start_addr, int range, bool is_write, const std::vector<std::string>& data) {
    if (start_addr < 0 || start_addr + range > N) throw std::out_of_range("range out of bounds");
    if (range <= 0) throw std::invalid_argument("range size must be > 0");
    if (is_write && data.size() != (size_t)range) throw std::invalid_argument("data size mismatch");
    
    int i = (range > 1) ? (int)std::ceil(std::log2((double)range)) : 0;
    if (i > ell) throw std::invalid_argument("range size exceeds max supported");
    int actual_range = 1 << i;
    int a0 = (start_addr / actual_range) * actual_range;
    
    // 1. & 2. Perform two ReadRanges to cover the range [start_addr, start_addr + range)
    std::vector<Block> fetched_blocks;
    for (int a_prime : {a0, a0 + actual_range}) {
        auto read_res = ReadRange(i, a_prime);
        for (const auto& b : read_res.first) {
            fetched_blocks.push_back(b);
        }
    }
    
    // 3. Remap
    static thread_local std::mt19937 rng{std::random_device{}()};
    for (int a_prime : {a0, a0 + actual_range}) {
        std::vector<int> p_primes_per_suboram(ell + 1);
        for (int j = 0; j <= ell; ++j) {
            std::uniform_int_distribution<int> d(0, sub_orams[j].num_leaves_count() - 1);
            p_primes_per_suboram[j] = d(rng);
        }

        for (int k = 0; k < actual_range; ++k) {
            int addr = a_prime + k;
            if (addr < N) {
                for (int j = 0; j <= ell; ++j) {
                    rpm[addr][j] = (p_primes_per_suboram[j] + k) % sub_orams[j].num_leaves_count();
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
        
        for (auto b : fetched_blocks) {
            if (!b.is_dummy) {
                b.tags = rpm[b.id]; 
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

long long rORAM::get_total_node_reads() const {
    long long total = 0;
    for (const auto& oram : sub_orams) {
        total += oram.get_node_read_count();
    }
    return total;
}

long long rORAM::get_total_node_writes() const {
    long long total = 0;
    for (const auto& oram : sub_orams) {
        total += oram.get_node_write_count();
    }
    return total;
}

void rORAM::reset_counts() {
    for (auto& oram : sub_orams) {
        oram.reset_counts();
    }
}