# SPDX-License-Identifier: GPL-2.0-or-later
# Add Metal to macOS builds; PB_METAL_ONLY controls the caller's other targets.
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)
find_library(PB_METAL_FRAMEWORK Metal REQUIRED)
find_library(PB_FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(PB_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
# Emit an empty registry when both test options are OFF, including after reconfiguration.
enable_testing()

add_library(pbmetal STATIC runtime/graphics_metal.mm)
target_include_directories(pbmetal PUBLIC ${PROJECT_SOURCE_DIR}/runtime/include)
target_compile_options(pbmetal PRIVATE -fobjc-arc -fobjc-exceptions -fblocks -fno-modules)
target_link_libraries(pbmetal PUBLIC ${PB_METAL_FRAMEWORK} ${PB_FOUNDATION_FRAMEWORK} ${PB_QUARTZCORE_FRAMEWORK})

if(PB_ENABLE_METAL_SMOKE_TEST)
    foreach(unit IN ITEMS metal_offscreen metal_upload)
        add_executable(test_${unit} test/test_${unit}.mm)
        target_compile_options(test_${unit} PRIVATE -fobjc-arc -fobjc-exceptions -fblocks -fno-modules)
        target_link_libraries(test_${unit} PRIVATE pbmetal)
        add_test(NAME ${unit} COMMAND test_${unit})
        set_tests_properties(${unit} PROPERTIES TIMEOUT 30 LABELS "gpu;metal")
    endforeach()
endif()

if(PB_ENABLE_METAL_PRESENTATION_TEST)
    find_library(PB_APPKIT_FRAMEWORK AppKit REQUIRED)
    add_executable(test_metal_presentation test/test_metal_presentation.mm)
    target_compile_options(test_metal_presentation PRIVATE -fobjc-arc -fobjc-exceptions -fblocks -fno-modules)
    target_link_libraries(test_metal_presentation PRIVATE pbmetal ${PB_APPKIT_FRAMEWORK})
    add_test(NAME metal_presentation COMMAND test_metal_presentation)
    set_tests_properties(metal_presentation PROPERTIES TIMEOUT 30 LABELS "gpu;metal;window")
endif()

if(PB_ENABLE_METAL_EXECUTOR)
    include(${PROJECT_SOURCE_DIR}/cmake/MetalExecutor.cmake)
endif()
