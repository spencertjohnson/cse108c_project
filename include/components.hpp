#pragma once
#include <cstring>
#include <vector>

#define BLOCK_SIZE 4096
#define Z 4

static constexpr int MAX_TAGS = 16;

// [ id: 4 bytes | data: BLOCK_SIZE bytes | num_tags: 4 bytes | tags: MAX_TAGS*4 bytes ]
static constexpr int DISK_BLOCK_SIZE  = 4 + BLOCK_SIZE + 4 + MAX_TAGS * 4;
static constexpr int DISK_BUCKET_SIZE = Z * DISK_BLOCK_SIZE;

struct Block {
    int id;
    uint8_t data[BLOCK_SIZE];
    int tags[MAX_TAGS];

    // Dummy block
    Block()
        : id(-1) {
            std::memset(data, 0, BLOCK_SIZE);
            std::memset(tags, 0, sizeof(tags));
        }

    // Real PATH ORAM block
    Block(int id_, const uint8_t * data_) : id(id_) {
        std::memcpy(data, data_, BLOCK_SIZE);
        std::memset(tags, 0, sizeof(tags));
    }

    //Read rORAM block
    Block(int id_, const uint8_t * data_, const int * tags_,) : id(id_) {
        std::memcpy(data, data_, BLOCK_SIZE);
        std::memset(tags, 0, sizeof(tags));
        std::memcpy(tags, tags_, MAX_TAGS * 4);
    }

    bool is_dummy() const { return id == -1; }

    void serialize(uint8_t* buf) const {
        std::memcpy(buf,                         &id,       4);
        std::memcpy(buf + 4,                      data,     BLOCK_SIZE);
        std::memcpy(buf + 4 + BLOCK_SIZE + 4,    tags,      MAX_TAGS * 4);

    }

    void deserialize(const uint8_t* buf) {
        std::memcpy(&id,   buf,                   4);
        std::memcpy( data, buf + 4,               BLOCK_SIZE);
        std::memcpy( tags, buf + 4 + BLOCK_SIZE,  MAX_TAGS * 4);
    }
};

class Bucket {
public:
    Block blocks[Z];

    Bucket() = default;

    void serialize(uint8_t* buf) const {
        for (int i = 0; i < Z; ++i)
            blocks[i].serialize(buf + i * DISK_BLOCK_SIZE);
    }

    void deserialize(const uint8_t* buf) {
        for (int i = 0; i < Z; ++i)
            blocks[i].deserialize(buf + i * DISK_BLOCK_SIZE);
    }
};
