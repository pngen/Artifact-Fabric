cmake_minimum_required(VERSION 3.28)

project(ArtifactFabric
  VERSION 1.0.0
  DESCRIPTION "Artifact Fabric - a production systems runtime for machine-produced AI artifact lifecycle and authority"
  HOMEPAGE_URL "https://github.com/pngen/Artifact-Fabric"
  LANGUAGES CXX)

option(ARTIFACT_FABRIC_WITH_CUDA "Enable CUDA-backed artifact proof (CUDA toolkit + supported NVIDIA GPU)" OFF)
option(ARTIFACT_FABRIC_BUILD_TESTS "Build tests" ON)
option(ARTIFACT_FABRIC_BUILD_EXAMPLES "Build examples" ON)
option(ARTIFACT_FABRIC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(ARTIFACT_FABRIC_BUILD_TOOLS "Build CLI tools" ON)
option(ARTIFACT_FABRIC_ENABLE_ASAN "Build with AddressSanitizer (MSVC /fsanitize=address)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(AF_WARN_FLAGS "")
if(MSVC)
  add_compile_definitions(NOMINMAX)
  set(AF_WARN_FLAGS /W4 /WX /permissive- /EHsc /Zc:__cplusplus /Zc:preprocessor /utf-8)
  add_compile_options(/utf-8)
  add_compile_options(/wd4514 /wd4711 /wd4127)
else()
  set(AF_WARN_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(artifact_fabric STATIC
  src/catalog.cpp
  src/compat.cpp
  src/dependency.cpp
  src/descriptor.cpp
  src/persistence.cpp
  src/reuse.cpp
  src/protocol.cpp
  src/net.cpp
  src/distributed.cpp
  src/cuda.cpp
  src/runtime.cpp
  src/explain.cpp)

add_library(af::artifact_fabric ALIAS artifact_fabric)

target_include_directories(artifact_fabric PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_compile_options(artifact_fabric PUBLIC ${AF_WARN_FLAGS})
target_compile_features(artifact_fabric PUBLIC cxx_std_20)

target_compile_definitions(artifact_fabric PUBLIC
  ARTIFACT_FABRIC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  ARTIFACT_FABRIC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  ARTIFACT_FABRIC_VERSION_PATCH=${PROJECT_VERSION_PATCH})

set_target_properties(artifact_fabric PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  OUTPUT_NAME artifact_fabric
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

if(ARTIFACT_FABRIC_ENABLE_ASAN)
  if(MSVC)
    target_compile_options(artifact_fabric PUBLIC /fsanitize=address /Zi)
    target_link_options(artifact_fabric PUBLIC /fsanitize=address)
  endif()
endif()

if(WIN32)
  target_link_libraries(artifact_fabric PUBLIC ws2_32)
endif()

if(ARTIFACT_FABRIC_WITH_CUDA)
  find_package(CUDAToolkit REQUIRED)
  message(STATUS "Artifact Fabric: CUDA toolkit ${CUDAToolkit_VERSION} enabled for sm_120 hardware proof")
  target_compile_definitions(artifact_fabric PUBLIC ARTIFACT_FABRIC_WITH_CUDA=1)
  target_link_libraries(artifact_fabric PUBLIC CUDA::cudart CUDA::cuda_driver CUDA::nvrtc)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS artifact_fabric
  EXPORT ArtifactFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/artifact_fabric
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ArtifactFabricTargets
  FILE ArtifactFabricTargets.cmake
  NAMESPACE af::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ArtifactFabric)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArtifactFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ArtifactFabricConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ArtifactFabric)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ArtifactFabricConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/ArtifactFabricConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/ArtifactFabricConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ArtifactFabric)

enable_testing()

if(ARTIFACT_FABRIC_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(ARTIFACT_FABRIC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(ARTIFACT_FABRIC_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(ARTIFACT_FABRIC_BUILD_TOOLS)
  add_subdirectory(tools)
endif()
