cmake_minimum_required(VERSION 3.16)
project(uartlink_host C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror -O2)

enable_testing()
add_executable(test_link test_link.c ../main/link_check.c)
target_include_directories(test_link PRIVATE ../main)
target_link_libraries(test_link m)
add_test(NAME test_link COMMAND test_link)
