cmake_minimum_required(VERSION 3.28.0)
project(
  vkguide
  VERSION 1.0.0
  LANGUAGES C CXX)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/vk-bootstrap)

include(CTest)
enable_testing()
find_package(SDL2 CONFIG REQUIRED)
find_package(Vulkan)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

add_executable(${CMAKE_PROJECT_NAME} src/main.cpp)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wall
    -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
          $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
          Vulkan::Vulkan
          GPUOpen::VulkanMemoryAllocator
          fmt::fmt
          glm::glm
          spdlog::spdlog
          absl::base
          vk-bootstrap::vk-bootstrap)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
