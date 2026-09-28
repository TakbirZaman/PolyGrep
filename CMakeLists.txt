cmake_minimum_required(VERSION 3.20)
project(ThreadForge VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

option(TF_BUILD_TESTS "Build unit tests" ON)
option(TF_BUILD_QT    "Build the Qt GUI (needs Qt6 Widgets)" ON)
option(TF_BUILD_MFC   "Build the MFC GUI (Windows + MSVC only)" ON)

find_package(Threads REQUIRED)

# ---- C++ engine (static) ---------------------------------------------------
add_library(threadforge_core STATIC core/src/engine.cpp core/src/thread_pool.cpp)
target_include_directories(threadforge_core PUBLIC core/include)
target_link_libraries(threadforge_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(threadforge_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(threadforge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- C ABI shared library (consumed by C#, MFC) ------------------------------
add_library(threadforge SHARED core/src/capi.cpp)
target_compile_definitions(threadforge PRIVATE TF_BUILD_DLL)
target_link_libraries(threadforge PRIVATE threadforge_core)
target_include_directories(threadforge PUBLIC core/include)
set_target_properties(threadforge PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# ---- CLI ---------------------------------------------------------------------
add_executable(tf_cli cli/main.cpp)
target_link_libraries(tf_cli PRIVATE threadforge_core)

# ---- Tests -------------------------------------------------------------------
if(TF_BUILD_TESTS)
  enable_testing()
  add_executable(tf_tests tests/test_main.cpp)
  target_link_libraries(tf_tests PRIVATE threadforge_core threadforge)
  add_test(NAME threadforge COMMAND tf_tests)
endif()

# ---- GUIs --------------------------------------------------------------------
if(TF_BUILD_QT)
  add_subdirectory(qt)
endif()
if(TF_BUILD_MFC AND WIN32 AND MSVC)
  add_subdirectory(mfc)
endif()
