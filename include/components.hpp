#pragma once
#include <cstring>
#include <vector>

#define BLOCK_SIZE 4096

struct Block {
    int id;
    char data[BLOCK_SIZE];
    bool is_dummy;

    Block()
        : id(-1), is_dummy(true) {
            std::memset(data, 0, BLOCK_SIZE);
        }

    Block(int id_, const char* data_) : id(id_), is_dummy(false) {
        std::strncpy(data, data_, BLOCK_SIZE - 1);
        data[BLOCK_SIZE - 1] = '\0';
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
