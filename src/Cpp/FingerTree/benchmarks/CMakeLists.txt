add_executable(
    fingertree_benchmarks
    fingertree_benchmarks.cpp
    benchmark_allocation.cpp
)
target_link_libraries(
    fingertree_benchmarks
    PRIVATE
        tools::data_structures::finger_tree
)
target_include_directories(
    fingertree_benchmarks
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NOT FINGERTREE_ENABLE_ALLOCATION_TRACKING)
    target_compile_definitions(
        fingertree_benchmarks
        PRIVATE
            FINGERTREE_DISABLE_ALLOCATION_TRACKING=1
    )
endif()
fingertree_configure_target(fingertree_benchmarks)

# Debug information for the aggregate header plus every benchmark specialization can exceed
# COFF's default section count under MinGW. The large-object form is a format switch, not a
# relaxation of the warning or language policy.
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(fingertree_benchmarks PRIVATE -Wa,-mbig-obj)
endif()
