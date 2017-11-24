add_library(benchmark benchmark.c dgemm.c sha256.c HACCmk.c stream.c fwq.c)
target_include_directories(benchmark PUBLIC .)
