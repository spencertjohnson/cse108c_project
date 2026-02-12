#include <string>
#include <vector>


struct Block {
    int id;
    std::string data;
    bool is_dummy;
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
