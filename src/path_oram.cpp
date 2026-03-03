#include "path_oram.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>


static int next_pow2(int x) {
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}


PathORAM::PathORAM(int N_in, int Z_in) : N(N_in), Z(Z_in) {
    if (N <= 0) throw std::invalid_argument("N must be > 0");
    if (Z <= 0) throw std::invalid_argument("Z must be > 0");

    // simplest: leaves >= N (power of two)
    num_leaves = next_pow2(N);
    num_nodes  = 2 * num_leaves - 1;

    // derive L (height in nodes along a path). For num_leaves=8 => L=4 (root + 3 edges)
    // We'll compute it by counting levels.
    L = 0;
    for (int x = num_leaves; x > 0; x >>= 1) L++;  // log2(num_leaves)+1

    // Build the tree: each bucket needs Z slots
    tree.clear();
    // tree.reserve(num_nodes);
    tree.resize(num_nodes + 1, Bucket(Z));
    // for (int i = 1; i < num_nodes; i++) tree.emplace_back(Z);

    // init positio map
    for (int id = 0; id < N; id++) position_map[id] = random_leaf();
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
    // int node = (num_leaves - 1) + leaf;  previous 0-based indexing switched to 1-based
    int node = num_leaves + leaf;

    std::vector<int> path;
    while (node >= 1) {
        path.push_back(node);
        // node = (node - 1) / 2;  previous 0-based indexing switched to 1-based
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
    int start = 1;

    for (int k = 0; k < show; k++) {
        int leaf = start + k;
        if (leaf >= num_leaves)break;
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
    (void)block_id; (void)data; (void)is_write;

    int x = position_map[block_id];
    
    remap_block(block_id);
    read_path(x);
    
    std::string result = "";

    if (is_write){
        stash_update(block_id, data);
    } else {
        for (const Block& b : stash){
            if (b.id == block_id && !b.is_dummy){
                return std::string(b.data);
                break;
            }
        }
    }

    write_path(get_path(x));
    return std::string(data);
}


// -------------------------------------------------------------------
// Access Helpers
// -------------------------------------------------------------------


void PathORAM::read_path(int leaf) {
    std::vector<int> path = get_path(leaf);

    for (int node_idx : path) {
        Bucket& bucket = tree[node_idx];

        for (int i = 0; i < Z; i++) {
            Block& block = bucket.blocks[i];

                if (!block.is_dummy){ //decrpyt the path 
                    decrypt_block(block);
                    stash.push_back(block);
                    
                    block.id = -1;
                    std::memset(block.data, 0, BLOCK_SIZE);
                    block.is_dummy = true;
                }
            }
        }
}


bool PathORAM:: bucket_on_path(int bucket_node, int leaf) const {
    auto path = get_path(leaf);
    return std::find(path.begin(), path.end(), bucket_node) != path.end();
}


void PathORAM:: write_path(std::vector<int> path) {
    for (int level = (int)path.size() - 1; level >= 0; --level) {
        int node_idx = path[level];
        Bucket& bucket = tree[node_idx];

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
            encrypt_block(dummy);
            bucket.blocks[filled] = dummy;
            filled++;
        }
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

    if (idx < 0){
        if (position_map.find(block_id) == position_map.end()){
            position_map[block_id] = random_leaf();
        }
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
    
    for (int i = 1; i < BLOCK_SIZE; ++i){
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