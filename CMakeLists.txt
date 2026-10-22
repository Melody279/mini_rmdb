cmake_minimum_required(VERSION 3.10)
project(mini_rmdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(bptree STATIC
    src/b_plus_tree.cpp
    src/adaptive_hash_index.cpp
)
target_include_directories(bptree PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Day1：B+ 树插入 / 点查询 / 分裂
add_executable(bplus_test
    tests/b_plus_tree_test.cpp
)
target_link_libraries(bplus_test PRIVATE bptree)

# Day2：B+ 树范围查询 / 删除 / 全量结构校验
add_executable(day2_bplus_test
    tests/b_plus_tree_day2_test.cpp
)
target_link_libraries(day2_bplus_test PRIVATE bptree)

# Day2：自适应哈希（热点缓存）
add_executable(adaptive_hash_test
    tests/adaptive_hash_test.cpp
)
target_link_libraries(adaptive_hash_test PRIVATE bptree)

enable_testing()
add_test(NAME bplus_test COMMAND bplus_test)
add_test(NAME day2_bplus_test COMMAND day2_bplus_test)
add_test(NAME adaptive_hash_test COMMAND adaptive_hash_test)
