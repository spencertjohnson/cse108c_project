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
#include "r_oram.hpp"

using namespace std;
using namespace std::chrono;

// -----------------------------------------------------------------------
// Result structs — separate since rORAM tracks seeks, PathORAM tracks paths
// -----------------------------------------------------------------------

struct PathTrialResult {
    double    avg_latency;
    double    min_latency;
    double    max_latency;
    double    throughput;
    long long path_reads;
    long long path_writes;
    long long node_reads;
    long long node_writes;
};

struct rORAMTrialResult {
    double    avg_latency;
    double    min_latency;
    double    max_latency;
    double    throughput;
    long long total_seeks;
};

// -----------------------------------------------------------------------
// PathORAM trial
// -----------------------------------------------------------------------

PathTrialResult run_single_trial_path(int N, int r, int num_ops,
                                      const string& trial_name) {
    PathORAM oram(N, "data/" + trial_name + "_path.bin");

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
// rORAM trial
// -----------------------------------------------------------------------

rORAMTrialResult run_single_trial_roram(int N, int r, int ell, int num_ops, const string& trial_name) {
    cerr << "    Creating rORAM...\n";
    rORAM oram(N, ell, "data/" + trial_name + "_roram");
    cerr << "    rORAM created\n";

    int chunk = 1 << ell;
    cerr << "    Filling with chunk=" << chunk << "\n";
    for (int i = 0; i < N; i += chunk) {
        cerr << "    Writing chunk at i=" << i << "\n";
        std::vector<uint8_t> chunk_buf(chunk * BLOCK_SIZE, 0);
        for (int k = 0; k < chunk && i + k < N; ++k)
            std::memset(chunk_buf.data() + k * BLOCK_SIZE, (i + k) & 0xFF, BLOCK_SIZE);
        oram.access(i, chunk, chunk_buf.data(), true, nullptr);
    }
    cerr << "    Fill complete\n";

    oram.reset_counts();

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, N - r);

    vector<double> latencies;
    latencies.reserve(num_ops);

    std::vector<uint8_t> out(r * BLOCK_SIZE);
    auto start_all = high_resolution_clock::now();

    for (int op = 0; op < num_ops; ++op) {
        int start_addr = dist(rng);
        auto t0 = high_resolution_clock::now();
        oram.access(start_addr, r, nullptr, false, out.data());
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
                        const vector<PathTrialResult>& p_results,
                        const vector<rORAMTrialResult>& r_results) {
    double p_lat = 0, p_mini = 1e9, p_maxi = 0, p_thr = 0;
    long long p_pr = 0, p_pw = 0, p_nr = 0, p_nw = 0;
    for (auto& res : p_results) {
        p_lat  += res.avg_latency;
        p_mini  = min(p_mini, res.min_latency);
        p_maxi  = max(p_maxi, res.max_latency);
        p_thr  += res.throughput;
        p_pr   += res.path_reads;
        p_pw   += res.path_writes;
        p_nr   += res.node_reads;
        p_nw   += res.node_writes;
    }
    int pn = (int)p_results.size();
    PathTrialResult p_avg = { p_lat/pn, p_mini, p_maxi, p_thr/pn,
                               p_pr/pn, p_pw/pn, p_nr/pn, p_nw/pn };

    double r_lat = 0, r_mini = 1e9, r_maxi = 0, r_thr = 0;
    long long r_seeks = 0;
    for (auto& res : r_results) {
        r_lat   += res.avg_latency;
        r_mini   = min(r_mini, res.min_latency);
        r_maxi   = max(r_maxi, res.max_latency);
        r_thr   += res.throughput;
        r_seeks += res.total_seeks;
    }
    int rn = (int)r_results.size();
    rORAMTrialResult r_avg = { r_lat/rn, r_mini, r_maxi, r_thr/rn, r_seeks/rn };

    cout << "\n============================================\n";
    cout << " N=" << N << ", Range r=" << r
         << " (avg of " << pn << " trial" << (pn > 1 ? "s" : "") << ")\n";
    cout << "============================================\n";
    cout << fixed << setprecision(2);
    cout << left << setw(18) << "Metric"
         << setw(18) << "PathORAM"
         << setw(18) << "rORAM" << "\n";
    cout << string(54, '-') << "\n";
    cout << left << setw(18) << "Avg Latency"
         << setw(15) << p_avg.avg_latency << " ms   "
         << setw(15) << r_avg.avg_latency << " ms\n";
    cout << left << setw(18) << "Min Latency"
         << setw(15) << p_avg.min_latency << " ms   "
         << setw(15) << r_avg.min_latency << " ms\n";
    cout << left << setw(18) << "Max Latency"
         << setw(15) << p_avg.max_latency << " ms   "
         << setw(15) << r_avg.max_latency << " ms\n";
    cout << left << setw(18) << "Throughput"
         << setw(15) << p_avg.throughput << " q/s  "
         << setw(15) << r_avg.throughput << " q/s\n";
    cout << left << setw(18) << "Path Reads"
         << setw(18) << p_avg.path_reads
         << setw(18) << "N/A" << "\n";
    cout << left << setw(18) << "Path Writes"
         << setw(18) << p_avg.path_writes
         << setw(18) << "N/A" << "\n";
    cout << left << setw(18) << "Node Reads"
         << setw(18) << p_avg.node_reads
         << setw(18) << "N/A" << "\n";
    cout << left << setw(18) << "Node Writes"
         << setw(18) << p_avg.node_writes
         << setw(18) << "N/A" << "\n";
    cout << left << setw(18) << "Total Seeks"
         << setw(18) << "N/A"
         << setw(18) << r_avg.total_seeks << "\n";
    cout << left << setw(18) << "Speedup"
         << setw(18) << "1.0x"
         << setw(15) << (p_avg.avg_latency / r_avg.avg_latency) << "x\n";
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

        int ell = (r > 1) ? (int)ceil(log2((double)r)) : 0;
        if (ell > MAX_TAGS - 1) {
            cout << "Skipping r=" << r << " (ell=" << ell
                 << " exceeds MAX_TAGS-1=" << MAX_TAGS - 1 << ")\n";
            continue;
        }

        cout << "Running range r=" << r << " (ell=" << ell << ")...\n";

        vector<PathTrialResult>  p_results;
        vector<rORAMTrialResult> r_results;

        for (int t = 0; t < num_trials; ++t) {
            cout << "  Trial " << t + 1 << "/" << num_trials << "\n";
            string trial_name = "t" + to_string(t) + "_r" + to_string(r);

            cout << "    PathORAM...\n";
            p_results.push_back(
                run_single_trial_path(N, r, num_ops, trial_name));
            filesystem::remove("data/" + trial_name + "_path.bin");

            cout << "    rORAM...\n";
            r_results.push_back(
                run_single_trial_roram(N, r, ell, num_ops, trial_name));
            for (int j = 0; j <= ell; ++j)
                filesystem::remove("data/" + trial_name + "_roram_sub_"
                                   + to_string(j) + ".bin");
        }

        report_comparison(N, r, p_results, r_results);
    }

    return 0;
}
