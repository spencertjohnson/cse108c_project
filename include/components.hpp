#pragma once
#include <cstring>
#include <vector>

#define Z 4

struct Block {
    int id = -1;
    std::vector<uint8_t> data;

    Block() = default;

    explicit Block(int block_size) : id(-1), data(block_size, 0) {}

    Block(int id_, const uint8_t* data_, int block_size) : id(id_), data(data_, data_ + block_size) {}

    bool is_dummy() const { return id == -1; }

    void serialize(uint8_t* buf, int block_size) const {
        std::memcpy(buf, &id, 4);
        if (!data.empty())
            std::memcpy(buf + 4, data.data(), block_size);
        else
            std::memset(buf + 4, 0, block_size);
    }

    void deserialize(const uint8_t* buf, int block_size) {
        std::memcpy(&id, buf, 4);
        data.assign(buf + 4, buf + 4 + block_size);
    }
};

struct Bucket {
    std::vector<Block> blocks;

    Bucket() = default;

    explicit Bucket(int block_size) {
        blocks.reserve(Z);
        for (int i = 0; i < Z; ++i)
            blocks.emplace_back(block_size);
    }

    void serialize(uint8_t* buf, int block_size) const {
        int dbs = disk_block_size(block_size);
        for (int i = 0; i < Z; ++i)
            blocks[i].serialize(buf + i * dbs, block_size);
    }

    void deserialize(const uint8_t* buf, int block_size) {
        int dbs = disk_block_size(block_size);
        blocks.resize(Z);
        for (int i = 0; i < Z; ++i)
            blocks[i].deserialize(buf + i * dbs, block_size);
    }

    static int disk_block_size(int block_size) { return 4 + block_size; }
    static int disk_bucket_size(int block_size) { return Z * disk_block_size(block_size); }
};
