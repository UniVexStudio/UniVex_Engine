# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
# Included after GoogleTest discovery, in CTest's scope. These cases are independent
# processes but deliberately bind the same two loopback ports. Protect the resource,
# not the whole suite: unrelated tests remain parallel and no assertion is weakened.
foreach(_uve_test IN LISTS uve_core_tests_TESTS)
    if(_uve_test MATCHES "^UdpTransportUVE\\.")
        set_tests_properties("${_uve_test}" PROPERTIES RESOURCE_LOCK uve_udp_ports_41741_41742)
    endif()
endforeach()
unset(_uve_test)
