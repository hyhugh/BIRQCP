#
# Copyright 2018, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#
ImportCapDL()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -u __vsyscall_ptr")
include(helpers.cmake)

# create the fake_roottask capdl-loader component,
# you will need to pass the manifest and all the c sources into this function
# note that this manifest is the manifest for the fake_roottask
CreateComponent(
    TARGET roottask
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/manifest_rt.py"
    C_FILES "client.c" "timer.c"
    )

# standard cdl_pp, note that the manifest here is the manifest written by you
cdl_pp(${CMAKE_CURRENT_SOURCE_DIR}/manifest.py cdl_pp_target
    ELF "provider"
    CFILE "${CMAKE_CURRENT_BINARY_DIR}/cspace_provider.c"

    ELF "roottask"
    CFILE "${CMAKE_CURRENT_BINARY_DIR}/cspace_roottask.c"
)

add_executable(provider EXCLUDE_FROM_ALL provider.c cspace_provider.c)
add_dependencies(provider cdl_pp_target)
target_link_libraries(provider sel4tutorials)
list(APPEND elf_files "$<TARGET_FILE:provider>")
list(APPEND elf_targets "provider")

add_dependencies(roottask cdl_pp_target)
target_link_libraries(roottask sel4tutorials)
# here we add the cspace file to the source of the fake_roottask
target_sources(roottask PRIVATE cspace_roottask.c)
list(APPEND elf_files "$<TARGET_FILE:roottask>")
list(APPEND elf_targets "roottask")

cdl_ld("${CMAKE_CURRENT_BINARY_DIR}/spec.cdl" capdl_spec
    MANIFESTS ${CMAKE_CURRENT_SOURCE_DIR}/manifest.py
    ELF ${elf_files}
    DEPENDS ${elf_targets})

DeclareCDLRootImage("${CMAKE_CURRENT_BINARY_DIR}/spec.cdl" capdl_spec ELF ${elf_files} ELF_DEPENDS ${elf_targets})

