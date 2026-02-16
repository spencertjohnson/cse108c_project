#pragma once
#include <cstring>
#include <vector>

#define BLOCK_SIZE 4096

struct Block {
    int id;
    char data[BLOCK_SIZE];
    bool is_dummy;

    Block()
        : id(-1), data(""), is_dummy(true) {}

    Block(int id_, const char* data_) : id(id_), is_dummy(false) {
        std::copy(data_, data_ + BLOCK_SIZE, data);
    }
};

class Bucket {
public:
    std::vector<Block> blocks;

    Bucket() = default;

    Bucket(int Z) {
        blocks.resize(Z);
        for (int i = 0; i < Z; ++i) {
            blocks[i].id = -1; // -1 indicates a dummy block
            std::memset(blocks[i].data, 0, BLOCK_SIZE);
            blocks[i].is_dummy = true;
        }
    }
};
