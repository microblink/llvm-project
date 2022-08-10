cmake_minimum_required( VERSION 3.20 )

add_executable( test-tool Source/Test.cpp )

target_link_libraries( test-tool PRIVATE clangTooling )
target_include_directories(
    test-tool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../clang/include
        ${CMAKE_CURRENT_BINARY_DIR}/../tools/clang/include
)

if ( NOT LLVM_ENABLE_RTTI )
    target_compile_options( test-tool PRIVATE -fno-rtti )
endif()

add_dependencies( test-tool ClangDeclNodes )
