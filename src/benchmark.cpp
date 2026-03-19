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
#include "ro_range_oram.hpp"

using namespace std;
using namespace std::chrono;

// -----------------------------------------------------------------------
// Result structs
// -----------------------------------------------------------------------

struct PathTrialResult {
    double    avg_latency;
    double    min_latency;
    double    max_latency;
    double    throughput;
    long long path_reads;
    long long node_reads;
    long long node_writes;
    long long seek_count;
};

struct ROTrialResult {
    double    avg_latency;
    double    min_latency;
    double    max_latency;
    double    throughput;
    long long seek_count;
};

// -----------------------------------------------------------------------
// PathORAM trial
// -----------------------------------------------------------------------

PathTrialResult run_single_trial_path(int N, int r, int num_ops,
                                      const string& trial_name,
                                      const vector<uint8_t>& init_data) {
    PathORAM oram(N, "data/" + trial_name + "_path.bin");

    for (int i = 0; i < N; ++i) {
        const uint8_t* block = init_data.data() + (long)i * BLOCK_SIZE;
        oram.access(i, block, true, nullptr);
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
// ReadOnlyRangeORAM trial
// -----------------------------------------------------------------------

ROTrialResult run_single_trial_ro(int N, int r, int ell, int num_ops,
                                  const string& trial_name,
                                  const vector<uint8_t>& init_data) {
    ReadOnlyRangeORAM oram(N, ell, init_data.data(),
                           "data/" + trial_name + "_ro");
    oram.reset_counts();

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, N - r);

    vector<double> latencies;
    latencies.reserve(num_ops);

    vector<uint8_t> out((long)r * BLOCK_SIZE);
    auto start_all = high_resolution_clock::now();

    for (int op = 0; op < num_ops; ++op) {
        int start_addr = dist(rng);
        auto t0 = high_resolution_clock::now();
        oram.read(start_addr, r, out.data());
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
        oram.get_total_seeks()
    };
}

// -----------------------------------------------------------------------
// Reporting
// -----------------------------------------------------------------------

void report_comparison(int N, int r,
                        const vector<PathTrialResult>&  p_results,
                        const vector<ROTrialResult>&   ro_results) {
    double p_lat = 0, p_mini = 1e9, p_maxi = 0, p_thr = 0;
    long long p_pr = 0, p_nr = 0, p_nw = 0, p_sk = 0;
    for (auto& res : p_results) {
        p_lat  += res.avg_latency;
        p_mini  = min(p_mini, res.min_latency);
        p_maxi  = max(p_maxi, res.max_latency);
        p_thr  += res.throughput;
        p_pr   += res.path_reads;
        p_nr   += res.node_reads;
        p_nw   += res.node_writes;
        p_sk   += res.seek_count;
    }
    int pn = (int)p_results.size();
    PathTrialResult p = { p_lat/pn, p_mini, p_maxi, p_thr/pn,
                          p_pr/pn, p_nr/pn, p_nw/pn, p_sk/pn };

    double ro_lat = 0, ro_mini = 1e9, ro_maxi = 0, ro_thr = 0;
    long long ro_sk = 0;
    for (auto& res : ro_results) {
        ro_lat  += res.avg_latency;
        ro_mini  = min(ro_mini, res.min_latency);
        ro_maxi  = max(ro_maxi, res.max_latency);
        ro_thr  += res.throughput;
        ro_sk   += res.seek_count;
    }
    int ron = (int)ro_results.size();
    ROTrialResult ro = { ro_lat/ron, ro_mini, ro_maxi, ro_thr/ron, ro_sk/ron };

    cout << "\n============================================\n";
    cout << " N=" << N << ", Range r=" << r
         << " (avg of " << pn << " trial" << (pn > 1 ? "s" : "") << ")\n";
    cout << "============================================\n";
    cout << fixed << setprecision(2);
    cout << left << setw(18) << "Metric"
         << setw(18) << "PathORAM"
         << setw(18) << "RO-RangeORAM" << "\n";
    cout << string(54, '-') << "\n";
    cout << left << setw(18) << "Avg Latency"
         << setw(15) << p.avg_latency  << " ms   "
         << setw(15) << ro.avg_latency << " ms\n";
    cout << left << setw(18) << "Min Latency"
         << setw(15) << p.min_latency  << " ms   "
         << setw(15) << ro.min_latency << " ms\n";
    cout << left << setw(18) << "Max Latency"
         << setw(15) << p.max_latency  << " ms   "
         << setw(15) << ro.max_latency << " ms\n";
    cout << left << setw(18) << "Throughput"
         << setw(15) << p.throughput   << " q/s  "
         << setw(15) << ro.throughput  << " q/s\n";
    cout << left << setw(18) << "Path Reads"
         << setw(18) << p.path_reads
         << setw(18) << "N/A"          << "\n";
    cout << left << setw(18) << "Node Reads"
         << setw(18) << p.node_reads
         << setw(18) << "N/A"          << "\n";
    cout << left << setw(18) << "Node Writes"
         << setw(18) << p.node_writes
         << setw(18) << "N/A"          << "\n";
    cout << left << setw(18) << "Seeks"
         << setw(18) << p.seek_count
         << setw(18) << ro.seek_count  << "\n";
    cout << left << setw(18) << "Speedup"
         << setw(18) << "1.0x"
         << setw(15) << (p.avg_latency / ro.avg_latency) << "x\n";
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    const int N          = 1024;
    const int num_ops    = 20;
    const int num_trials = 1;

    const vector<int> ranges = {1, 4, 16, 64, 256};

    // Generate shared initial data once
    cout << "Generating initial data...\n";
    vector<uint8_t> init_data((long)N * BLOCK_SIZE);
    for (int i = 0; i < N; ++i)
        std::memset(init_data.data() + (long)i * BLOCK_SIZE, i & 0xFF, BLOCK_SIZE);

    for (int r : ranges) {
        if (r > N) {
            cout << "Skipping r=" << r << " (larger than N=" << N << ")\n";
            continue;
        }

        int ell = (r > 1) ? (int)ceil(log2((double)r)) : 0;

        cout << "Running range r=" << r << " (ell=" << ell << ")...\n";

        vector<PathTrialResult> p_results;
        vector<ROTrialResult>   ro_results;

        for (int t = 0; t < num_trials; ++t) {
            cout << "  Trial " << t + 1 << "/" << num_trials << "\n";
            string trial_name = "t" + to_string(t) + "_r" + to_string(r);

            cout << "    PathORAM...\n";
            p_results.push_back(
                run_single_trial_path(N, r, num_ops, trial_name, init_data));
            filesystem::remove("data/" + trial_name + "_path.bin");

            cout << "    ReadOnlyRangeORAM...\n";
            ro_results.push_back(
                run_single_trial_ro(N, r, ell, num_ops, trial_name, init_data));
            for (int j = 0; j <= ell; ++j)
                filesystem::remove("data/" + trial_name + "_ro_sub_"
                                   + to_string(j) + ".bin");
        }

        report_comparison(N, r, p_results, ro_results);
    }

    return 0;
}
