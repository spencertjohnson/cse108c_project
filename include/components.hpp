#pragma once
#include <string>
#include <vector>


struct Block {
    int id;
    std::string data;
    bool is_dummy;

    Block()
        : id(-1), data(""), is_dummy(true) {}

    Block(int id, const std::string& data)
        : id(id), data(data), is_dummy(false) {}
};

class Bucket {
public:
    std::vector<Block> blocks;

    Bucket(int Z) {
        blocks.resize(Z);
        for (int i = 0; i < Z; ++i) {
            blocks[i].id = -1; // -1 indicates a dummy block
            blocks[i].data = "";
            blocks[i].is_dummy = true;
        }
    }
};
