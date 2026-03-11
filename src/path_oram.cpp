#include "path_oram.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>


PathORAM::PathORAM(int N_in, int Z_in, const std::string& filename, int tags_in) 
    : N(N_in), Z(Z_in), tags_count(tags_in), tree_filename(filename) {
    if (N <= 0) throw std::invalid_argument("N must be > 0");
    if (Z <= 0) throw std::invalid_argument("Z must be > 0");

    // L = tree height = ceil(log2(N)), giving 2^L leaves >= N
    L = (N > 1) ? (int)ceil(log2((double)N)) : 1;
    num_leaves = 1 << L;
    num_nodes = 2 * num_leaves - 1;

    if (tree_filename.empty()) {
        tree_filename = "path_oram_tree.bin";
    }

    // Initialize position map: assign every block a random starting leaf
    for (int id = 0; id < N; id++) position_map[id] = random_leaf();

    // Open/initialize the tree file
    tree_file = std::make_unique<std::fstream>();
    tree_file->open(tree_filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!tree_file->is_open()) {
        tree_file->clear();
        tree_file->open(tree_filename, std::ios::out | std::ios::binary);
        tree_file->close();
        tree_file->open(tree_filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    // Ensure file is large enough, fill with dummy buckets if empty/small
    size_t expected_size = get_bucket_size() * (num_nodes + 1);
    tree_file->seekg(0, std::ios::end);
    if (tree_file->tellg() < (long)expected_size) {
        tree_file->seekp(0, std::ios::beg);
        Bucket dummy(Z);
        for (int i = 0; i < Z; ++i) {
            dummy.blocks[i].tags.resize(tags_count, 0);
        }
        for (int i = 0; i <= num_nodes; ++i) {
            write_node(i, dummy);
        }
    }
}

PathORAM::~PathORAM() {
    if (tree_file && tree_file->is_open()) {
        tree_file->close();
    }
}


int PathORAM::random_leaf() const {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);
    return dist(rng);
}


std::vector<int> PathORAM::get_path(int leaf) const {
    if (leaf < 0 || leaf >= num_leaves)
        throw std::out_of_range("leaf out of range");

    // leaf node index in heap array
    int node = num_leaves + leaf;

    std::vector<int> path;
    while (node >= 1) {
        path.push_back(node);
        node = node / 2;
    }
    std::reverse(path.begin(), path.end()); // root->leaf
    return path;
}


void PathORAM::print_tree_structure() const {
    std::cout << "PathORAM N=" << N
              << " Z=" << Z
              << " L=" << L
              << " leaves=" << num_leaves
              << " nodes=" << num_nodes
              << "\n";

    // print example paths
    int show = std::min(num_leaves, 4);
    int start = 0;

    for (int k = 0; k < show; k++) {
        int leaf = start + k;
        if (leaf >= num_leaves - 1)break;
        auto p = get_path(leaf);
        std::cout << "leaf " << leaf << ": ";
        for (int i = 0; i < (int)p.size(); i++) {
            std::cout << p[i] << (i + 1 == (int)p.size() ? "\n" : " -> ");
        }
    }
}


void PathORAM::print_path_to_leaf(int leaf) const{
    auto path = get_path(leaf);
    std::cout << "leaf " << leaf << ": ";
    for (int i = 0; i< (int)path.size(); i++){
        std::cout << path[i] << (i + 1 == (int)path.size() ? "\n" : " -> ");
    }
}


std::string PathORAM::access(int block_id, const char* data, bool is_write) {
    // 1. Look up (and then remap) the block's current leaf
    int x = position_map[block_id];
    remap_block(block_id);

    // 2. Read the entire path into the stash
    read_path(x);

    // 3. Perform the requested operation on the stash
    std::string result = "";
    if (is_write) {
        stash_update(block_id, data);
    } else {
        for (const Block& b : stash) {
            if (b.id == block_id && !b.is_dummy) {
                result = std::string(b.data);
                break;
            }
        }
    }

    // 4. Write the path back — must always happen (read or write)
    write_path(get_path(x));
    return result;
}


// -------------------------------------------------------------------
// Access Helpers
// -------------------------------------------------------------------


void PathORAM::read_path(int leaf) {
    path_read_count++;
    std::vector<int> path = get_path(leaf);

    for (int node_idx : path) {
        Bucket bucket = read_node(node_idx);

        for (int i = 0; i < Z; i++) {
            Block& block = bucket.blocks[i];

            if (!block.is_dummy){ //decrypt the path 
                decrypt_block(block);
                
                // Consistency Fix: If this block already exists in the stash, it means
                // a newer version was added (likely via rORAM multi-tree update).
                // We keep the newer stash version and discard this stale tree version.
                bool already_in_stash = false;
                for (const auto& sb : stash) {
                    if (!sb.is_dummy && sb.id == block.id) {
                        already_in_stash = true;
                        break;
                    }
                }

                if (!already_in_stash) {
                    stash.push_back(block);
                }
                
                block.id = -1;
                std::memset(block.data, 0, BLOCK_SIZE);
                block.is_dummy = true;
            }
        }
        write_node(node_idx, bucket);
    }
}


bool PathORAM::bucket_on_path(int bucket_node, int leaf) const {
    // O(1): bucket b is on the path to leaf x iff the leaf's ancestor at
    // the same depth as b equals b.  Depth of b = floor(log2(b)) in a
    // 1-indexed heap.  Shifting (num_leaves + leaf) right by (L - depth)
    // gives that ancestor.
    int depth = (int)log2((double)bucket_node);
    return ((num_leaves + leaf) >> (L - depth)) == bucket_node;
}


void PathORAM::write_path(const std::vector<int>& path) {
    path_write_count++;
    for (int level = (int)path.size() - 1; level >= 0; --level) {
        int node_idx = path[level];
        Bucket bucket = read_node(node_idx);

        int filled = 0;

        for (size_t i = 0; i < stash.size() && filled < Z; ) {
            Block& block = stash[i];

            if (block.is_dummy) {
                ++i;
                continue;
            }

            int assigned_leaf = position_map[block.id];

            if (bucket_on_path(node_idx, assigned_leaf)) {
                encrypt_block(block);
                bucket.blocks[filled] = block;

                stash[i] = stash.back();
                stash.pop_back();
                filled++;
            }
            else {
                ++i;
            }
        }
        while (filled < Z) {
            Block dummy;
            dummy.tags.resize(tags_count, 0);
            encrypt_block(dummy);
            bucket.blocks[filled] = dummy;
            filled++;
        }
        write_node(node_idx, bucket);
    }
}
    

void PathORAM::remap_block(int block_id){
    position_map[block_id] = random_leaf();
}


int PathORAM::stash_update(int block_id, const char* data) {
    int idx = -1;

    for (size_t i = 0; i < stash.size(); ++i){
        if (!stash[i].is_dummy && stash[i].id == block_id){
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        // Block not yet in stash — create a new entry.
        Block nb;
        nb.id = block_id;
        nb.is_dummy = false;
        std::memset(nb.data, 0, BLOCK_SIZE);
        stash.push_back(nb);
        idx = (int)stash.size() - 1;
    }

    std::strncpy(stash[idx].data, data, BLOCK_SIZE - 1);
    stash[idx].data[BLOCK_SIZE - 1] = '\0';
    stash[idx].is_dummy = false;

    return idx;
}


void PathORAM::encrypt_block(Block &b){
    if (b.is_dummy) return;

    const uint64_t key = 0xC0FFEE1234ABCDEFULL;
    uint64_t s = key ^ (uint64_t) (uint32_t) b.id;
    
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        uint8_t k = (uint8_t)((s * 2685821657726338717ULL) & 0xFF);
        b.data[i] ^= (char)k;
    }
}

void PathORAM::decrypt_block(Block &b){
    encrypt_block(b);
}

// -------------------------------------------------------------------
// Disk Implementation
// -------------------------------------------------------------------

size_t PathORAM::get_block_size() const {
    // id (int) + data (BLOCK_SIZE) + is_dummy (bool) + tags (int * tags_count)
    return sizeof(int) + BLOCK_SIZE + sizeof(bool) + (tags_count * sizeof(int));
}

size_t PathORAM::get_bucket_size() const {
    return Z * get_block_size();
}

long PathORAM::node_offset(int node_idx) const {
    return (long)node_idx * get_bucket_size();
}

Bucket PathORAM::read_node(int node_idx) const {
    Bucket b(Z);
    if (!tree_file || !tree_file->is_open()) return b;

    tree_file->seekg(node_offset(node_idx), std::ios::beg);
    
    for (int i = 0; i < Z; ++i) {
        Block& block = b.blocks[i];
        tree_file->read(reinterpret_cast<char*>(&block.id), sizeof(int));
        tree_file->read(block.data, BLOCK_SIZE);
        tree_file->read(reinterpret_cast<char*>(&block.is_dummy), sizeof(bool));
        block.tags.resize(tags_count);
        if (tags_count > 0) {
            tree_file->read(reinterpret_cast<char*>(block.tags.data()), tags_count * sizeof(int));
        }
    }
    return b;
}

void PathORAM::write_node(int node_idx, const Bucket& b) {
    if (!tree_file || !tree_file->is_open()) return;

    tree_file->seekp(node_offset(node_idx), std::ios::beg);
    
    for (int i = 0; i < Z; ++i) {
        const Block& block = b.blocks[i];
        tree_file->write(reinterpret_cast<const char*>(&block.id), sizeof(int));
        tree_file->write(block.data, BLOCK_SIZE);
        tree_file->write(reinterpret_cast<const char*>(&block.is_dummy), sizeof(bool));
        if (tags_count > 0) {
            tree_file->write(reinterpret_cast<const char*>(block.tags.data()), tags_count * sizeof(int));
        }
    }
    tree_file->flush();
}