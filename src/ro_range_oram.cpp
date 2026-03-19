#include "ro_range_oram.hpp"
#include <cmath>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>


ReadOnlyRangeORAM::ReadOnlyRangeORAM(int N_in, int ell_in, const uint8_t* data, const std::string& prefix)
    : N(N_in), ell(ell_in)
{
    if (N <= 0)   throw std::invalid_argument("N must be > 0");
    if (ell < 0)  throw std::invalid_argument("ell must be >= 0");

    rng = std::mt19937{std::random_device{}()};

    sub_orams.reserve(ell + 1);
    rpm.resize(ell + 1);

    // Create ell+1 sub-ORAMs and initialize each one
    for (int i = 0; i <= ell; ++i) {
        std::string fname = prefix + "_sub_" + std::to_string(i) + ".bin";
        sub_orams.emplace_back(N, fname);
        init_sub_oram(i, data);
    }
}