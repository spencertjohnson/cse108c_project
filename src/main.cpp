#include "path_oram.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <unordered_map>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;

// Colour codes (gracefully degrade if terminal doesn't support them)
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

#define ASSERT_EQ(name, got, expected) \
    do { \
        if ((got) == (expected)) { pass(name); } \
        else { fail(name, "expected \"" + std::string(expected) + \
                          "\" got \"" + std::string(got) + "\""); } \
    } while(0)

#define ASSERT_TRUE(name, cond, detail) \
    do { \
        if (cond) { pass(name); } \
        else { fail(name, detail); } \
    } while(0)

// -----------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------

// 1. More blocks — write/read many distinct blocks, not just 2
static void test_many_blocks(PathORAM& oram, int N) {
    std::cout << "\n-- test_many_blocks (" << N << " blocks) --\n";

    // Write phase
    for (int i = 0; i < N; ++i) {
        std::string val = "val_" + std::to_string(i);
        oram.access(i, val.c_str(), true);
    }

    // Read phase — every block must return its value
    for (int i = 0; i < N; ++i) {
        std::string got = oram.access(i, "", false);
        ASSERT_EQ("many_blocks block " + std::to_string(i),
                  got, "val_" + std::to_string(i));
    }
}

// 2. Overwrite — write to same block twice, read should return latest value
static void test_overwrite(PathORAM& oram) {
    std::cout << "\n-- test_overwrite --\n";
    oram.access(0, "first",  true);
    oram.access(0, "second", true);
    std::string got = oram.access(0, "", false);
    ASSERT_EQ("overwrite returns latest value", got, "second");
}

// 3. Sequential access — access same block many times, value must persist
static void test_sequential_access(PathORAM& oram) {
    std::cout << "\n-- test_sequential_access --\n";
    oram.access(5, "persistent", true);
    for (int i = 0; i < 20; ++i) {
        std::string got = oram.access(5, "", false);
        ASSERT_EQ("sequential read #" + std::to_string(i), got, "persistent");
    }
}

// 4. All N blocks — write all, read all back, interleaved
static void test_all_blocks(PathORAM& oram, int N) {
    std::cout << "\n-- test_all_blocks (N=" << N << ") --\n";

    // Build expected values
    std::vector<std::string> expected(N);
    for (int i = 0; i < N; ++i) {
        expected[i] = "block" + std::to_string(i);
        oram.access(i, expected[i].c_str(), true);
    }

    // Read back in reverse order to vary access patterns
    for (int i = N - 1; i >= 0; --i) {
        std::string got = oram.access(i, "", false);
        ASSERT_EQ("all_blocks read[" + std::to_string(i) + "]", got, expected[i]);
    }
}

// 5. Stash size check — stash should stay bounded across many accesses
//    Path ORAM guarantees stash overflow is negligibly rare; O(log N) bound.
static void test_stash_bounded(PathORAM& oram, int N) {
    std::cout << "\n-- test_stash_bounded --\n";

    // Write all blocks first
    for (int i = 0; i < N; ++i)
        oram.access(i, ("data" + std::to_string(i)).c_str(), true);

    int max_stash = 0;

    // Do 5*N random-ish accesses and track peak stash size
    for (int round = 0; round < 5 * N; ++round) {
        int id = round % N;
        oram.access(id, "", false);
        int s = oram.stash_size();
        if (s > max_stash) max_stash = s;
    }

    // Conservative bound: stash should never exceed N (often stays near 0)
    ASSERT_TRUE("stash stays bounded (max=" + std::to_string(max_stash) + ")",
                max_stash < N,
                "stash grew to " + std::to_string(max_stash) + " >= N=" + std::to_string(N));
}

// 6. Position map remapping — after each access the block gets a new leaf
static void test_position_remap(PathORAM& oram) {
    std::cout << "\n-- test_position_remap --\n";

    oram.access(0, "remap_test", true);

    int changes = 0;
    int trials  = 30;
    int prev_leaf = oram.get_leaf(0);

    for (int i = 0; i < trials; ++i) {
        oram.access(0, "", false);
        int new_leaf = oram.get_leaf(0);
        if (new_leaf != prev_leaf) ++changes;
        prev_leaf = new_leaf;
        // Leaf must always be in valid range
        ASSERT_TRUE("remap leaf in range [trial " + std::to_string(i) + "]",
                    new_leaf >= 0 && new_leaf < oram.num_leaves_count(),
                    "leaf=" + std::to_string(new_leaf) + " out of range");
    }

    // With N>=16 leaves, over 30 trials we almost certainly see at least one change
    ASSERT_TRUE("position map remaps across accesses",
                changes > 0,
                "leaf never changed across " + std::to_string(trials) + " accesses");
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int DEFAULT_N = 16;
    const int DEFAULT_Z = 4;

    int N = DEFAULT_N;
    int Z = DEFAULT_Z;

    std::string line;

    std::cout << "Enter number of nodes N [default " << DEFAULT_N << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        try { N = std::stoi(line); }
        catch (...) {
            std::cerr << "Invalid input for N, using default (" << DEFAULT_N << ").\n";
            N = DEFAULT_N;
        }
    }

    std::cout << "Enter nodes per bucket Z [default " << DEFAULT_Z << "]: ";
    std::getline(std::cin, line);
    if (!line.empty()) {
        try { Z = std::stoi(line); }
        catch (...) {
            std::cerr << "Invalid input for Z, using default (" << DEFAULT_Z << ").\n";
            Z = DEFAULT_Z;
        }
    }

    std::cout << "Creating PathORAM with N=" << N << ", Z=" << Z << "\n";

    // Each test gets a fresh ORAM instance so state doesn't bleed between tests
    { PathORAM oram(N, Z); test_many_blocks(oram, N); }
    { PathORAM oram(N, Z); test_overwrite(oram); }
    { PathORAM oram(N, Z); test_sequential_access(oram); }
    { PathORAM oram(N, Z); test_all_blocks(oram, N); }
    { PathORAM oram(N, Z); test_stash_bounded(oram, N); }
    { PathORAM oram(N, Z); test_position_remap(oram); }

    // Summary
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