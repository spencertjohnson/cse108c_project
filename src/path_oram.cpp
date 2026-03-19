#include "path_oram.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <filesystem>

PathORAM::PathORAM(int N_in, int block_size_in, const std::string& filename)
    : N(N_in), block_size(block_size_in)
{
    if (N <= 0) throw std::invalid_argument("N must be > 0");
    if (block_size <= 0) throw std::invalid_argument("block_size must be > 0");

    L = (N > 1) ? (int)std::ceil(std::log2((double)N)) : 1;
    num_leaves = 1 << L;
    num_nodes = 2 * num_leaves - 1;

    rng = std::mt19937{std::random_device{}()};

    tree_filename = filename.empty() ? "data/path_oram_tree.bin" : filename;

    std::filesystem::path p(tree_filename);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());

    for (int id = 0; id < N; ++id)
        position_map[id] = random_leaf();

    tree_file = std::make_unique<std::fstream>();
    tree_file->open(tree_filename,
        std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (!tree_file->is_open())
        throw std::runtime_error("Failed to open tree file: " + tree_filename);

    Bucket dummy(block_size);
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

int PathORAM::node_at_level(int leaf, int level) const {
    return (num_leaves + leaf) >> (L - level);
}

void PathORAM::access(int block_id, const uint8_t* data_in, bool is_write, uint8_t* data_out) {
    if (block_id < 0 || block_id >= N)
        throw std::invalid_argument("block_id out of range");

    int x = has_position(block_id) ? position_map[block_id] : random_leaf();
    position_map[block_id] = random_leaf();

    for (int level = 0; level <= L; ++level)
        read_bucket(node_at_level(x, level));

    if (data_out)
        std::memset(data_out, 0, block_size);

    for (Block& b : stash) {
        if (b.id == block_id) {
            if (data_out)
                std::memcpy(data_out, b.data.data(), block_size);
            break;
        }
    }

    if (is_write) {
        bool found = false;
        for (Block& b : stash) {
            if (b.id == block_id) {
                std::memcpy(b.data.data(), data_in, block_size);
                found = true;
                break;
            }
        }
        if (!found)
            stash.emplace_back(block_id, data_in, block_size);
    }

    for (int level = L; level >= 0; --level)
        write_bucket(node_at_level(x, level), x, level);
}

void PathORAM::read_bucket(int node_idx) {
    Bucket b = read_node(node_idx);
    for (int i = 0; i < Z; ++i)
        if (!b.blocks[i].is_dummy())
            stash.push_back(b.blocks[i]);
}

void PathORAM::write_bucket(int node_idx, int leaf_x, int level) {
    Bucket b(block_size);
    int slots = 0;
    for (auto it = stash.begin(); it != stash.end() && slots < Z; ) {
        if (!has_position(it->id)) { ++it; continue; }
        int block_leaf = position_map[it->id];
        if (node_at_level(leaf_x, level) == node_at_level(block_leaf, level)) {
            b.blocks[slots++] = *it;
            it = stash.erase(it);
        } else {
            ++it;
        }
    }
    write_node(node_idx, b);
}

Bucket PathORAM::read_node(int node_idx) const {
    int  dbs = Bucket::disk_bucket_size(block_size);
    long offset = (long)(node_idx - 1) * dbs;

    tree_file->seekg(offset, std::ios::beg);
    ++seek_count;
    if (!tree_file->good())
        throw std::runtime_error("read_node: seek failed at node " + std::to_string(node_idx));

    std::vector<uint8_t> buf(dbs);
    tree_file->read(reinterpret_cast<char*>(buf.data()), dbs);
    if (!tree_file->good())
        throw std::runtime_error("read_node: read failed at node " + std::to_string(node_idx));

    bytes_read += dbs;

    Bucket b(block_size);
    b.deserialize(buf.data(), block_size);
    return b;
}

void PathORAM::write_node(int node_idx, const Bucket& b) {
    int  dbs = Bucket::disk_bucket_size(block_size);
    long offset = (long)(node_idx - 1) * dbs;

    tree_file->seekp(offset, std::ios::beg);
    ++seek_count;
    if (!tree_file->good())
        throw std::runtime_error("write_node: seek failed at node " + std::to_string(node_idx));

    std::vector<uint8_t> buf(dbs);
    b.serialize(buf.data(), block_size);
    tree_file->write(reinterpret_cast<char*>(buf.data()), dbs);
    if (!tree_file->good())
        throw std::runtime_error("write_node: write failed at node " + std::to_string(node_idx));

    bytes_written += dbs;
}

void PathORAM::print_tree_structure() const {
    std::cout << "PathORAM N=" << N << " L=" << L
              << " leaves=" << num_leaves << " nodes=" << num_nodes
              << " block_size=" << block_size << "\n";
    int show = std::min(num_leaves, 4);
    for (int i = 0; i < show; ++i)
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
