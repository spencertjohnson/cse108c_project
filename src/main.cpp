#include "path_oram.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <cstring>

// -----------------------------------------------------------------------
// Test block size — can be anything, using original default
// -----------------------------------------------------------------------
static constexpr int TEST_BS = 4096;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;

#define GREEN "\033[32m"
#define RED   "\033[31m"
#define RESET "\033[0m"

static void pass(const std::string& name) {
    ++tests_run; ++tests_passed;
    std::cout << GREEN << "[PASS]" << RESET << " " << name << "\n";
}

static void fail(const std::string& name, const std::string& detail) {
    ++tests_run;
    std::cout << RED << "[FAIL]" << RESET << " " << name << " — " << detail << "\n";
}

#define ASSERT_TRUE(name, cond, detail) \
    do { \
        if (cond) pass(name); \
        else      fail(name, detail); \
    } while(0)

static void str_to_buf(const std::string& s, uint8_t* buf) {
    std::memset(buf, 0, TEST_BS);
    std::memcpy(buf, s.c_str(), std::min(s.size(), (size_t)TEST_BS - 1));
}

static std::string buf_to_str(const uint8_t* buf) {
    return std::string(reinterpret_cast<const char*>(buf));
}

// -----------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------

static void test_many_blocks(PathORAM& oram, int N) {
    std::cout << "\n-- test_many_blocks (" << N << " blocks) --\n";

    std::vector<uint8_t> buf(TEST_BS);

    for (int i = 0; i < N; ++i) {
        str_to_buf("val_" + std::to_string(i), buf.data());
        oram.access(i, buf.data(), true, nullptr);
    }

    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> out(TEST_BS);
        oram.access(i, nullptr, false, out.data());
        std::string got      = buf_to_str(out.data());
        std::string expected = "val_" + std::to_string(i);
        ASSERT_TRUE("many_blocks block " + std::to_string(i),
                    got == expected,
                    "expected \"" + expected + "\" got \"" + got + "\"");
    }
}

static void test_overwrite(PathORAM& oram) {
    std::cout << "\n-- test_overwrite --\n";

    std::vector<uint8_t> buf(TEST_BS), out(TEST_BS);

    str_to_buf("first", buf.data());
    oram.access(0, buf.data(), true, nullptr);

    str_to_buf("second", buf.data());
    oram.access(0, buf.data(), true, nullptr);

    oram.access(0, nullptr, false, out.data());
    std::string got = buf_to_str(out.data());
    ASSERT_TRUE("overwrite returns latest value",
                got == "second",
                "expected \"second\" got \"" + got + "\"");
}

static void test_sequential_access(PathORAM& oram) {
    std::cout << "\n-- test_sequential_access --\n";

    std::vector<uint8_t> buf(TEST_BS), out(TEST_BS);

    str_to_buf("persistent", buf.data());
    oram.access(5, buf.data(), true, nullptr);

    for (int i = 0; i < 20; ++i) {
        oram.access(5, nullptr, false, out.data());
        std::string got = buf_to_str(out.data());
        ASSERT_TRUE("sequential read #" + std::to_string(i),
                    got == "persistent",
                    "expected \"persistent\" got \"" + got + "\"");
    }
}

static void test_all_blocks(PathORAM& oram, int N) {
    std::cout << "\n-- test_all_blocks (N=" << N << ") --\n";

    std::vector<uint8_t> buf(TEST_BS), out(TEST_BS);
    std::vector<std::string> expected(N);

    for (int i = 0; i < N; ++i) {
        expected[i] = "block" + std::to_string(i);
        str_to_buf(expected[i], buf.data());
        oram.access(i, buf.data(), true, nullptr);
    }

    for (int i = N - 1; i >= 0; --i) {
        oram.access(i, nullptr, false, out.data());
        std::string got = buf_to_str(out.data());
        ASSERT_TRUE("all_blocks read[" + std::to_string(i) + "]",
                    got == expected[i],
                    "expected \"" + expected[i] + "\" got \"" + got + "\"");
    }
}

static void test_stash_bounded(PathORAM& oram, int N) {
    std::cout << "\n-- test_stash_bounded --\n";

    std::vector<uint8_t> buf(TEST_BS);

    for (int i = 0; i < N; ++i) {
        str_to_buf("data" + std::to_string(i), buf.data());
        oram.access(i, buf.data(), true, nullptr);
    }

    int max_stash = 0;
    for (int round = 0; round < 5 * N; ++round) {
        oram.access(round % N, nullptr, false, nullptr);
        int s = oram.stash_size();
        if (s > max_stash) max_stash = s;
    }

    ASSERT_TRUE("stash stays bounded (max=" + std::to_string(max_stash) + ")",
                max_stash < N,
                "stash grew to " + std::to_string(max_stash) + " >= N=" + std::to_string(N));
}

static void test_position_remap(PathORAM& oram) {
    std::cout << "\n-- test_position_remap --\n";

    std::vector<uint8_t> buf(TEST_BS);
    str_to_buf("remap_test", buf.data());
    oram.access(0, buf.data(), true, nullptr);

    int changes   = 0;
    int trials    = 30;
    int prev_leaf = oram.get_leaf(0);

    for (int i = 0; i < trials; ++i) {
        oram.access(0, nullptr, false, nullptr);
        int new_leaf = oram.get_leaf(0);
        if (new_leaf != prev_leaf) ++changes;
        prev_leaf = new_leaf;

        ASSERT_TRUE("remap leaf in range [trial " + std::to_string(i) + "]",
                    new_leaf >= 0 && new_leaf < oram.get_num_leaves(),
                    "leaf=" + std::to_string(new_leaf) + " out of range");
    }

    ASSERT_TRUE("position map remaps across accesses",
                changes > 0,
                "leaf never changed across " + std::to_string(trials) + " accesses");
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int DEFAULT_N = 16;
    int N = DEFAULT_N;

    std::string line;
    std::cout << "Enter number of blocks N [default " << DEFAULT_N << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        try { N = std::stoi(line); }
        catch (...) {
            std::cerr << "Invalid input, using default (" << DEFAULT_N << ").\n";
            N = DEFAULT_N;
        }
    }

    std::cout << "Creating PathORAM tests with N=" << N
              << " block_size=" << TEST_BS << "\n";

    { PathORAM oram(N, TEST_BS, "data/test_many.bin");      test_many_blocks(oram, N); }
    { PathORAM oram(N, TEST_BS, "data/test_overwrite.bin"); test_overwrite(oram); }
    { PathORAM oram(N, TEST_BS, "data/test_seq.bin");       test_sequential_access(oram); }
    { PathORAM oram(N, TEST_BS, "data/test_all.bin");       test_all_blocks(oram, N); }
    { PathORAM oram(N, TEST_BS, "data/test_stash.bin");     test_stash_bounded(oram, N); }
    { PathORAM oram(N, TEST_BS, "data/test_pos.bin");       test_position_remap(oram); }

    std::cout << "\n============================\n";
    if (tests_passed == tests_run)
        std::cout << GREEN << "ALL TESTS PASSED" << RESET
                  << " (" << tests_passed << "/" << tests_run << ")\n";
    else
        std::cout << RED << "SOME TESTS FAILED" << RESET
                  << " (" << tests_passed << "/" << tests_run << " passed)\n";
    std::cout << "============================\n";

    return (tests_passed == tests_run) ? 0 : 1;
}
