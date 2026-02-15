// path_oram.cpp
#include "path_oram.hpp"
#include <stdexcept>
#include <iostream>
#include <algorithm>


void PathORAM::print_tree_structure() const{
    std::cout << "PathORAM tree: " << "N=" << N_ << ", Z=" << Z_ << ", L=" << L_ << ", nodes=" << num_nodes_ << "\n";

    for (std::size_t leaf = 0; leaf < std::min<std::size_t>(L_, 4); leaf++){
        auto path = build_path(leaf);
        std::cout << "leaf" << leaf << ":";
        for (std::size_t i = 0; i < path.size(); i++){
            std::cout << path[i] << (i + 1 == path.size() ? "\n" : " -> ");
        }
    }
}

std::size_t PathORAM::leaf_to_node(std::size_t leaf) const {
    if (leaf >= L_) throw std::out_of_range("leaf out of range");
    return (L_ - 1) + leaf;
}

std::vector<std::size_t> PathORAM::build_path(std::size_t leaf) const {
    std::size_t node = leaf_to_node(leaf);

    std::vector<std::size_t> path;
    while (true){
        path.push_back(node);
        if (node == 0) break;
        node = (node - 1) / 2;
    }

    std::reverse(path.begin(), path.end());
    return path;
}

void PathORAM::print_path_to_leaf(std::size_t leaf) const {
    auto path = build_path(leaf);
    std::cout << "Path to leaf" << leaf << ": ";
    for (std::size_t i = 0; i < path.size(); i++){
        std::cout << path[i] << (i + 1 == path.size() ? "\n" : "->");
    }
}

static std::size_t next_pow2(std::size_t x){
    std::size_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

PathORAM::PathORAM(std::size_t N, std::size_t Z)
    : N_(N), Z_(Z)
{
    L_ = next_pow2(N_);
    num_nodes_ = 2 * L_ - 1;
}