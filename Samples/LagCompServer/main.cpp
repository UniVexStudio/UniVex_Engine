// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdio>
#include <string>

#include "lag_comp_server_sample_uve.h"

int main() {
    UVE::Samples::LagCompServerSampleConfigUVE config;
    config.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    const UVE::Samples::LagCompServerSampleResultUVE result =
        UVE::Samples::RunLagCompServerSampleUVE(config);
    if (!result.success) {
        std::printf("SAMPLE FAIL (%zu violation%s):\n", result.failures.size(),
                    result.failures.size() == 1U ? "" : "s");
        for (const std::string& failure : result.failures) {
            std::printf("  - %s\n", failure.c_str());
        }
        return 1;
    }
    std::printf("SAMPLE PASS: ticks %llu shots %llu/%llu/%llu/%llu (noview %llu) frac %llu rays %llu/%llu/%llu/%llu entered %llu lag %llu health %.1f\n",
                static_cast<unsigned long long>(result.stats.ticks),
                static_cast<unsigned long long>(result.stats.shotsFired),
                static_cast<unsigned long long>(result.stats.shotLiveHits),
                static_cast<unsigned long long>(result.stats.shotRewoundHits),
                static_cast<unsigned long long>(result.stats.shotSaves),
                static_cast<unsigned long long>(result.stats.shotsNoView),
                static_cast<unsigned long long>(result.stats.fracInterpolated),
                static_cast<unsigned long long>(result.stats.raysFired),
                static_cast<unsigned long long>(result.stats.rayLiveHits),
                static_cast<unsigned long long>(result.stats.rayRewoundHits),
                static_cast<unsigned long long>(result.stats.raySaves),
                static_cast<unsigned long long>(result.stats.enteredEvents),
                static_cast<unsigned long long>(result.stats.maxViewLag),
                static_cast<double>(result.stats.victimHealth));
    return 0;
}
