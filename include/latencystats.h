#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

/*
   LatencyStats
   ----------------
   Collects timing samples for one pipeline stage (e.g. "parse" or
   "apply") and reports median / p99 / min / max instead of an average,
   since an average hides the tail spikes that matter for latency
   work.

   Three things a previous version of this file got wrong -- see the
   inline notes below.
*/
struct LatencyStats {
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;

    // NOTE 1: type-safe API. The old signature was
    //     record(long long latency_ns)
    // which silently trusted the caller to hand over nanoseconds; a
    // wrong duration_cast at the call site produced wrong numbers with
    // no compiler warning. Taking `duration` makes units part of the
    // type, and lets main.cpp pass `t1 - t0` directly without a cast.
    void record(const duration d) { samples.push_back(d); }

    // Reserve capacity up front so push_back inside the timed loop
    // does not reallocate mid-run (a resize copy in the middle of a
    // timing window shows up as an outlier that was never really there).
    void reserve(const std::size_t n) { samples.reserve(n); }

    /*
       report

       NOTE 2: does NOT mutate `samples`. The old report() sorted the
       vector in place; a second report() call, or any post-hoc use of
       the samples, then saw them sorted. Now we sort a copy.

       NOTE 3: `timer_overhead` is the per-sample cost of one pair of
       clock::now() calls, obtained from estimate_timer_overhead(). We
       subtract it from each printed number, so what you see is the
       actual work rather than the work plus the observer. Pass
       duration::zero() to skip the correction.
    */
    void report(const std::string& stage_label,
                const duration timer_overhead = duration::zero()) const {
        if (samples.empty()) {
            std::cout << stage_label << ": no samples\n";
            return;
        }

        std::vector<duration> sorted(samples);
        std::ranges::sort(sorted);

        const std::size_t n = sorted.size();

        // Nearest-rank percentile: for p in {1..100}, pick
        // sorted[ceil(p * n / 100) - 1], clamped to [0, n-1]. The old
        // code did `n * p / 100`, which floor-divides and reads one
        // element to the right; it happened to hit .back() as both p99
        // and worst_case whenever n * 99 / 100 == n - 1, i.e. for every
        // n <= 100 -- so p99 was always the max, not the 99th percentile.
        auto at = [&](const int p_pct) -> duration {
            std::size_t idx = (static_cast<std::size_t>(p_pct) * n + 99) / 100;
            if (idx > 0) --idx;
            if (idx >= n) idx = n - 1;
            return sorted[idx];
        };

        auto ns = [&](const duration d) -> std::int64_t {
            const auto corrected = d - timer_overhead;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(corrected).count();
        };

        std::cout << stage_label
                  << ": min="    << ns(sorted.front()) << "ns"
                  << " median="  << ns(at(50))         << "ns"
                  << " p99="     << ns(at(99))         << "ns"
                  << " max="     << ns(sorted.back())  << "ns"
                  << " (n=" << n
                  << ", timer_overhead="
                  << std::chrono::duration_cast<std::chrono::nanoseconds>(timer_overhead).count()
                  << "ns)\n";

        if (n < 100) {
            std::cout << "  (warning: p99 with n<100 collapses to max;"
                         " results are indicative, not statistical)\n";
        }
    }

    /*
       estimate_timer_overhead

       Runs `iters` back-to-back empty clock::now() pairs and returns
       the median of the resulting empty intervals. Call this ONCE at
       startup, before any timing loops, and pass the return value to
       report() so the printed numbers exclude the observer's own cost.

       Median (not min) because the min of a very short interval on
       Windows QPC is often 0 due to counter granularity, which would
       over-correct.
    */
    static duration estimate_timer_overhead(const std::size_t iters = 4096) {
        std::vector<duration> empties;
        empties.reserve(iters);
        for (std::size_t i = 0; i < iters; ++i) {
            const auto a = clock::now();
            const auto b = clock::now();
            empties.push_back(b - a);
        }
        std::ranges::sort(empties);
        return empties[iters / 2];
    }

    std::vector<duration> samples;
};
