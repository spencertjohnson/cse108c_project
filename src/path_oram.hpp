//path_oram.hpp
#pragma once
#include <vector>
#include <cstddef>


class PathORAM {
public:
    PathORAM(std::size_t N, std::size_t Z);
    void print_tree_structure() const;

    void print_path_to_leaf(std::size_t leaf) const;

private:
    std::size_t N_;
    std::size_t Z_;
    std::size_t L_;
    std::size_t num_nodes_;

    std::size_t leaf_to_node(std::size_t leaf) const;

    std::vector<std::size_t> build_path(std::size_t leaf) const;
};