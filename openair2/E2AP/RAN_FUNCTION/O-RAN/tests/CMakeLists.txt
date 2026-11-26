# SPDX-License-Identifier: LicenseRef-CSSL-1.0

# openran-lab: conversão da ação Slice-level PRB quota do E2SM-RC
# (ran_func_rc_slice.c), sem depender do MAC nem do RRC
add_executable(test_rc_slice
  test_rc_slice.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../ran_func_rc_slice.c
)
target_include_directories(test_rc_slice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${OPENAIR_DIR})
target_link_libraries(test_rc_slice PRIVATE GTest::gtest GTest::gtest_main)
add_dependencies(tests test_rc_slice)
include(GoogleTest)
gtest_discover_tests(test_rc_slice)
