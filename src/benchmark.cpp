#include "path_oram.hpp"
#include "range_tree.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <chrono>
#include <cmath>

// -----------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------
static constexpr int BENCH_BS      = 64;    // primitive block size in bytes
static constexpr int BENCH_N       = 64;    // number of blocks (power of 2)
static constexpr int QUERIES       = 20;    // queries per range size (averaged)

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static std::vector<uint8_t> make_data(int N, int bs) {
    std::vector<uint8_t> d((long)N * bs, 0);
    for (int i = 0; i < N; ++i) {
        std::string s = "block_" + std::to_string(i);
        std::memcpy(d.data() + (long)i * bs, s.c_str(),
                    std::min(s.size(), (size_t)bs - 1));
    }
    return d;
}

using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

// -----------------------------------------------------------------------
// Benchmark structs
// -----------------------------------------------------------------------

struct Result {
    int    len;
    double rt_seeks;
    double rt_bandwidth_kb;
    double rt_latency_ms;
    double rt_qps;
    double po_seeks;
    double po_bandwidth_kb;
    double po_latency_ms;
    double po_qps;
};

// -----------------------------------------------------------------------
// Run
// -----------------------------------------------------------------------

static Result benchmark_range(int N, int bs, int len, int queries,
                               const std::vector<uint8_t>& data) {
    Result r;
    r.len = len;

    int max_start = N - len;
    if (max_start < 0) max_start = 0;

    // ---- RangeTree ----
    {
        RangeTree rt(N, bs, data.data(), "data/bench_rt");
        rt.reset_counts();

        std::vector<uint8_t> out((long)len * bs);
        long long total_seeks = 0, total_bw = 0;
        double    total_ms    = 0.0;

        for (int q = 0; q < queries; ++q) {
            int s = (max_start > 0) ? (q * 7 % max_start) : 0; // deterministic spread
            int t = s + len - 1;

            rt.reset_counts();
            auto t0 = Clock::now();
            rt.access(s, t, out.data());
            auto t1 = Clock::now();

            total_seeks += rt.get_seek_count();
            total_bw    += rt.get_bandwidth();
            total_ms    += Ms(t1 - t0).count();
        }

        r.rt_seeks        = (double)total_seeks  / queries;
        r.rt_bandwidth_kb = (double)total_bw     / queries / 1024.0;
        r.rt_latency_ms   = total_ms             / queries;
        r.rt_qps          = 1000.0 / r.rt_latency_ms;
    }

    // ---- PathORAM (len individual accesses) ----
    {
        PathORAM oram(N, bs, "data/bench_po");

        // write data in first
        std::vector<uint8_t> buf(bs);
        for (int i = 0; i < N; ++i) {
            std::memcpy(buf.data(), data.data() + (long)i * bs, bs);
            oram.access(i, buf.data(), true, nullptr);
        }

        std::vector<uint8_t> out(bs);
        long long total_seeks = 0, total_bw = 0;
        double    total_ms    = 0.0;

        for (int q = 0; q < queries; ++q) {
            int s = (max_start > 0) ? (q * 7 % max_start) : 0;

            oram.reset_counts();
            auto t0 = Clock::now();
            for (int k = 0; k < len; ++k)
                oram.access(s + k, nullptr, false, out.data());
            auto t1 = Clock::now();

            total_seeks += oram.get_seek_count();
            total_bw    += oram.get_bandwidth();
            total_ms    += Ms(t1 - t0).count();
        }

        r.po_seeks        = (double)total_seeks  / queries;
        r.po_bandwidth_kb = (double)total_bw     / queries / 1024.0;
        r.po_latency_ms   = total_ms             / queries;
        r.po_qps          = 1000.0 / r.po_latency_ms;
    }

    return r;
}

// -----------------------------------------------------------------------
// Print table
// -----------------------------------------------------------------------

static void print_table(const std::vector<Result>& results) {
    // Header
    std::cout << "\n";
    std::cout << std::string(105, '-') << "\n";
    std::cout << std::left
              << std::setw(6)  << "len"
              << std::setw(24) << "  --- RangeTree ---"
              << std::setw(24) << ""
              << std::setw(24) << "  --- PathORAM ---"
              << "\n";

    std::cout << std::left
              << std::setw(6)  << ""
              << std::setw(12) << "seeks"
              << std::setw(12) << "bw(KB)"
              << std::setw(12) << "lat(ms)"
              << std::setw(12) << "q/s"
              << std::setw(12) << "seeks"
              << std::setw(12) << "bw(KB)"
              << std::setw(12) << "lat(ms)"
              << std::setw(12) << "q/s"
              << "\n";
    std::cout << std::string(105, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::fixed << std::setprecision(1)
                  << std::left
                  << std::setw(6)  << r.len
                  << std::setw(12) << r.rt_seeks
                  << std::setw(12) << r.rt_bandwidth_kb
                  << std::setw(12) << r.rt_latency_ms
                  << std::setw(12) << r.rt_qps
                  << std::setw(12) << r.po_seeks
                  << std::setw(12) << r.po_bandwidth_kb
                  << std::setw(12) << r.po_latency_ms
                  << std::setw(12) << r.po_qps
                  << "\n";
    }
    std::cout << std::string(105, '-') << "\n";

    // Summary: seeks ratio
    std::cout << "\nSeek ratio (PathORAM / RangeTree) — should grow with len:\n";
    for (const auto& r : results) {
        double ratio = (r.rt_seeks > 0) ? r.po_seeks / r.rt_seeks : 0.0;
        std::cout << "  len=" << std::setw(4) << r.len
                  << "  ratio=" << std::fixed << std::setprecision(2) << ratio
                  << "x\n";
    }
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int N  = BENCH_N;
    const int bs = BENCH_BS;

    std::cout << "Benchmark: N=" << N << " block_size=" << bs
              << " queries_per_len=" << QUERIES << "\n";

    auto data = make_data(N, bs);

    // Range sizes to test: 1, 2, 4, 8, ... up to N
    std::vector<Result> results;
    for (int len = 1; len <= N; len <<= 1)
        results.push_back(benchmark_range(N, bs, len, QUERIES, data));

    print_table(results);

    return 0;
}
