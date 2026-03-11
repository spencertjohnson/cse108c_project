#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <random>
#include <cmath>
#include "path_oram.hpp"
#include "r_oram.hpp"

using namespace std;
using namespace std::chrono;

void run_benchmark(int N, int r, int ell) {
    cout << "\n============================================\n";
    cout << " BENCHMARK: N=" << N << ", Range Size r=" << r << endl;
    cout << "============================================\n";

    // Initialize ORAMs
    PathORAM p_oram(N, 4, "bench_path.bin");
    rORAM r_oram(N, 4, ell, "bench_roram"); 

    // Warm up: fill data
    // For Path ORAM, just fill a few to avoid overhead if N is large
    int fill_limit = min(N, 100);
    for (int i = 0; i < fill_limit; ++i) {
        p_oram.access(i, "init", true);
    }
    
    // rORAM fill in chunks of r
    for (int i = 0; i < fill_limit; i += r) {
        vector<string> data;
        for (int k = 0; k < r && i + k < N; ++k) data.push_back("init");
        r_oram.access(i, r, true, data);
    }

    p_oram.reset_counts();
    r_oram.reset_counts();

    // To keep benchmark time reasonable for large ell, we'll do fewer operations
    // but enough to get a trend.
    int num_ops = 5; 
    
    // --- Path ORAM Benchmark ---
    auto start_p = high_resolution_clock::now();
    for (int op = 0; op < num_ops; ++op) {
        int start_addr = (op * r) % (N - r + 1);
        for (int i = 0; i < r; ++i) {
            p_oram.access(start_addr + i);
        }
    }
    auto end_p = high_resolution_clock::now();
    auto duration_p = duration_cast<microseconds>(end_p - start_p);

    // --- rORAM Benchmark ---
    auto start_r = high_resolution_clock::now();
    for (int op = 0; op < num_ops; ++op) {
        int start_addr = (op * r) % (N - r + 1);
        r_oram.access(start_addr, r);
    }
    auto end_r = high_resolution_clock::now();
    auto duration_r = duration_cast<microseconds>(end_r - start_r);

    // Report
    cout << left << setw(15) << "Metric" << setw(15) << "Path ORAM" << setw(15) << "rORAM" << endl;
    cout << string(45, '-') << endl;
    cout << left << setw(15) << "Total Blocks" << setw(15) << num_ops * r << setw(15) << num_ops * r << endl;
    cout << left << setw(15) << "Avg Latency" << setw(15) << (double)duration_p.count()/1000.0/num_ops << " ms" << setw(15) << (double)duration_r.count()/1000.0/num_ops << " ms" << " (per range op)" << endl;
    cout << left << setw(15) << "Path Reads" << setw(15) << p_oram.get_path_read_count() << setw(15) << r_oram.get_total_path_reads() << endl;
    cout << left << setw(15) << "Path Writes" << setw(15) << p_oram.get_path_write_count() << setw(15) << r_oram.get_total_path_writes() << endl;
    cout << left << setw(15) << "Bandwidth" << setw(15) << p_oram.get_path_read_count() + p_oram.get_path_write_count() 
         << setw(15) << r_oram.get_total_path_reads() + r_oram.get_total_path_writes() << " (total paths)" << endl;
}

int main() {
    int N = 1024;
    int ell = 8; // supports up to 2^8 = 256
    
    run_benchmark(N, 1, ell);
    run_benchmark(N, 4, ell);
    run_benchmark(N, 16, ell);
    run_benchmark(N, 64, ell);
    run_benchmark(N, 256, ell);
    
    return 0;
}
