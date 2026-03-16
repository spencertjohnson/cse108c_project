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
    for (int k = 0; k < range_size; ++k) {
    if (start_addr + k < N)
        oram.set_position(start_addr + k, (p_prime + k) % num_leaves);
    }

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
    PathORAM& oram = sub_orams[sub_oram_idx];
    int range_size = 1 << sub_oram_idx;  // 2^i
    int L          = oram.get_L();

    int nodes_at_level = 1 << level;  // 2^j nodes at this level
    int num_buckets    = std::min(range_size, nodes_at_level);

    // Get the node index of the first bucket we need
    // node_at_level gives us the 1-indexed node for a given leaf and level
    int first_node = oram.node_at_level(p % oram.get_num_leaves(), level);
    long offset    = node_offset(sub_oram_idx, first_node);

    // One seek, one read — all num_buckets are contiguous on disk
    std::vector<uint8_t> buf(num_buckets * DISK_BUCKET_SIZE);
    std::fstream& f = oram.get_file();

    f.seekg(offset, std::ios::beg);
    total_seeks++;
    if (!f.good())
        throw std::runtime_error("read_bucket: seek failed at level "
                                 + std::to_string(level));

    f.read(reinterpret_cast<char*>(buf.data()), num_buckets * DISK_BUCKET_SIZE);
    if (!f.good())
        throw std::runtime_error("read_bucket: read failed at level "
                                 + std::to_string(level));

    // Deserialize
    std::vector<Bucket> buckets(num_buckets);
    for (int i = 0; i < num_buckets; ++i)
        buckets[i].deserialize(buf.data() + i * DISK_BUCKET_SIZE);

    return buckets;
}

void rORAM::BatchEvict(int sub_oram_idx, int k) {
    PathORAM& oram = sub_orams[sub_oram_idx];
    int L          = oram.get_L();
    int num_leaves = oram.get_num_leaves();

    // Convert cnt to bit-reversed eviction paths
    // cnt increments normally, but the actual paths are in bit-reversed order
    std::vector<int> eviction_paths;
    for (int t = cnt; t < cnt + k; ++t) {
        eviction_paths.push_back(bit_reverse(t % num_leaves, L));
    }

    // Steps 1-5: read buckets top-down
    for (int level = 0; level <= L; ++level) {
        // Use first eviction path as starting point — they are contiguous
        // in bit-reversed order so read_bucket reads them all in one seek
        std::vector<Bucket> buckets = read_buckets(sub_oram_idx, level, eviction_paths[0]);

        for (const Bucket& bucket : buckets) {
            for (int b = 0; b < Z; ++b) {
                const Block& block = bucket.blocks[b];
                if (block.is_dummy()) continue;

                bool already_in_stash = false;
                for (const Block& s : oram.get_stash())
                    if (s.id == block.id) { already_in_stash = true; break; }

                if (!already_in_stash)
                    oram.get_stash().push_back(block);
            }
        }
    }

    // Steps 6-11: evict bottom-up
    std::vector<std::vector<Bucket>> write_back(L + 1);

    for (int level = L; level >= 0; --level) {
        int nodes_at_level = 1 << level;
        int num_buckets    = std::min(k, nodes_at_level);
        write_back[level].resize(num_buckets);

        for (int idx = 0; idx < k; ++idx) {
            int r          = eviction_paths[idx] % nodes_at_level;
            int bucket_idx = idx % num_buckets;
            int slots      = 0;

            for (auto it = oram.get_stash().begin();
                 it != oram.get_stash().end() && slots < Z; ) {
                if (it->tags[sub_oram_idx] % nodes_at_level == r) {
                    write_back[level][bucket_idx].blocks[slots++] = *it;
                    it = oram.get_stash().erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // Steps 12-13: write back level by level
    for (int level = 0; level <= L; ++level) {
        write_buckets(sub_oram_idx, level, eviction_paths[0], write_back[level]);
    }

    oram.get_file().flush();
}

void rORAM::write_buckets(int sub_oram_idx, int level, int p, const std::vector<Bucket>& buckets) {
    PathORAM& oram = sub_orams[sub_oram_idx];
    int num_leaves = oram.get_num_leaves();

    int num_buckets = buckets.size();

    // Serialize all buckets into one contiguous buffer
    std::vector<uint8_t> buf(num_buckets * DISK_BUCKET_SIZE);
    for (int i = 0; i < num_buckets; ++i)
        buckets[i].serialize(buf.data() + i * DISK_BUCKET_SIZE);

    // One seek to the start of the contiguous range at this level
    int first_node = oram.node_at_level(p % num_leaves, level);
    long offset    = node_offset(sub_oram_idx, first_node);

    std::fstream& f = oram.get_file();
    f.seekp(offset, std::ios::beg);
    total_seeks++;
    if (!f.good())
        throw std::runtime_error("write_bucket: seek failed at level "
                                 + std::to_string(level));

    // One write — all buckets in one sequential disk operation
    f.write(reinterpret_cast<char*>(buf.data()), num_buckets * DISK_BUCKET_SIZE);
    if (!f.good())
        throw std::runtime_error("write_bucket: write failed at level "
                                 + std::to_string(level));
}

void rORAM::access(int start_addr, int range, const uint8_t* data_in,
                   bool is_write, uint8_t* data_out) {
    if (start_addr < 0 || start_addr + range > N)
        throw std::out_of_range("range out of bounds");
    if (range <= 0)
        throw std::invalid_argument("range must be > 0");

    // Algorithm 3 Step 1: i = ceil(log2(r))
    int i = (range > 1) ? (int)std::ceil(std::log2((double)range)) : 0;
    if (i > ell)
        throw std::invalid_argument("range exceeds max supported");

    int actual_range = 1 << i;  // 2^i

    // Step 2: a0 = floor(a / 2^i) * 2^i
    int a0 = (start_addr / actual_range) * actual_range;

    // Step 3: D <- {}
    // Map of block_id -> Block for all fetched blocks
    std::unordered_map<int, Block> fetched;

    // Steps 4-7: two ReadRanges on R_i
    for (int a_prime : {a0, a0 + actual_range}) {
        auto [blocks, p_prime] = ReadRange(i, a_prime);

        for (Block& b : blocks) {
            // Step 7: update tags[i] for all blocks in range
            // B_a'+j.pi <- p' + j
            int j = b.id - a_prime;
            b.tags[i] = (p_prime + j) % sub_orams[i].get_num_leaves();
            fetched[b.id] = b;
        }
    }

    // Steps 8-9: if write, update block data
    if (is_write) {
        for (int j = start_addr; j < start_addr + range; ++j) {
            if (fetched.count(j)) {
                std::memcpy(fetched[j].data, data_in + (j - start_addr) * BLOCK_SIZE, BLOCK_SIZE);
            } else {
                // Block not found — create it
                Block b(j, data_in + (j - start_addr) * BLOCK_SIZE);
                fetched[j] = b;
            }
        }
    }

    // Return data if reading
    if (!is_write && data_out != nullptr) {
        for (int j = start_addr; j < start_addr + range; ++j) {
            if (fetched.count(j) && !fetched[j].is_dummy())
                std::memcpy(data_out + (j - start_addr) * BLOCK_SIZE,
                            fetched[j].data, BLOCK_SIZE);
            else
                std::memset(data_out + (j - start_addr) * BLOCK_SIZE, 0, BLOCK_SIZE);
        }
    }

    // Steps 10-13: update stashes and BatchEvict in each sub-ORAM
    for (int j = 0; j <= ell; ++j) {
        // Step 11: remove stale blocks in range [a0, a0 + 2^(i+1)) from stash_j
        auto& stash = sub_orams[j].get_stash();
        stash.erase(
            std::remove_if(stash.begin(), stash.end(), [&](const Block& b) {
                return b.id >= a0 && b.id < a0 + 2 * actual_range;
            }),
            stash.end()
        );

        // Step 12: insert updated blocks into stash_j
        for (auto& [id, b] : fetched) {
            if (!b.is_dummy())
                stash.push_back(b);
        }

        // Step 13: BatchEvict(2^(i+1)) to sub-ORAM R_j
        BatchEvict(j, 2 * actual_range);
    }

    // Step 14: advance global eviction counter
    cnt += 2 * actual_range;
}
