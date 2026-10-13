// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdio>
#include <string>

#include "interest_fanout_sample_uve.h"

int main() {
    UVE::Samples::InterestFanoutSampleConfigUVE config;
    config.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    const UVE::Samples::InterestFanoutSampleResultUVE result =
        UVE::Samples::RunInterestFanoutSampleUVE(config);
    if (!result.success) {
        std::printf("SAMPLE FAIL (%zu violation%s):\n", result.failures.size(),
                    result.failures.size() == 1U ? "" : "s");
        for (const std::string& failure : result.failures) {
            std::printf("  - %s\n", failure.c_str());
        }
        return 1;
    }
    std::printf("SAMPLE PASS: ticks %llu A %llu B %llu C %llu selected %llu dropped %llu skips %llu/%llu/%llu prof %llu/%llu\n",
                static_cast<unsigned long long>(result.stats.ticks),
                static_cast<unsigned long long>(result.stats.receivedA[0] +
                                                result.stats.receivedA[1] +
                                                result.stats.receivedA[2] +
                                                result.stats.receivedA[3] +
                                                result.stats.receivedA[4] +
                                                result.stats.receivedA[5]),
                static_cast<unsigned long long>(result.stats.receivedB[0] +
                                                result.stats.receivedB[1]),
                static_cast<unsigned long long>(result.stats.receivedC[0] +
                                                result.stats.receivedC[1] +
                                                result.stats.receivedC[2] +
                                                result.stats.receivedC[3] +
                                                result.stats.receivedC[4] +
                                                result.stats.receivedC[5]),
                static_cast<unsigned long long>(result.stats.fanoutSelected),
                static_cast<unsigned long long>(result.stats.fanoutBudgetDropped),
                static_cast<unsigned long long>(result.stats.skippedA),
                static_cast<unsigned long long>(result.stats.skippedB),
                static_cast<unsigned long long>(result.stats.skippedC),
                static_cast<unsigned long long>(result.stats.profilerSpans),
                static_cast<unsigned long long>(result.stats.profilerCounters));
    return 0;
}
