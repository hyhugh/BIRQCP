
function(CreateComponent)
    set(oneValueArgs TARGET MANIFEST)
    set(multiValueArgs C_FILES)
    cmake_parse_arguments(CREATE_COMPONENT "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if (NOT "${CREATE_COMPONENT_UNPARSED_ARGUMENTS}" STREQUAL "")
        message(FATAL_ERROR "Unknown arguments to CreateComponent ${CREATE_COMPONENT_UNPARSED_ARGUMENTS}")
    endif()

    foreach(item ${CREATE_COMPONENT_C_FILES})
        list(APPEND pp_cspaces ${CMAKE_CURRENT_BINARY_DIR}/cspace_${item})
    endforeach()

    foreach(item ${CREATE_COMPONENT_C_FILES})
        get_filename_component(name ${item} NAME)
        strip_last_extension(name ${name})
        list(APPEND pp_elfs "${name}")
    endforeach()

    cdl_pp(${CREATE_COMPONENT_MANIFEST} ${CREATE_COMPONENT_TARGET}_pp_target
        ELF ${pp_elfs}
        CFILE ${pp_cspaces}
        )

    list(LENGTH pp_elfs len)
    math(EXPR len "${len} - 1")

    foreach(i RANGE ${len})
        list(GET pp_elfs ${i} elf)
        list(GET pp_cspaces ${i} cspace)
        list(GET CREATE_COMPONENT_C_FILES ${i} cfile)

        set_property(SOURCE ${cspace} PROPERTY GENERATED 1)
        add_executable(${elf} EXCLUDE_FROM_ALL ${cfile} ${cspace})
        add_dependencies(${elf} ${CREATE_COMPONENT_TARGET}_pp_target)

        target_link_libraries(${elf} PRIVATE sel4tutorials)

        list(APPEND elf_files "$<TARGET_FILE:${elf}>")
        list(APPEND elf_targets "${elf}")
    endforeach()

    cdl_ld(${CMAKE_CURRENT_BINARY_DIR}/${CREATE_COMPONENT_TARGET}_spec.cdl ${CREATE_COMPONENT_TARGET}_capdl_spec
        MANIFESTS ${CREATE_COMPONENT_MANIFEST}
        ELF ${elf_files}
        DEPENDS ${elf_targets}
        )

    CapDLToolCFileGen(${CREATE_COMPONENT_TARGET}_cspec ${CREATE_COMPONENT_TARGET}_cspec.c ${CMAKE_CURRENT_BINARY_DIR}/${CREATE_COMPONENT_TARGET}_spec.cdl "${CAPDL_TOOL_BINARY}"
        MAX_IRQS ${CapDLLoaderMaxIRQs}
        DEPENDS ${${CREATE_COMPONENT_TARGET}_capdl_spec} install_capdl_tool "${CAPDL_TOOL_BINARY}")

    BuildCapDLApplication(
        C_SPEC ${CREATE_COMPONENT_TARGET}_cspec.c
        ELF ${elf_files}
        DEPENDS ${elf_targets} ${CREATE_COMPONENT_TARGET}_cspec
        OUTPUT ${CREATE_COMPONENT_TARGET}
        )

    SetSeL4Start(${CREATE_COMPONENT_TARGET})
endfunction(CreateComponent)


macro(strip_last_extension OUTPUT_VAR FILENAME)
    #from http://stackoverflow.com/questions/30049180/strip-filename-shortest-extension-by-cmake-get-filename-removing-the-last-ext
    string(REGEX REPLACE "\\.[^.]*$" "" ${OUTPUT_VAR} ${FILENAME})
endmacro()


