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
    tree.reserve(num_nodes);
    for (int i = 0; i < num_nodes; i++) tree.emplace_back(Z);

    // init position map
    for (int id = 0; id < N; id++) position_map[id] = random_leaf();
}

int PathORAM::random_leaf() const {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, num_leaves - 1);
    return dist(rng);
}

std::vector<int> PathORAM::get_path(int leaf) const {
    if (leaf < 0 || leaf >= num_leaves) throw std::out_of_range("leaf out of range");

    // leaf node index in heap array
    int node = (num_leaves - 1) + leaf;

    std::vector<int> path;
    while (true) {
        path.push_back(node);
        if (node == 0) break;
        node = (node - 1) / 2; // parent
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

void PathORAM::access(int block_id, const std::string& data, bool is_write) {
    (void)block_id; (void)data; (void)is_write;
    // next step: read_path -> stash -> write_path
}

void PathORAM::print_path_to_leaf(int leaf) const{
    auto path = get_path(leaf);
    std::cout << "leaf " << leaf << ": ";
    for (int i = 0; i< (int)path.size(); i++){
        std::cout << path[i] << (i + 1 == (int)path.size() ? "\n" : " -> ");
    }
}