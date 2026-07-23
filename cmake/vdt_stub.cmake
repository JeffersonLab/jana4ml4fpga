# Some ROOT installations (e.g. Arch/CachyOS system package) are built with vdt
# support, but the vdt package itself (cern-vdt) may not be installed. In that case
# ROOTConfig.cmake fails on find_dependency(Vdt). Neither this project nor JANA2
# uses vdt (only ROOT::ROOTVecOps interface-links it), so if vdt headers are absent,
# provide an empty stub target to satisfy ROOTConfig.
#
# The proper fix is to install vdt (e.g. `pacman -S cern-vdt`, `apt install libvdt-dev`).
#
# This file is also injected into the JANA2 configure step via CMAKE_PROJECT_INCLUDE
# (see fetch_dependencies.cmake), since JANA2 calls find_package(ROOT) too.
if(NOT TARGET VDT::VDT AND NOT EXISTS /usr/include/vdt/vdtMath.h)
    add_library(VDT::VDT INTERFACE IMPORTED)
endif()
