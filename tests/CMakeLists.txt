# tests/CMakeLists.txt
# Test suite — tests added here as each module is implemented.

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# add_subdirectory(test_audio_buffer)
# add_subdirectory(test_stft_processor)
# add_subdirectory(test_equalizer))