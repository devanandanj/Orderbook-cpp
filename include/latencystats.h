#pragma once
#include <vector>
#include <algorithm>
#include <iostream>

/*
   LatencyStats
   ----------------
   Collects timing samples (in nanoseconds) for one pipeline stage
   (e.g. "parse" or "apply") and reports them as percentiles instead
   of a plain average, since an average hides tail spikes that matter
   for latency-sensitive work.
*/
struct LatencyStats {
    std::vector<long long> latency_samples_ns;   // one entry per recorded call, in nanoseconds

    // Record one latency sample. Call this once per message, per stage.
    void record(const long long latency_ns) {
        latency_samples_ns.push_back(latency_ns);
    }

    // Print median, 99th percentile, and worst-case latency for this stage.
    void report(const std::string& stage_label) {
        if (latency_samples_ns.empty()) {
            std::cout << stage_label << ": no samples\n";
            return;
        }

        std::ranges::sort(latency_samples_ns);

        const size_t sample_count = latency_samples_ns.size();

        // median: half of all messages were faster than this, half slower.
        // this is the TYPICAL latency.
        const long long median_ns = latency_samples_ns[sample_count * 50 / 100];

        // 99th percentile: 99% of messages were faster than this; only the
        // slowest ~1% took this long or longer. This is the TAIL / worst-case
        // latency, usually what matters most in latency-sensitive systems.
        const long long p99_latency_ns = latency_samples_ns[sample_count * 99 / 100];

        // the single slowest sample recorded in the whole run.
        const long long worst_case_ns = latency_samples_ns.back();

        std::cout << stage_label
                   << ": median=" << median_ns << "ns"
                   << " p99=" << p99_latency_ns << "ns"
                   << " worst_case=" << worst_case_ns << "ns"
                   << " (samples=" << sample_count << ")\n";
    }
};