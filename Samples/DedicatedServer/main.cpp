// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdio>
#include <string>

#include "dedicated_server_sample_uve.h"

int main() {
    UVE::Samples::DedicatedServerSampleConfigUVE config;
    config.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    const UVE::Samples::DedicatedServerSampleResultUVE result =
        UVE::Samples::RunDedicatedServerSampleUVE(config);
    if (!result.success) {
        std::printf("SAMPLE FAIL (%zu violation%s):\n", result.failures.size(),
                    result.failures.size() == 1U ? "" : "s");
        for (const std::string& failure : result.failures) {
            std::printf("  - %s\n", failure.c_str());
        }
        return 1;
    }
    std::printf("SAMPLE PASS: %llu ticks, reconciles amy/bob/cat %llu/%llu/%llu, shouts heard %llu/%llu/%llu\n",
                static_cast<unsigned long long>(result.stats.ticks),
                static_cast<unsigned long long>(result.stats.reconcilesAmy),
                static_cast<unsigned long long>(result.stats.reconcilesBob),
                static_cast<unsigned long long>(result.stats.reconcilesCat),
                static_cast<unsigned long long>(result.stats.shoutsHeardAmy),
                static_cast<unsigned long long>(result.stats.shoutsHeardBob),
                static_cast<unsigned long long>(result.stats.shoutsHeardCat));
    return 0;
}
