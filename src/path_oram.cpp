#include "path_oram.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <filesystem>

PathORAM::PathORAM(int N_in, const std::string& filename) 
    : N(N_in), tree_filename(filename) {
    if (N <= 0) throw std::invalid_argument("N must be > 0");

    L          = (N > 1) ? (int)ceil(log2((double)N)) : 1;
    num_leaves = 1 << L;
    num_nodes  = 2 * num_leaves - 1;

    rng = std::mt19937{std::random_device{}()};

    if (tree_filename.empty())
        tree_filename = "data/path_oram_tree.bin";

    std::filesystem::path p(tree_filename);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());

    for (int id = 0; id < N; id++)
        position_map[id] = random_leaf();

    tree_file = std::make_unique<std::fstream>();
    tree_file->open(tree_filename,
        std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (!tree_file->is_open())
        throw std::runtime_error("Failed to open tree file: " + tree_filename);

    Bucket dummy;
    for (int i = 1; i <= num_nodes; ++i)
        write_node(i, dummy);

    tree_file->flush();
}

PathORAM::~PathORAM() {
    if (tree_file && tree_file->is_open())
        tree_file->close();
    std::filesystem::remove(tree_filename);
}

int PathORAM::random_leaf() const {
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);
    return dist(rng);
}

void PathORAM::print_tree_structure() const {
    std::cout << "PathORAM N=" << N
              << " L=" << L
              << " leaves=" << num_leaves
              << " nodes=" << num_nodes
              << "\n";
    int show = std::min(num_leaves, 4);
    for (int i = 0; i < show; i++)
        print_path_to_leaf(i);
}

void PathORAM::print_path_to_leaf(int leaf) const {
    std::cout << "leaf " << leaf << ": ";
    for (int i = 0; i <= L; ++i) {
        std::cout << node_at_level(leaf, i);
        if (i < L) std::cout << " -> ";
    }
    std::cout << "\n";
}

void PathORAM::access(int block_id, const uint8_t* data_in,
                      bool is_write, uint8_t* data_out) {
    if (block_id < 0 || block_id >= N)
        throw std::invalid_argument("block_id out of range");

    // Line 1: look up current leaf
    auto it = position_map.find(block_id);
    int x = (it != position_map.end()) ? it->second : random_leaf();

    // Line 2: remap
    position_map[block_id] = random_leaf();

    // Lines 3-5: read path into stash
    for (int level = 0; level <= L; ++level)
        read_bucket(node_at_level(x, level));

    // Line 6: read block from stash
    if (data_out != nullptr)
        std::memset(data_out, 0, BLOCK_SIZE);
    for (Block& b : stash) {
        if (b.id == block_id) {
            if (data_out != nullptr)
                std::memcpy(data_out, b.data, BLOCK_SIZE);
            break;
        }
    }

    // Lines 7-9: update if write
    if (is_write) {
        bool found = false;
        for (Block& b : stash) {
            if (b.id == block_id) {
                std::memcpy(b.data, data_in, BLOCK_SIZE);
                found = true;
                break;
            }
        }
        if (!found)
            stash.emplace_back(block_id, data_in);
    }

    // Lines 10-15: write path back
    for (int level = L; level >= 0; --level)
        write_bucket(node_at_level(x, level), x, level);

    ++path_read_count;
    ++path_write_count;
}

// -------------------------------------------------------------------
// Access Helpers
// -------------------------------------------------------------------

int PathORAM::node_at_level(int leaf, int level) const {
    return (num_leaves + leaf) >> (L - level);
}

void PathORAM::read_bucket(int node_idx) {
    Bucket b = read_node(node_idx);
    for (int i = 0; i < Z; ++i) {
        if (!b.blocks[i].is_dummy())
            stash.push_back(b.blocks[i]);
    }
}

void PathORAM::write_bucket(int node_idx, int leaf_x, int level) {
    Bucket b;
    int slots = 0;
    for (auto it = stash.begin(); it != stash.end() && slots < Z; ) {
        auto pos_it = position_map.find(it->id);
        if (pos_it == position_map.end()) {
            ++it;
            continue;
        }
        int block_leaf = pos_it->second;
        bool can_place = (node_at_level(leaf_x, level) ==
                          node_at_level(block_leaf, level));
        if (can_place) {
            b.blocks[slots++] = *it;
            it = stash.erase(it);
        } else {
            ++it;
        }
    }
    write_node(node_idx, b);
}

// -------------------------------------------------------------------
// Disk Implementation
// -------------------------------------------------------------------

Bucket PathORAM::read_node(int node_idx) const {
    Bucket b;
    long offset = (long)(node_idx - 1) * DISK_BUCKET_SIZE;

    tree_file->seekg(offset, std::ios::beg);
    ++seek_count;
    if (!tree_file->good())
        throw std::runtime_error("read_node: seek failed at node "
                                 + std::to_string(node_idx));

    uint8_t buf[DISK_BUCKET_SIZE];
    tree_file->read(reinterpret_cast<char*>(buf), DISK_BUCKET_SIZE);
    if (!tree_file->good())
        throw std::runtime_error("read_node: read failed at node "
                                 + std::to_string(node_idx));

    b.deserialize(buf);
    ++node_read_count;
    return b;
}

void PathORAM::write_node(int node_idx, const Bucket& b) {
    long offset = (long)(node_idx - 1) * DISK_BUCKET_SIZE;

    tree_file->seekp(offset, std::ios::beg);
    ++seek_count;
    if (!tree_file->good())
        throw std::runtime_error("write_node: seek failed at node "
                                 + std::to_string(node_idx));

    uint8_t buf[DISK_BUCKET_SIZE];
    b.serialize(buf);
    tree_file->write(reinterpret_cast<char*>(buf), DISK_BUCKET_SIZE);
    if (!tree_file->good())
        throw std::runtime_error("write_node: write failed at node "
                                 + std::to_string(node_idx));

    ++node_write_count;
}


void PathORAM::access_with_remap(int block_id, const uint8_t* data_in, bool is_write, uint8_t* data_out, int new_leaf) {
    if (block_id < 0 || block_id >= N)
        throw std::invalid_argument("block_id out of range");

    // Line 1: look up current leaf
    auto it = position_map.find(block_id);
    int x = (it != position_map.end()) ? it->second : random_leaf();

    // Line 2: remap to SPECIFIED leaf instead of random
    position_map[block_id] = new_leaf;

    // Lines 3-5: read path into stash
    for (int level = 0; level <= L; ++level)
        read_bucket(node_at_level(x, level));

    // Line 6: read block from stash
    if (data_out != nullptr)
        std::memset(data_out, 0, BLOCK_SIZE);
    for (Block& b : stash) {
        if (b.id == block_id) {
            if (data_out != nullptr)
                std::memcpy(data_out, b.data, BLOCK_SIZE);
            break;
        }
    }

    // Lines 7-9: update if write
    if (is_write) {
        bool found = false;
        for (Block& b : stash) {
            if (b.id == block_id) {
                std::memcpy(b.data, data_in, BLOCK_SIZE);
                found = true;
                break;
            }
        }
        if (!found)
            stash.emplace_back(block_id, data_in);
    }

    // Lines 10-15: write path back
    for (int level = L; level >= 0; --level)
        write_bucket(node_at_level(x, level), x, level);

    ++path_read_count;
    ++path_write_count;
}


void PathORAM::evict_level(int leaf, int level, int count) {
    int nodes_at_level = 1 << level;
    int start_pos      = leaf % nodes_at_level;
    int level_base     = nodes_at_level;

    std::vector<Bucket> buckets(count);

    for (int k = 0; k < count; ++k) {
        int node_leaf = (leaf + k) % num_leaves;

        Bucket& b = buckets[k];
        int slots = 0;
        for (auto it = stash.begin(); it != stash.end() && slots < Z; ) {
            auto pos_it = position_map.find(it->id);
            if (pos_it == position_map.end()) { ++it; continue; }
            if (node_at_level(pos_it->second, level) == 
                node_at_level(node_leaf, level)) {
                b.blocks[slots++] = *it;
                it = stash.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Write all buckets in one seek
    std::vector<uint8_t> buf(count * DISK_BUCKET_SIZE);
    for (int k = 0; k < count; ++k)
        buckets[k].serialize(buf.data() + k * DISK_BUCKET_SIZE);

    if (start_pos + count <= nodes_at_level) {
        // No wraparound
        long offset = (long)(level_base + start_pos - 1) * DISK_BUCKET_SIZE;
        tree_file->seekp(offset, std::ios::beg);
        ++seek_count;
        tree_file->write(reinterpret_cast<char*>(buf.data()),
                         count * DISK_BUCKET_SIZE);
    } else {
        // Wraparound — two writes
        int first_part  = nodes_at_level - start_pos;
        int second_part = count - first_part;

        long offset1 = (long)(level_base + start_pos - 1) * DISK_BUCKET_SIZE;
        tree_file->seekp(offset1, std::ios::beg);
        ++seek_count;
        tree_file->write(reinterpret_cast<char*>(buf.data()),
                         first_part * DISK_BUCKET_SIZE);

        long offset2 = (long)(level_base - 1) * DISK_BUCKET_SIZE;
        tree_file->seekp(offset2, std::ios::beg);
        ++seek_count;
        tree_file->write(reinterpret_cast<char*>(buf.data() + first_part * DISK_BUCKET_SIZE),
                         second_part * DISK_BUCKET_SIZE);
    }
}


void PathORAM::write_to_leaf(int block_id, int leaf, const uint8_t* data) {
    // Set position
    position_map[block_id] = leaf;

    // Find the leaf node and write the block there directly
    int node_idx = node_at_level(leaf, L);  // leaf level node

    // Read current bucket at that node
    Bucket b = read_node(node_idx);

    // Find an empty slot and write the block
    for (int i = 0; i < Z; ++i) {
        if (b.blocks[i].is_dummy()) {
            b.blocks[i] = Block(block_id, data);
            write_node(node_idx, b);
            return;
        }
    }

    // Leaf bucket full — put in stash
    stash.emplace_back(block_id, data);
}
