cmake_minimum_required(VERSION 3.10)
project(gang_malloc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Werror -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Wall -Werror")

include_directories(gang-malloc)
add_library(gang-malloc SHARED gang-malloc/gang-malloc.c)
install(TARGETS gang-malloc DESTINATION lib/gang-malloc)
install(FILES gang-malloc/gang-malloc.h DESTINATION include/gang-malloc)

add_executable(stress-tests tests/stress-tests.c gang-malloc/gang-malloc.c)
set_target_properties(stress-tests PROPERTIES COMPILE_DEFINITIONS "NUM_RUNS=10 ; MAX_GANG_SIZE=128")
target_include_directories(stress-tests PRIVATE gang-malloc)
target_link_libraries(stress-tests pthread)
