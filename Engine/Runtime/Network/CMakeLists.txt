# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# uve_network: reliable UDP packet framing plus the transport seam (loopback and UDP
# sockets), sessions, the replication core, and RPC. Depends on uve_math (PUBLIC: positions
# cross the replication header), uve_uvscript (PUBLIC: RPC args are script values),
# uve_diagnostics (PUBLIC: the profiler emitters name capture types) and
# uve_utilities (PRIVATE: the wire codec stays in the .cpp); Winsock on Windows for the UDP
# socket calls.

add_library(uve_network STATIC
    Internal/network_conditions_uve.cpp
    Internal/network_inspector_uve.cpp
    Internal/network_interest_fanout_uve.cpp
    Internal/network_interest_uve.cpp
    Internal/network_lobby_uve.cpp
    Internal/network_packet_capture_uve.cpp
    Internal/network_prediction_uve.cpp
    Internal/network_profiler_uve.cpp
    Internal/network_relay_uve.cpp
    Internal/network_replication_uve.cpp
    Internal/network_rpc_uve.cpp
    Internal/network_session_uve.cpp
    Internal/network_shot_uve.cpp
    Internal/network_transport_uve.cpp
    Internal/network_udp_uve.cpp
    Internal/network_value_codec_uve.cpp
    Internal/reliable_packet_window_uve.cpp
)
target_include_directories(uve_network PUBLIC Expose)
target_compile_features(uve_network PUBLIC cxx_std_23)
target_link_libraries(uve_network PUBLIC uve_math uve_uvscript uve_diagnostics PRIVATE uve_utilities)
if(WIN32)
    target_link_libraries(uve_network PRIVATE ws2_32)
endif()
uve_set_warnings(uve_network)
