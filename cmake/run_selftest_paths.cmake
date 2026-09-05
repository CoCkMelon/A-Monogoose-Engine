# Regression test: `memory --selftest` / `biscuit --selftest` must write their
# BMP relative to the caller's working directory. They used to hard-code
# /home/user/ame-next/..., so both self-tests failed in any other checkout.
#
# Run by ctest:
#   cmake -DMEMORY_EXE=... -DBISCUIT_EXE=... -P run_selftest_paths.cmake
#
# Headless: force SDL's dummy drivers so this works on build machines and CI.

set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_AUDIODRIVER} dummy)

set(dir "${CMAKE_CURRENT_BINARY_DIR}/selftest_paths")
file(REMOVE_RECURSE "${dir}")
file(MAKE_DIRECTORY "${dir}")

macro(expect_bmp exe default_name)
    if(NOT EXISTS "${exe}")
        message(FATAL_ERROR "missing executable: ${exe}")
    endif()
    execute_process(
        COMMAND "${exe}" --selftest
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE res
        OUTPUT_VARIABLE out_txt
        ERROR_VARIABLE err_txt)
    if(NOT res EQUAL 0)
        message(FATAL_ERROR
            "${exe} --selftest exited ${res} in a plain working directory.\n"
            "stdout: ${out_txt}\nstderr: ${err_txt}")
    endif()
    if(NOT EXISTS "${dir}/${default_name}")
        message(FATAL_ERROR
            "${exe} --selftest did not write ${default_name} into the working\n"
            "directory (${dir}).\nstdout: ${out_txt}\nstderr: ${err_txt}")
    endif()
    file(SIZE "${dir}/${default_name}" nbytes)
    if(nbytes LESS 1024)
        message(FATAL_ERROR "${default_name} looks empty (${nbytes} bytes)")
    endif()
    message(STATUS "${default_name}: ok (${nbytes} bytes, cwd-relative)")
endmacro()

macro(expect_bmp_at exe path)
    execute_process(
        COMMAND "${exe}" --selftest "${path}"
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE res
        OUTPUT_VARIABLE out_txt
        ERROR_VARIABLE err_txt)
    if(NOT res EQUAL 0)
        message(FATAL_ERROR
            "${exe} --selftest <path> exited ${res}.\n"
            "stdout: ${out_txt}\nstderr: ${err_txt}")
    endif()
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "${exe} --selftest <path> did not write ${path}")
    endif()
endmacro()

expect_bmp("${MEMORY_EXE}" "preview.bmp")
expect_bmp("${BISCUIT_EXE}" "biscuit.bmp")

# An explicit path is honoured: a custom name in the working directory, and a
# path outside it. (Parent directories must exist — the writers do not mkdir.)
expect_bmp_at("${MEMORY_EXE}" "${dir}/named.bmp")
expect_bmp_at("${BISCUIT_EXE}" "${CMAKE_CURRENT_BINARY_DIR}/biscuit_outside.bmp")

message(STATUS "test_selftest_paths ok — no hard-coded output directories")
