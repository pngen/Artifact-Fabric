# Artifact Fabric tests
add_executable(af_core_tests core_test.cpp)
target_link_libraries(af_core_tests PRIVATE af::artifact_fabric)
add_test(NAME af_core_tests COMMAND af_core_tests)

add_executable(af_distributed_test distributed_test.cpp)
target_link_libraries(af_distributed_test PRIVATE af::artifact_fabric)
add_dependencies(af_distributed_test af_coordinator af_worker)
add_test(NAME af_distributed_test COMMAND af_distributed_test
  $<TARGET_FILE:af_coordinator>
  $<TARGET_FILE:af_worker>
  ${CMAKE_CURRENT_BINARY_DIR}/)

if(ARTIFACT_FABRIC_WITH_CUDA)
  add_executable(af_cuda_test cuda_test.cpp)
  target_link_libraries(af_cuda_test PRIVATE af::artifact_fabric)
  add_test(NAME af_cuda_test COMMAND af_cuda_test)
endif()
