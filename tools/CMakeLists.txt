# Artifact Fabric CLI / distributed tools
add_executable(af_coordinator af_coordinator.cpp)
target_link_libraries(af_coordinator PRIVATE af::artifact_fabric)

add_executable(af_worker af_worker.cpp)
target_link_libraries(af_worker PRIVATE af::artifact_fabric)

add_executable(af af_cli.cpp)
target_link_libraries(af PRIVATE af::artifact_fabric)
