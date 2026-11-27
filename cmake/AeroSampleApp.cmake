set(_AERO_SAMPLE_APP_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(aero_add_sample_app TARGET_NAME)
    set(oneValueArgs SAMPLE_DIR TITLE WIDTH HEIGHT OUTPUT_DIR PROFILE)
    set(multiValueArgs EXTRA_ASSET_DIRS)
    cmake_parse_arguments(AERO_SAMPLE "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT AERO_SAMPLE_PROFILE)
        set(AERO_SAMPLE_PROFILE "DEV_LIVE")
    endif()

    if(NOT AERO_SAMPLE_SAMPLE_DIR)
        message(FATAL_ERROR "aero_add_sample_app(${TARGET_NAME}): SAMPLE_DIR is required")
    endif()
    if(NOT AERO_SAMPLE_TITLE)
        set(AERO_SAMPLE_TITLE "${TARGET_NAME}")
    endif()
    if(NOT AERO_SAMPLE_WIDTH)
        set(AERO_SAMPLE_WIDTH 1000)
    endif()
    if(NOT AERO_SAMPLE_HEIGHT)
        set(AERO_SAMPLE_HEIGHT 640)
    endif()
    if(NOT AERO_SAMPLE_OUTPUT_DIR)
        set(AERO_SAMPLE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/samples/${TARGET_NAME}")
    endif()

    set(_aero_launcher_src "${_AERO_SAMPLE_APP_CMAKE_DIR}/../tools/aero_sample_launcher/main.cpp")
    if(NOT EXISTS "${_aero_launcher_src}")
        set(_aero_launcher_src "${_AERO_SAMPLE_APP_CMAKE_DIR}/../../../share/aerogui2/aero_sample_launcher_main.cpp")
    endif()
    if(NOT EXISTS "${_aero_launcher_src}")
        message(FATAL_ERROR "aero_add_sample_app(${TARGET_NAME}): unable to locate aero_sample_launcher main.cpp from ${_AERO_SAMPLE_APP_CMAKE_DIR}")
    endif()

    add_executable(${TARGET_NAME} "${_aero_launcher_src}")
    if(AERO_SAMPLE_PROFILE STREQUAL "RELEASE_SLIM_JIT" AND TARGET aero-compiler AND TARGET AeroGUI::AppRuntime)
        target_link_libraries(${TARGET_NAME} PRIVATE AeroGUI::AppRuntime)
        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /GR-)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE -fno-rtti)
        endif()
    else()
        target_link_libraries(${TARGET_NAME} PRIVATE AeroGUI::App)
        if(MSVC)
            target_compile_options(${TARGET_NAME} PRIVATE /utf-8)
        endif()
    endif()
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE psapi)
    endif()
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        AERO_SAMPLE_APP_TITLE="${AERO_SAMPLE_TITLE}"
        AERO_SAMPLE_APP_WIDTH=${AERO_SAMPLE_WIDTH}u
        AERO_SAMPLE_APP_HEIGHT=${AERO_SAMPLE_HEIGHT}u
    )
    set_target_properties(${TARGET_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${AERO_SAMPLE_OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${AERO_SAMPLE_OUTPUT_DIR}/Release"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${AERO_SAMPLE_OUTPUT_DIR}/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${AERO_SAMPLE_OUTPUT_DIR}/RelWithDebInfo"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${AERO_SAMPLE_OUTPUT_DIR}/MinSizeRel"
    )

    if(AERO_SAMPLE_PROFILE STREQUAL "RELEASE_SLIM_JIT" AND TARGET aero-compiler AND TARGET AeroGUI::AppRuntime)
        set(_aero_arbc_name "${TARGET_NAME}.arbc")
        set(_aero_arbc_path "${CMAKE_CURRENT_BINARY_DIR}/generated/${_aero_arbc_name}")
        target_compile_definitions(${TARGET_NAME} PRIVATE
            AERO_SAMPLE_PRECOMPILED_ARBC="${_aero_arbc_name}"
        )
        file(GLOB_RECURSE _aero_sample_sources CONFIGURE_DEPENDS
            "${AERO_SAMPLE_SAMPLE_DIR}/*.xaml"
            "${AERO_SAMPLE_SAMPLE_DIR}/*.rs"
            "${AERO_SAMPLE_SAMPLE_DIR}/*.cs"
        )
        add_custom_command(
            OUTPUT "${_aero_arbc_path}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
            COMMAND $<TARGET_FILE:aero-compiler>
                --sample-dir "${AERO_SAMPLE_SAMPLE_DIR}"
                --compiled-xaml-catalog
                --O2
                --out-arbc "${_aero_arbc_path}"
            DEPENDS aero-compiler ${_aero_sample_sources}
            COMMENT "Compiling slim bytecode bundle (${_aero_arbc_name}) for ${TARGET_NAME}"
            VERBATIM
        )
        add_custom_target(${TARGET_NAME}_arbc DEPENDS "${_aero_arbc_path}")
        add_dependencies(${TARGET_NAME} ${TARGET_NAME}_arbc)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_aero_arbc_path}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_aero_arbc_name}"
            COMMENT "Staging ${_aero_arbc_name} next to ${TARGET_NAME}"
        )
    elseif(TARGET AeroGUI::Stdlib)
        add_dependencies(${TARGET_NAME} AeroGUI::Stdlib)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:AeroGUI::Stdlib>"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>"
            COMMENT "Staging aerogui_stdlib next to ${TARGET_NAME}"
        )
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${AERO_SAMPLE_SAMPLE_DIR}"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMENT "Staging sample directory for ${TARGET_NAME}"
    )

    foreach(_extra_spec IN LISTS AERO_SAMPLE_EXTRA_ASSET_DIRS)
        # Split cleanly at the LAST ':' so Windows drive letters like C:/... work properly.
        string(REGEX MATCH "^(.+):([^:]+)$" _matched "${_extra_spec}")
        if(CMAKE_MATCH_1 AND CMAKE_MATCH_2)
            set(_extra_src "${CMAKE_MATCH_1}")
            set(_extra_dst "${CMAKE_MATCH_2}")
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_extra_dst}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_extra_src}"
                    "$<TARGET_FILE_DIR:${TARGET_NAME}>/${_extra_dst}"
                COMMENT "Staging extra assets ${_extra_dst} for ${TARGET_NAME}"
            )
        else()
            message(FATAL_ERROR "aero_add_sample_app(${TARGET_NAME}): invalid EXTRA_ASSET_DIRS entry '${_extra_spec}' (expected '<src>:<dst_rel>')")
        endif()
    endforeach()
endfunction()
