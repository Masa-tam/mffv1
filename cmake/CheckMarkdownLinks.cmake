if(NOT DEFINED MFFV1_MARKDOWN_LINK_ROOT)
    message(FATAL_ERROR "Set MFFV1_MARKDOWN_LINK_ROOT to the documentation root")
endif()

get_filename_component(
    MFFV1_MARKDOWN_LINK_ROOT
    "${MFFV1_MARKDOWN_LINK_ROOT}"
    ABSOLUTE
)

if(NOT IS_DIRECTORY "${MFFV1_MARKDOWN_LINK_ROOT}")
    message(FATAL_ERROR
        "Markdown link root does not exist: ${MFFV1_MARKDOWN_LINK_ROOT}"
    )
endif()

file(GLOB_RECURSE mffv1_markdown_link_files
    "${MFFV1_MARKDOWN_LINK_ROOT}/*.md"
)

foreach(mffv1_markdown_link_file IN LISTS mffv1_markdown_link_files)
    file(READ "${mffv1_markdown_link_file}" mffv1_markdown_link_content)
    string(REGEX MATCHALL "\\[[^]]*\\]\\([^)]+\\)"
        mffv1_markdown_link_matches
        "${mffv1_markdown_link_content}"
    )

    get_filename_component(
        mffv1_markdown_link_base_dir
        "${mffv1_markdown_link_file}"
        DIRECTORY
    )

    foreach(mffv1_markdown_link_match IN LISTS mffv1_markdown_link_matches)
        string(REGEX REPLACE "^\\[[^]]*\\]\\(([^)]+)\\)$" "\\1"
            mffv1_markdown_link_target
            "${mffv1_markdown_link_match}"
        )
        string(REGEX REPLACE "^([^ \"']+).*$" "\\1"
            mffv1_markdown_link_target
            "${mffv1_markdown_link_target}"
        )

        if(mffv1_markdown_link_target MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:"
           OR mffv1_markdown_link_target MATCHES "^#"
           OR mffv1_markdown_link_target STREQUAL "")
            continue()
        endif()

        string(REGEX REPLACE "[?#].*$" ""
            mffv1_markdown_link_path
            "${mffv1_markdown_link_target}"
        )

        if(mffv1_markdown_link_path STREQUAL "")
            continue()
        endif()

        get_filename_component(
            mffv1_markdown_link_resolved
            "${mffv1_markdown_link_base_dir}/${mffv1_markdown_link_path}"
            ABSOLUTE
        )

        if(NOT EXISTS "${mffv1_markdown_link_resolved}")
            message(FATAL_ERROR
                "Broken Markdown link in ${mffv1_markdown_link_file}: "
                "${mffv1_markdown_link_target}"
            )
        endif()
    endforeach()
endforeach()
