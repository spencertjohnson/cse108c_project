#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <random>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <string>
#include <filesystem>
#include <cstring>
#include "path_oram.hpp"
// #include "r_oram.hpp"

using namespace std;
using namespace std::chrono;

// -----------------------------------------------------------------------
// Result struct
// -----------------------------------------------------------------------

struct TrialResult {
    double    avg_latency;
    double    min_latency;
    double    max_latency;
    double    throughput;
    long long path_reads;
    long long node_reads;
    long long node_writes;
    long long seek_count;
};

// -----------------------------------------------------------------------
// Standard PathORAM trial — evicts 1 path per access
// -----------------------------------------------------------------------

TrialResult run_single_trial_path(int N, int r, int num_ops,
                                  const string& trial_name) {
    PathORAM oram(N, "data/" + trial_name + "_path.bin");

    uint8_t buf[BLOCK_SIZE];
    for (int i = 0; i < N; ++i) {
        std::memset(buf, i & 0xFF, BLOCK_SIZE);
        oram.access(i, buf, true, nullptr);
        oram.batch_evict(1);
    }
    oram.reset_counts();

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, N - r);

    vector<double> latencies;
    latencies.reserve(num_ops);

    uint8_t out[BLOCK_SIZE];
    auto start_all = high_resolution_clock::now();

    for (int op = 0; op < num_ops; ++op) {
        int start_addr = dist(rng);
        auto t0 = high_resolution_clock::now();
        for (int i = 0; i < r; ++i) {
            oram.access(start_addr + i, nullptr, false, out);
            oram.batch_evict(1);
        }
        auto t1 = high_resolution_clock::now();
        latencies.push_back(duration_cast<microseconds>(t1 - t0).count() / 1000.0);
    }

    auto end_all = high_resolution_clock::now();
    double total_sec = duration_cast<microseconds>(end_all - start_all).count() / 1000000.0;

    double sum = 0;
    for (double d : latencies) sum += d;

    return {
        sum / num_ops,
        *min_element(latencies.begin(), latencies.end()),
        *max_element(latencies.begin(), latencies.end()),
        num_ops / total_sec,
        oram.get_path_read_count(),
        oram.get_node_read_count(),
        oram.get_node_write_count(),
        oram.get_seek_count()
    };
}

// -----------------------------------------------------------------------
// Batched PathORAM trial — accumulates r accesses then evicts r paths at once
// -----------------------------------------------------------------------

TrialResult run_single_trial_batched(int N, int r, int num_ops,
                                     const string& trial_name) {
    PathORAM oram(N, "data/" + trial_name + "_batched.bin");

    uint8_t buf[BLOCK_SIZE];
    for (int i = 0; i < N; ++i) {
        std::memset(buf, i & 0xFF, BLOCK_SIZE);
        oram.access(i, buf, true, nullptr);
    }
    oram.batch_evict(N);
    oram.reset_counts();

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, N - r);

    vector<double> latencies;
    latencies.reserve(num_ops);

    uint8_t out[BLOCK_SIZE];
    auto start_all = high_resolution_clock::now();

    for (int op = 0; op < num_ops; ++op) {
        int start_addr = dist(rng);
        auto t0 = high_resolution_clock::now();

        // Accumulate r accesses without eviction
        for (int i = 0; i < r; ++i)
            oram.access(start_addr + i, nullptr, false, out);

        // Evict r paths at once in one batch
        oram.batch_evict(r);

        auto t1 = high_resolution_clock::now();
        latencies.push_back(duration_cast<microseconds>(t1 - t0).count() / 1000.0);
    }

    auto end_all = high_resolution_clock::now();
    double total_sec = duration_cast<microseconds>(end_all - start_all).count() / 1000000.0;

    double sum = 0;
    for (double d : latencies) sum += d;

    return {
        sum / num_ops,
        *min_element(latencies.begin(), latencies.end()),
        *max_element(latencies.begin(), latencies.end()),
        num_ops / total_sec,
        oram.get_path_read_count(),
        oram.get_node_read_count(),
        oram.get_node_write_count(),
        oram.get_seek_count()
    };
}

// -----------------------------------------------------------------------
// rORAM trial — uncomment when rORAM is ready
// -----------------------------------------------------------------------

// struct rORAMTrialResult { ... };
// rORAMTrialResult run_single_trial_roram(...) { ... }

// -----------------------------------------------------------------------
// Reporting
// -----------------------------------------------------------------------

void report_comparison(int N, int r,
                        const vector<TrialResult>& p_results,
                        const vector<TrialResult>& b_results) {
    auto average = [](const vector<TrialResult>& v) {
        double lat = 0, mini = 1e9, maxi = 0, thr = 0;
        long long pr = 0, nr = 0, nw = 0, sk = 0;
        for (auto& res : v) {
            lat  += res.avg_latency;
            mini  = min(mini, res.min_latency);
            maxi  = max(maxi, res.max_latency);
            thr  += res.throughput;
            pr   += res.path_reads;
            nr   += res.node_reads;
            nw   += res.node_writes;
            sk   += res.seek_count;
        }
        int n = (int)v.size();
        return TrialResult{ lat/n, mini, maxi, thr/n, pr/n, nr/n, nw/n, sk/n };
    };

    TrialResult p = average(p_results);
    TrialResult b = average(b_results);

    cout << "\n============================================\n";
    cout << " N=" << N << ", Range r=" << r
         << " (avg of " << p_results.size() << " trial"
         << (p_results.size() > 1 ? "s" : "") << ")\n";
    cout << "============================================\n";
    cout << fixed << setprecision(2);
    cout << left << setw(18) << "Metric"
         << setw(18) << "PathORAM"
         << setw(18) << "Batched" << "\n";
    cout << string(54, '-') << "\n";
    cout << left << setw(18) << "Avg Latency"
         << setw(15) << p.avg_latency << " ms   "
         << setw(15) << b.avg_latency << " ms\n";
    cout << left << setw(18) << "Min Latency"
         << setw(15) << p.min_latency << " ms   "
         << setw(15) << b.min_latency << " ms\n";
    cout << left << setw(18) << "Max Latency"
         << setw(15) << p.max_latency << " ms   "
         << setw(15) << b.max_latency << " ms\n";
    cout << left << setw(18) << "Throughput"
         << setw(15) << p.throughput  << " q/s  "
         << setw(15) << b.throughput  << " q/s\n";
    cout << left << setw(18) << "Path Reads"
         << setw(18) << p.path_reads
         << setw(18) << b.path_reads  << "\n";
    cout << left << setw(18) << "Node Reads"
         << setw(18) << p.node_reads
         << setw(18) << b.node_reads  << "\n";
    cout << left << setw(18) << "Node Writes"
         << setw(18) << p.node_writes
         << setw(18) << b.node_writes << "\n";
    cout << left << setw(18) << "Seeks"
         << setw(18) << p.seek_count
         << setw(18) << b.seek_count  << "\n";
    cout << left << setw(18) << "Speedup"
         << setw(18) << "1.0x"
         << setw(15) << (p.avg_latency / b.avg_latency) << "x\n";
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int N          = 1024;
    const int num_ops    = 20;
    const int num_trials = 1;

    const vector<int> ranges = {1, 4, 16, 64, 256};

    for (int r : ranges) {
        if (r > N) {
            cout << "Skipping r=" << r << " (larger than N=" << N << ")\n";
            continue;
        }

        cout << "Running range r=" << r << "...\n";

        vector<TrialResult> p_results;
        vector<TrialResult> b_results;

        for (int t = 0; t < num_trials; ++t) {
            cout << "  Trial " << t + 1 << "/" << num_trials << "\n";
            string trial_name = "t" + to_string(t) + "_r" + to_string(r);

            cout << "    Standard PathORAM...\n";
            p_results.push_back(
                run_single_trial_path(N, r, num_ops, trial_name));
            filesystem::remove("data/" + trial_name + "_path.bin");

            cout << "    Batched PathORAM (batch=" << r << ")...\n";
            b_results.push_back(
                run_single_trial_batched(N, r, num_ops, trial_name));
            filesystem::remove("data/" + trial_name + "_batched.bin");
        }

        report_comparison(N, r, p_results, b_results);

        // rORAM comparison — uncomment when rORAM is ready
        // int ell = (r > 1) ? (int)ceil(log2((double)r)) : 0;
        // vector<rORAMTrialResult> roram_results;
        // for (int t = 0; t < num_trials; ++t) { ... }
        // report_roram_comparison(N, r, p_results, roram_results);
    }

    return 0;
}
