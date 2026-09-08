# benches — microbenchmarks for the ame-next C core (this branch).
#
# bench_core: single headless binary, no GL/SDL window needed. Times the
# hot subsystems (math, pool, events, geometry, input, snapshot, audio
# mixer, text layout, mem sim). Prints ns/op + ops/sec as a plain table
# so results paste straight into BENCH_REPORT.md.
#
# bench_render_sort: compares render-batch sort kernels (old linked-list
# counting sort vs new prefix-sum counting sort) on synthetic keys —
# no GL context needed, same key distribution as rp_end_frame.
#
# Both are Release-optimised (-O3) even in Debug configures so numbers
# are comparable; FP flags stay pinned (-ffp-contract=off).

add_executable(bench_core bench_core.c
    ${CMAKE_SOURCE_DIR}/examples/memory_game/mem_sim.c)
target_include_directories(bench_core PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/memory_game)
target_link_libraries(bench_core PRIVATE ame::3d ame_font)
target_compile_definitions(bench_core PRIVATE AME_3D=1)
target_compile_options(bench_core PRIVATE -O3 -ffp-contract=off -Wall -Wextra -Werror)

add_executable(bench_render_sort bench_render_sort.c)
target_compile_options(bench_render_sort PRIVATE -O3 -ffp-contract=off -Wall -Wextra -Werror)

# `ctest -R bench` runs them (they always "pass"; output is the table).
add_test(NAME bench_core COMMAND bench_core)
add_test(NAME bench_render_sort COMMAND bench_render_sort)
set_tests_properties(bench_core bench_render_sort PROPERTIES LABELS "bench")
