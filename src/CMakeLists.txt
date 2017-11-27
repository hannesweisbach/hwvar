add_subdirectory(benchmarks)
add_executable(hwperfvar main.c worker.c barrier.c)
set_source_files_properties(main.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE) # for asprintf
target_link_libraries(hwperfvar benchmark ${HWLOC_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
