add_subdirectory(benchmarks)
add_executable(hwperfvar main.c worker.c barrier.c)
target_link_libraries(hwperfvar benchmark ${HWLOC_LIBRARIES})
