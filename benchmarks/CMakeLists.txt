# Artifact Fabric benchmarks
add_executable(af_bench bench.cpp)
target_link_libraries(af_bench PRIVATE af::artifact_fabric)
