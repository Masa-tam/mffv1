function(mffv1_add_fuzzer target_name source_file)
    add_executable(${target_name}
        fuzz_main.cpp
        ${source_file}
    )
    target_link_libraries(${target_name}
        PRIVATE
            mffv1::mffv1
    )
    mffv1_apply_common_options(${target_name})
    if(BUILD_TESTING)
        add_test(NAME ${target_name}_empty_input COMMAND ${target_name})
        set_tests_properties(${target_name}_empty_input PROPERTIES LABELS fuzz)
    endif()
endfunction()

mffv1_add_fuzzer(mffv1_fuzz_configuration_record configuration_record_fuzzer.cpp)
mffv1_add_fuzzer(mffv1_fuzz_frame_decode frame_decode_fuzzer.cpp)
mffv1_add_fuzzer(mffv1_fuzz_encoder encoder_fuzzer.cpp)
