cmake_minimum_required(VERSION 3.0.0)
project(client VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_COMPILER /usr/bin/gcc)

include(CTest)
enable_testing()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB glib-2.0)

pkg_check_modules(ZIP libzip)
include_directories(${GLIB_INCLUDE_DIRS})
include_directories(${ZIP_INCLUDE_DIRS})

add_executable(client main.c)
target_link_libraries(client ${GLIB_LIBRARIES} ${ZIP_LIBRARIES})

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)