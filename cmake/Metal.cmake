# SPDX-License-Identifier: GPL-2.0-or-later
# Included only by the macOS PB_METAL_ONLY branch. No guest/runtime targets are added here.
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
find_library(PB_METAL_FRAMEWORK Metal REQUIRED)
find_library(PB_FOUNDATION_FRAMEWORK Foundation REQUIRED)
# Emit an empty registry when the smoke option is OFF, including after reconfiguration.
enable_testing()

add_library(pbmetal STATIC runtime/graphics_metal.mm)
target_include_directories(pbmetal PUBLIC ${PROJECT_SOURCE_DIR}/runtime/include)
target_compile_options(pbmetal PRIVATE -fobjc-arc -fobjc-exceptions -fblocks -fno-modules)
target_link_libraries(pbmetal PUBLIC ${PB_METAL_FRAMEWORK} ${PB_FOUNDATION_FRAMEWORK})

if(PB_ENABLE_METAL_SMOKE_TEST)
    add_executable(test_metal_offscreen test/test_metal_offscreen.mm)
    target_compile_options(test_metal_offscreen PRIVATE -fobjc-arc -fobjc-exceptions -fblocks -fno-modules)
    target_link_libraries(test_metal_offscreen PRIVATE pbmetal)
    add_test(NAME metal_offscreen COMMAND test_metal_offscreen)
    set_tests_properties(metal_offscreen PROPERTIES TIMEOUT 30 LABELS "gpu;metal")
endif()
