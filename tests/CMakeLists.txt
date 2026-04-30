add_executable(test_orderbook test_orderbook.cpp)
target_link_libraries(test_orderbook PRIVATE ordermatching)
add_test(NAME orderbook COMMAND test_orderbook)

add_executable(test_stream test_stream.cpp)
target_link_libraries(test_stream PRIVATE ordermatching)
add_test(NAME stream COMMAND test_stream)

add_executable(test_advanced test_advanced.cpp)
target_link_libraries(test_advanced PRIVATE ordermatching)
add_test(NAME advanced COMMAND test_advanced)

add_executable(test_v4_snapshot test_v4_snapshot.cpp)
target_link_libraries(test_v4_snapshot PRIVATE ordermatching)
add_test(NAME v4_snapshot COMMAND test_v4_snapshot)
