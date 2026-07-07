get_filename_component(
    MFFV1_PUBLIC_TREE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

if(DEFINED MFFV1_PUBLIC_TREE_ROOT_OVERRIDE)
    get_filename_component(
        MFFV1_PUBLIC_TREE_ROOT
        "${MFFV1_PUBLIC_TREE_ROOT_OVERRIDE}"
        ABSOLUTE
    )
endif()

execute_process(
    COMMAND git -C "${MFFV1_PUBLIC_TREE_ROOT}" ls-files
    RESULT_VARIABLE mffv1_public_tree_git_result
    OUTPUT_VARIABLE mffv1_public_tree_files_output
    ERROR_VARIABLE mffv1_public_tree_git_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT mffv1_public_tree_git_result EQUAL 0)
    message(FATAL_ERROR
        "git ls-files failed while checking public tracked files: "
        "${mffv1_public_tree_git_error}"
    )
endif()

string(REPLACE "\n" ";" mffv1_public_tree_files
    "${mffv1_public_tree_files_output}"
)

set(mffv1_public_tree_forbidden_path_patterns
    "^\\.agents/"
    "^\\.codex/"
    "^build/"
    "^out/"
    "^private/"
    "^Testing/"
    "^testvectors/createVector\\.zip$"
    "^testvectors/test_vector_data\\.hpp$"
    "^testvectors/.*\\.(avi|mkv|mov|mp4|bin|zip|7z|tar|gz|xz|dll|exe|lib|pdb)$"
    "^.*createVector\\.zip$"
)

foreach(mffv1_public_tree_file IN LISTS mffv1_public_tree_files)
    foreach(mffv1_public_tree_pattern IN LISTS mffv1_public_tree_forbidden_path_patterns)
        if(mffv1_public_tree_file MATCHES "${mffv1_public_tree_pattern}")
            message(FATAL_ERROR
                "tracked file is not part of the intended public tree: "
                "${mffv1_public_tree_file}"
            )
        endif()
    endforeach()
endforeach()
