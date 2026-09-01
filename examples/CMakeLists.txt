# Artifact Fabric examples
set(EXAMPLES
 01_basic_publication
 02_content_addressed
 03_dependency_graph
 04_compatibility_check
 05_reuse_eligibility
 06_supersession
 07_invalidation_propagation
 08_quarantine
 09_deduplicated_backing
 10_single_flight
 11_persistence_recovery
 12_authority_fencing
 13_cuda_artifact
 14_generation_rollover
 15_explainability)
foreach(e IN LISTS EXAMPLES)
  add_executable(${e} ${e}.cpp)
  target_link_libraries(${e} PRIVATE af::artifact_fabric)
endforeach()
