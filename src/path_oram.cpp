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
    for (int leaf = 0; leaf < show; leaf++) {
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

// TODO: implement access. For now just a stub to test structure.
// Step 1: remap block in position map
// Step 2: read path
// Step 3: update block
// Step 4: write path

void PathORAM::access(int block_id, const char* data, bool is_write) {
    (void)block_id; (void)data; (void)is_write;
}

void PathORAM::read_path(int leaf) {
    std::vector<int> path = get_path(leaf);

    for (int node_idx : path) {
        Bucket& bucket = tree[node_idx];

        for (int i = 0; i < Z; i++) {
            Block& block = bucket.blocks[i];
            if (!block.is_dummy) {
                stash.push_back(block);

                block.id = -1;
                std::memset(block.data, 0, BLOCK_SIZE);
                block.is_dummy = true;
            }
        }
    }
}

void PathORAM:: write_path(std::vector<int> path) {

}

    // TODO: implement write_path
    // Write blocks from stash back to path, ensuring blocks are placed greedily
    // in buckets along the path to their assigned leaf
    // TODO: implement remap
    // Assign accessed block a new random leaf in position map
    // TODO: update stash;
    
void PathORAM::remap_block(int block_id){
    position_map[block_id] = random_leaf();
}

int PathORAM::stash_update(int block_id, const char* data) {
    int idx = -1;
    for (size_t i = 0; i < stash.size(); ++i){
        if (stash[i].id == block_id){
            idx = i;
            break;
        }
    }
    if (idx < 0){
        stash.push_back(Block(block_id, ""));
        idx = (int)stash.size() - 1;
    }

    std::strncpy(stash[idx].data, data, BLOCK_SIZE - 1);
    stash[idx].data[BLOCK_SIZE - 1] = '\0';
    stash[idx].is_dummy = false;

    return idx;
}



//later tho 
// TODO encrypt blocks when given to server and decrypt when read
// TODO set stash size limit
// TODO add unit tests
