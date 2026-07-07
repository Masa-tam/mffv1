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

function(mffv1_public_tree_reject file reason)
    message(FATAL_ERROR
        "tracked file is not part of the intended public tree: "
        "${file} (${reason})"
    )
endfunction()

set(mffv1_public_tree_allowed_testvector_files
    testvectors/.gitignore
    testvectors/README.md
)

set(mffv1_public_tree_forbidden_path_patterns
    "^\\.agents/"
    "^\\.codex/"
    "^build/"
    "^out/"
    "^private/"
    "^Testing/"
    "^.*createVector\\.zip$"
)

foreach(mffv1_public_tree_file IN LISTS mffv1_public_tree_files)
    if(mffv1_public_tree_file MATCHES "^testvectors/"
       AND NOT mffv1_public_tree_file IN_LIST mffv1_public_tree_allowed_testvector_files)
        mffv1_public_tree_reject(
            "${mffv1_public_tree_file}"
            "testvectors only allows tracked README.md and .gitignore files"
        )
    endif()

    foreach(mffv1_public_tree_pattern IN LISTS mffv1_public_tree_forbidden_path_patterns)
        if(mffv1_public_tree_file MATCHES "${mffv1_public_tree_pattern}")
            mffv1_public_tree_reject(
                "${mffv1_public_tree_file}"
                "matched forbidden path pattern ${mffv1_public_tree_pattern}"
            )
        endif()
    endforeach()
endforeach()
