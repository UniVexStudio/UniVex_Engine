// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdio>
#include <string>

#include "packet_capture_sample_uve.h"

int main() {
    UVE::Samples::PacketCaptureSampleConfigUVE config;
    config.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    const UVE::Samples::PacketCaptureSampleResultUVE result =
        UVE::Samples::RunPacketCaptureSampleUVE(config);
    if (!result.success) {
        std::printf("SAMPLE FAIL (%zu violation%s):\n", result.failures.size(),
                    result.failures.size() == 1U ? "" : "s");
        for (const std::string& failure : result.failures) {
            std::printf("  - %s\n", failure.c_str());
        }
        return 1;
    }
    std::printf("SAMPLE PASS: pushed %llu kept %llu dropped %llu malformed %llu in/out %llu/%llu rep/rpc/shot %llu/%llu/%llu fire %llu hex %llu\n",
                static_cast<unsigned long long>(result.stats.pushed),
                static_cast<unsigned long long>(result.stats.size),
                static_cast<unsigned long long>(result.stats.drops),
                static_cast<unsigned long long>(result.stats.malformed),
                static_cast<unsigned long long>(result.stats.inbound),
                static_cast<unsigned long long>(result.stats.outbound),
                static_cast<unsigned long long>(result.stats.replication),
                static_cast<unsigned long long>(result.stats.rpc),
                static_cast<unsigned long long>(result.stats.shots),
                static_cast<unsigned long long>(result.stats.fireHits),
                static_cast<unsigned long long>(result.stats.hexdumpBytes));
    return 0;
}
