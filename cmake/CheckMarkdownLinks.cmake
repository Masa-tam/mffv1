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

if(NOT DEFINED MFFV1_MARKDOWN_LINK_EXCLUDE_DIRS)
    set(MFFV1_MARKDOWN_LINK_EXCLUDE_DIRS
        .agents
        .codex
        .git
        build
        third-party
    )
endif()

set(mffv1_markdown_link_exclude_paths)
foreach(mffv1_markdown_link_exclude_dir IN LISTS MFFV1_MARKDOWN_LINK_EXCLUDE_DIRS)
    get_filename_component(
        mffv1_markdown_link_exclude_path
        "${MFFV1_MARKDOWN_LINK_ROOT}/${mffv1_markdown_link_exclude_dir}"
        ABSOLUTE
    )
    list(APPEND mffv1_markdown_link_exclude_paths
        "${mffv1_markdown_link_exclude_path}"
    )
endforeach()

foreach(mffv1_markdown_link_file IN LISTS mffv1_markdown_link_files)
    set(mffv1_markdown_link_excluded FALSE)
    foreach(mffv1_markdown_link_exclude_path IN LISTS mffv1_markdown_link_exclude_paths)
        cmake_path(IS_PREFIX
            mffv1_markdown_link_exclude_path
            "${mffv1_markdown_link_file}"
            NORMALIZE
            mffv1_markdown_link_is_excluded
        )
        if(mffv1_markdown_link_is_excluded)
            set(mffv1_markdown_link_excluded TRUE)
            break()
        endif()
    endforeach()

    if(mffv1_markdown_link_excluded)
        continue()
    endif()

    file(READ "${mffv1_markdown_link_file}" mffv1_markdown_link_content)
    string(REGEX REPLACE "!\\[[^]]*\\]\\([^)]+\\)" "image"
        mffv1_markdown_link_content
        "${mffv1_markdown_link_content}"
    )
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
