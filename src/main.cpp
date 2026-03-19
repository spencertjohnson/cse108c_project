#include "path_oram.hpp"
#include "ro_range_oram.hpp"
#include <iostream>
#include <vector>
#include <cstring>
#include <string>

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
        if (cond) { pass(name); } \
        else { fail(name, detail); } \
    } while(0)

// Fill block with recognizable pattern — byte fill of (id & 0xFF)
// with block id stored in first 4 bytes
static void fill_block(uint8_t* buf, int block_id) {
    std::memset(buf, block_id & 0xFF, BLOCK_SIZE);
    std::memcpy(buf, &block_id, 4);
}

static bool check_block(const uint8_t* buf, int block_id) {
    uint8_t expected[BLOCK_SIZE];
    fill_block(expected, block_id);
    return std::memcmp(buf, expected, BLOCK_SIZE) == 0;
}

static std::vector<uint8_t> make_init_data(int N) {
    std::vector<uint8_t> data((long)N * BLOCK_SIZE);
    for (int i = 0; i < N; ++i)
        fill_block(data.data() + (long)i * BLOCK_SIZE, i);
    return data;
}

// -----------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------

// 1. Read single block (range=1) at several positions
static void test_single_block(int N, int ell) {
    std::cout << "\n-- test_single_block (N=" << N << ") --\n";
    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_single");

    std::vector<uint8_t> out(BLOCK_SIZE);
    for (int i = 0; i < N; i += std::max(1, N/8)) {
        oram.read(i, 1, out.data());
        ASSERT_TRUE("single block " + std::to_string(i),
                    check_block(out.data(), i),
                    "block " + std::to_string(i) + " data mismatch");
    }
}

// 2. Read aligned super-blocks of max size
static void test_aligned_range(int N, int ell) {
    std::cout << "\n-- test_aligned_range (N=" << N << ", r=2^ell=" << (1<<ell) << ") --\n";
    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_aligned");

    int r = 1 << ell;
    std::vector<uint8_t> out((long)r * BLOCK_SIZE);

    for (int start = 0; start + r <= N; start += r * 2) {
        oram.read(start, r, out.data());
        bool ok = true;
        for (int k = 0; k < r; ++k) {
            if (!check_block(out.data() + (long)k * BLOCK_SIZE, start + k)) {
                ok = false;
                fail("aligned block " + std::to_string(start + k), "mismatch");
                break;
            }
        }
        if (ok)
            pass("aligned range start=" + std::to_string(start));
    }
}

// 3. Read unaligned range
static void test_unaligned_range(int N, int ell) {
    std::cout << "\n-- test_unaligned_range --\n";
    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_unaligned");

    // start=3 is unaligned, r=3 is not a power of 2
    int start = 3;
    int r     = 3;
    if (start + r > N) { std::cout << "   Skipping — N too small\n"; return; }

    std::vector<uint8_t> out((long)r * BLOCK_SIZE);
    oram.read(start, r, out.data());

    for (int k = 0; k < r; ++k) {
        ASSERT_TRUE("unaligned block " + std::to_string(start + k),
                    check_block(out.data() + (long)k * BLOCK_SIZE, start + k),
                    "mismatch");
    }
}

// 4. Read every single block individually
static void test_all_single(int N, int ell) {
    std::cout << "\n-- test_all_single (N=" << N << ") --\n";
    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_all_single");

    std::vector<uint8_t> out(BLOCK_SIZE);
    int failures = 0;
    for (int i = 0; i < N; ++i) {
        oram.read(i, 1, out.data());
        if (!check_block(out.data(), i)) ++failures;
    }
    ASSERT_TRUE("all " + std::to_string(N) + " blocks correct",
                failures == 0,
                std::to_string(failures) + " blocks had wrong data");
}

// 5. Repeated reads return consistent data
static void test_repeated_reads(int N, int ell) {
    std::cout << "\n-- test_repeated_reads --\n";
    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_repeated");

    std::vector<uint8_t> out(BLOCK_SIZE);
    for (int trial = 0; trial < 10; ++trial) {
        oram.read(0, 1, out.data());
        ASSERT_TRUE("repeated read trial " + std::to_string(trial),
                    check_block(out.data(), 0),
                    "block 0 wrong on trial " + std::to_string(trial));
    }
}

// 6. Range of size 2
static void test_range_size_2(int N, int ell) {
    std::cout << "\n-- test_range_size_2 --\n";
    if (ell < 1) { std::cout << "   Skipping — ell < 1\n"; return; }

    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_range2");

    std::vector<uint8_t> out(2 * BLOCK_SIZE);
    for (int start = 0; start + 2 <= N; start += 16) {
        oram.read(start, 2, out.data());
        ASSERT_TRUE("range2 start=" + std::to_string(start),
                    check_block(out.data(), start) &&
                    check_block(out.data() + BLOCK_SIZE, start + 1),
                    "blocks " + std::to_string(start) + "," +
                    std::to_string(start+1) + " mismatch");
    }
}

// 7. Range crosses super-block boundary
static void test_cross_boundary(int N, int ell) {
    std::cout << "\n-- test_cross_boundary --\n";
    if (ell < 1) { std::cout << "   Skipping — ell < 1\n"; return; }

    auto data = make_init_data(N);
    ReadOnlyRangeORAM oram(N, ell, data.data(), "data/test_cross");

    int r     = 1 << ell;
    int start = r - 1;  // starts one before boundary, crosses into next super-block
    if (start + r > N) { std::cout << "   Skipping — N too small\n"; return; }

    std::vector<uint8_t> out((long)r * BLOCK_SIZE);
    oram.read(start, r, out.data());

    bool ok = true;
    for (int k = 0; k < r; ++k) {
        if (!check_block(out.data() + (long)k * BLOCK_SIZE, start + k)) {
            ok = false;
            fail("cross-boundary block " + std::to_string(start + k), "mismatch");
        }
    }
    if (ok)
        pass("cross-boundary range start=" + std::to_string(start));
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int N   = 64;
    const int ell = 3;

    std::cout << "ReadOnlyRangeORAM correctness tests\n";
    std::cout << "N=" << N << " ell=" << ell
              << " max_range=" << (1 << ell) << "\n";

    test_single_block(N, ell);
    test_aligned_range(N, ell);
    test_unaligned_range(N, ell);
    test_all_single(N, ell);
    test_repeated_reads(N, ell);
    test_range_size_2(N, ell);
    test_cross_boundary(N, ell);

    std::cout << "\n============================\n";
    if (tests_passed == tests_run) {
        std::cout << GREEN << "ALL TESTS PASSED" << RESET
                  << " (" << tests_passed << "/" << tests_run << ")\n";
    } else {
        std::cout << RED << "SOME TESTS FAILED" << RESET
                  << " (" << tests_passed << "/" << tests_run << " passed)\n";
    }
    std::cout << "============================\n";

    return (tests_passed == tests_run) ? 0 : 1;
}
