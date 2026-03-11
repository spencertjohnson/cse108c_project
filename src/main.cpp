#include "path_oram.hpp"
#include "r_oram.hpp"
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
// rORAM Tests
// -----------------------------------------------------------------------

static void test_roram_single(rORAM& oram, int N) {
    std::cout << "\n-- test_roram_single --\n";
    for (int i = 0; i < N; ++i) {
        oram.access(i, 1, true, {"roram_" + std::to_string(i)});
    }
    for (int i = 0; i < N; ++i) {
        std::string got = oram.access(i, 1)[0];
        ASSERT_EQ("roram single read block " + std::to_string(i), got, "roram_" + std::to_string(i));
    }
}

static void test_roram_overwrite(rORAM& oram) {
    std::cout << "\n-- test_roram_overwrite --\n";
    oram.access(2, 1, true, {"alpha"});
    oram.access(2, 1, true, {"beta"});
    std::string got = oram.access(2, 1)[0];
    ASSERT_EQ("roram overwrite via access returns latest", got, "beta");
}

static void test_roram_range(rORAM& oram) {
    std::cout << "\n-- test_roram_range --\n";
    int start = 4;
    int r = 4; // should hit sub-ORAM i=2 (size 2^2=4)
    std::vector<std::string> data = {"val4", "val5", "val6", "val7"};
    
    // Write range
    oram.access(start, r, true, data);
    
    // Read range back
    std::vector<std::string> got = oram.access(start, r);
    ASSERT_TRUE("range returning correct size", got.size() == (size_t)r, "got size " + std::to_string(got.size()));
    if (got.size() == (size_t)r) {
        for (int i = 0; i < r; ++i) {
            ASSERT_EQ("roram range read block " + std::to_string(start + i), got[i], data[i]);
        }
    }
    
    // Unaligned range test
    int unalign_start = 5;
    int unalign_r = 3; // i=2, actual_range=4, a0=4
    std::vector<std::string> unalign_data = {"x", "y", "z"};
    oram.access(unalign_start, unalign_r, true, unalign_data);
    
    std::vector<std::string> unalign_got = oram.access(unalign_start, unalign_r);
    ASSERT_TRUE("unaligned range returning correct size", unalign_got.size() == (size_t)unalign_r, "got size " + std::to_string(unalign_got.size()));
    if (unalign_got.size() == (size_t)unalign_r) {
        for (int i = 0; i < unalign_r; ++i) {
            ASSERT_EQ("roram unaligned range read block " + std::to_string(unalign_start + i), unalign_got[i], unalign_data[i]);
        }
    }
}

static void test_roram_cross_consistency(rORAM& oram) {
    std::cout << "\n-- test_roram_cross_consistency --\n";
    // Write 4 blocks via access (hits R2)
    std::vector<std::string> data4 = {"A", "B", "C", "D"};
    oram.access(0, 4, true, data4);
    
    // Read first two blocks via access size 2 (hits R1)
    std::vector<std::string> got2 = oram.access(0, 2);
    ASSERT_EQ("cross-consistency read 0 (R2->R1)", got2.size() > 0 ? got2[0] : "", "A");
    ASSERT_EQ("cross-consistency read 1 (R2->R1)", got2.size() > 1 ? got2[1] : "", "B");
    
    // Read third block via single access (hits R0)
    std::string got1 = oram.access(2, 1)[0];
    ASSERT_EQ("cross-consistency read 2 (R2->R0)", got1, "C");
    
    // Update block 1 via single access (hits R0)
    oram.access(1, 1, true, {"B_mod"});
    
    // Read range 4 again (hits R2, should see R0's write via stash propagation)
    std::vector<std::string> got4 = oram.access(0, 4);
    ASSERT_EQ("cross-consistency write 1 (R0->R2)", got4.size() > 1 ? got4[1] : "", "B_mod");
    
    // Harder test: Trigger many eviction cycles
    for (int round = 0; round < 10; ++round) {
        oram.access(0, 4);
    }
    
    // Verify data is still intact after heavy eviction
    std::vector<std::string> got4_final = oram.access(0, 4);
    ASSERT_TRUE("cross-consistency stress check returns correct size", got4_final.size() == 4, "");
    if (got4_final.size() == 4) {
        ASSERT_EQ("cross-consistency stress check [0]", got4_final[0], "A");
        ASSERT_EQ("cross-consistency stress check [1]", got4_final[1], "B_mod");
        ASSERT_EQ("cross-consistency stress check [2]", got4_final[2], "C");
        ASSERT_EQ("cross-consistency stress check [3]", got4_final[3], "D");
    }
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

    std::cout << "Creating PathORAM tests...\n";

    // Each test gets a fresh PathORAM instance so state doesn't bleed between tests
    { PathORAM oram(N, Z, "test_many.bin"); test_many_blocks(oram, N); }
    { PathORAM oram(N, Z, "test_overwrite.bin"); test_overwrite(oram); }
    { PathORAM oram(N, Z, "test_seq.bin"); test_sequential_access(oram); }
    { PathORAM oram(N, Z, "test_all.bin"); test_all_blocks(oram, N); }
    { PathORAM oram(N, Z, "test_stash.bin"); test_stash_bounded(oram, N); }
    { PathORAM oram(N, Z, "test_pos.bin"); test_position_remap(oram); }

    int ell = 2; // Supports ranges up to 2^2 = 4 blocks
    std::cout << "\nCreating rORAM with N=" << N << ", Z=" << Z << ", ell=" << ell << "\n";
    
    { rORAM oram(N, Z, ell, "roram_single"); test_roram_single(oram, N); }
    { rORAM oram(N, Z, ell, "roram_overwrite"); test_roram_overwrite(oram); }
    { rORAM oram(N, Z, ell, "roram_range"); test_roram_range(oram); }
    { rORAM oram(N, Z, ell, "roram_cross"); test_roram_cross_consistency(oram); }

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