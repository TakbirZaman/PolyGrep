find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
  message(STATUS "Qt6 Widgets not found - skipping tf_qt (set CMAKE_PREFIX_PATH to your Qt install)")
  return()
endif()

set(CMAKE_AUTOMOC ON)
add_executable(tf_qt WIN32 main.cpp MainWindow.cpp MainWindow.h SearchWorker.cpp SearchWorker.h)
target_link_libraries(tf_qt PRIVATE Qt6::Widgets threadforge_core)
