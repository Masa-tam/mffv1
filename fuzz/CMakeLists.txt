function(mffv1_add_fuzzer target_name source_file seed_file)
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
        add_test(
            NAME ${target_name}_seed_input
            COMMAND ${target_name} "${CMAKE_CURRENT_SOURCE_DIR}/${seed_file}"
        )
        set_tests_properties(${target_name}_seed_input PROPERTIES LABELS fuzz)
    endif()
endfunction()

mffv1_add_fuzzer(
    mffv1_fuzz_configuration_record
    configuration_record_fuzzer.cpp
    corpus/configuration_record/ascii_seed.bin
)
mffv1_add_fuzzer(
    mffv1_fuzz_frame_decode
    frame_decode_fuzzer.cpp
    corpus/frame_decode/ascii_seed.bin
)
mffv1_add_fuzzer(
    mffv1_fuzz_encoder
    encoder_fuzzer.cpp
    corpus/encoder/ascii_seed.bin
)
