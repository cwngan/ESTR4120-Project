cmake_minimum_required(VERSION 4.1.1)

project(VoiceChat)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

if (OPUS_FOUND)
  add_executable(VoiceChat main.cpp)
  target_include_directories(VoiceChat PRIVATE include)
  target_include_directories(VoiceChat PRIVATE ${OPUS_INCLUDE_DIRS})
  # Link the imported pkg-config target so libopus is linked (provides include/libs)
  target_link_libraries(VoiceChat PRIVATE PkgConfig::OPUS)
endif()