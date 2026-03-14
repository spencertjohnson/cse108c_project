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
    long long path_writes;
    long long node_reads;
    long long node_writes;
};

// -----------------------------------------------------------------------
// PathORAM trial
// -----------------------------------------------------------------------

TrialResult run_single_trial_path(int N, int r, int num_ops, const string& trial_name) {
    PathORAM oram(N, "data/" + trial_name + "_path.bin");

    // Fill with initial data
    uint8_t buf[BLOCK_SIZE];
    for (int i = 0; i < N; ++i) {
        std::memset(buf, i & 0xFF, BLOCK_SIZE);
        oram.access(i, buf, true, nullptr);
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
        for (int i = 0; i < r; ++i)
            oram.access(start_addr + i, nullptr, false, out);
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
        oram.get_path_write_count(),
        oram.get_node_read_count(),
        oram.get_node_write_count()
    };
}

// -----------------------------------------------------------------------
// rORAM trial (commented out until rORAM is ready)
// -----------------------------------------------------------------------

// TrialResult run_single_trial_roram(int N, int r, int ell, int num_ops, const string& trial_name) {
//     rORAM r_oram(N, ell, "data/" + trial_name + "_roram");
//
//     std::vector<int> perm(N);
//     std::iota(perm.begin(), perm.end(), 0);
//     std::shuffle(perm.begin(), perm.end(), std::mt19937{std::random_device{}()});
//
//     uint8_t buf[BLOCK_SIZE];
//     for (int i = 0; i < N; i += r) {
//         for (int k = 0; k < r && i + k < N; ++k) {
//             std::memset(buf, (i + k) & 0xFF, BLOCK_SIZE);
//             r_oram.access(i + k, buf, true, nullptr);
//         }
//     }
//     r_oram.reset_counts();
//
//     std::mt19937 rng{std::random_device{}()};
//     std::uniform_int_distribution<int> dist(0, N - r);
//     vector<double> latencies;
//     latencies.reserve(num_ops);
//
//     uint8_t out[BLOCK_SIZE];
//     auto start_all = high_resolution_clock::now();
//     for (int op = 0; op < num_ops; ++op) {
//         int start_addr = dist(rng);
//         auto t0 = high_resolution_clock::now();
//         r_oram.access_range(start_addr, r, nullptr, false, out);
//         auto t1 = high_resolution_clock::now();
//         latencies.push_back(duration_cast<microseconds>(t1 - t0).count() / 1000.0);
//     }
//     auto end_all = high_resolution_clock::now();
//     double total_sec = duration_cast<microseconds>(end_all - start_all).count() / 1000000.0;
//
//     double sum = 0;
//     for (double d : latencies) sum += d;
//     return {
//         sum / num_ops,
//         *min_element(latencies.begin(), latencies.end()),
//         *max_element(latencies.begin(), latencies.end()),
//         num_ops / total_sec,
//         r_oram.get_node_read_count(),
//         r_oram.get_node_write_count()
//     };
// }

// -----------------------------------------------------------------------
// Reporting
// -----------------------------------------------------------------------

void report_pathoram(int N, int r, const vector<TrialResult>& results) {
    double lat = 0, mini = 1e9, maxi = 0, thr = 0;
    long long pr = 0, pw = 0, nr = 0, nw = 0;
    for (auto& res : results) {
        lat  += res.avg_latency;
        mini  = min(mini, res.min_latency);
        maxi  = max(maxi, res.max_latency);
        thr  += res.throughput;
        pr   += res.path_reads;
        pw   += res.path_writes;
        nr   += res.node_reads;
        nw   += res.node_writes;
    }
    int n = (int)results.size();
    TrialResult avg = { lat/n, mini, maxi, thr/n, pr/n, pw/n, nr/n, nw/n };

    cout << "\n============================================\n";
    cout << " PathORAM: N=" << N << ", Range r=" << r
         << " (avg of " << n << " trial" << (n > 1 ? "s" : "") << ")\n";
    cout << "============================================\n";
    cout << fixed << setprecision(2);
    cout << left << setw(16) << "Avg Latency"  << avg.avg_latency  << " ms\n";
    cout << left << setw(16) << "Min Latency"  << avg.min_latency  << " ms\n";
    cout << left << setw(16) << "Max Latency"  << avg.max_latency  << " ms\n";
    cout << left << setw(16) << "Throughput"   << avg.throughput   << " q/s\n";
    cout << left << setw(16) << "Path Reads"   << avg.path_reads   << "\n";
    cout << left << setw(16) << "Path Writes"  << avg.path_writes  << "\n";
    cout << left << setw(16) << "Node Reads"   << avg.node_reads   << "\n";
    cout << left << setw(16) << "Node Writes"  << avg.node_writes  << "\n";
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int N        = 1024;
    const int num_ops  = 20;
    const int num_trials = 1;

    const vector<int> ranges = {1, 4, 16, 64, 256};

    for (int r : ranges) {
        if (r > N) {
            cout << "Skipping r=" << r << " (larger than N=" << N << ")\n";
            continue;
        }

        cout << "Running range r=" << r << "...\n";

        vector<TrialResult> p_results;

        for (int t = 0; t < num_trials; ++t) {
            cout << "  Trial " << t + 1 << "/" << num_trials << "\n";
            string trial_name = "t" + to_string(t) + "_r" + to_string(r);

            p_results.push_back(run_single_trial_path(N, r, num_ops, trial_name));

            // Clean up bin file after each trial
            filesystem::remove("data/" + trial_name + "_path.bin");
        }

        report_pathoram(N, r, p_results);

        // rORAM comparison — uncomment when rORAM is ready
        // int ell = (r > 1) ? (int)ceil(log2((double)r)) : 0;
        // vector<TrialResult> r_results;
        // for (int t = 0; t < num_trials; ++t) {
        //     string trial_name = "t" + to_string(t) + "_r" + to_string(r);
        //     r_results.push_back(run_single_trial_roram(N, r, ell, num_ops, trial_name));
        //     for (int j = 0; j <= ell; ++j)
        //         filesystem::remove("data/" + trial_name + "_roram_sub_" + to_string(j) + ".bin");
        // }
        // report_comparison(N, r, p_results, r_results);
    }

    return 0;
}
