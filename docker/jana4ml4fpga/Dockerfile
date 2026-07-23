# JANA4ML4FPGA image: ROOT + patched JANA2 (master) + this project.
#
# Replaces docker/ml4fpga, docker/ml4fpga-pre, docker/ml4fpga-conda (Ubuntu 22.04
# + edpm + JANA2 v2.1.2, kept only for reference/older branches). JANA4ML4FPGA now
# targets JANA2 master (cmake/fetch_dependencies.cmake) with C++20, so this image
# is built eic-base/eic-full style: explicit from-source CMake stages, no edpm.
# See github.com/eicdev-containers eic-base/eic-full for the sibling images this
# mirrors.
#
# Build (repo root as context, so COPY below can see cmake/, src/, docker/):
#   docker build -f docker/jana4ml4fpga/Dockerfile -t eicdev/jana4ml4fpga:latest .
#   docker build -f docker/jana4ml4fpga/Dockerfile -t eicdev/jana4ml4fpga:latest --build-arg BUILD_THREADS=24 .
#   docker buildx build -f docker/jana4ml4fpga/Dockerfile --tag eicdev/jana4ml4fpga:latest --push --build-arg BUILD_THREADS=24 .
# Requires eicdev/eic-base:latest (ROOT, fmt, spdlog, clang-18 toolchain) --
# see https://github.com/<org>/eicdev-containers/eic-base -- pulled or built locally.
#
# Run:
#   docker run --rm -it --init eicdev/jana4ml4fpga:latest
#   docker run --rm -it --init eicdev/jana4ml4fpga:latest \
#       jana4ml4fpga -Pplugins=CDAQfileMT,flat_tree,root_output some_run.evio

FROM eicdev/eic-base:latest

ARG BUILD_THREADS=8
ARG CXX_STANDARD=20

WORKDIR ${APP_ROOT}


### ---------------------------------------------------------------------- ###
###          JANA2 build (master, ClearOutputs patch applied)              ###
### ---------------------------------------------------------------------- ###
# eic-base already ships a stock JANA2 (+podio +root +zmq) for the wider EIC
# stack, but JANA4ML4FPGA needs its own build:
#  - flags matching cmake/fetch_dependencies.cmake exactly (-DUSE_PODIO=Off
#    -DUSE_ZEROMQ=Off -DUSE_PYTHON=Off), so find_package(JANA) below picks
#    this build up and the project's own CMake fetch step is a no-op.
#  - the ClearOutputs patch: moves JEvent::Clear() (per-event object
#    recycling - thousands of deallocations for raw EVIO events) out from
#    under the JExecutionEngine mutex, which otherwise serializes every
#    worker through recycling. Not yet upstream; measured ~2x readout
#    throughput (10.2 kHz -> 21.1 kHz @ 24 threads). See
#    ai_rework/reports/phase2_jana2_migration.md and ai_rework/notes/DECISIONS.md #18.
#  - installed to its own prefix, placed first on CMAKE_PREFIX_PATH so it
#    takes priority over eic-base's stock JANA2.
ARG VERSION_JANA2=f815961e
ENV JANA2_SRC_PATH=${APP_ROOT}/jana4ml4fpga-jana2-src \
    JANA2_BLD_PATH=${APP_ROOT}/jana4ml4fpga-jana2-build \
    JANA2_INS_PATH=${APP_ROOT}/jana4ml4fpga-jana2

# NOTE: build context MUST be the repo root (see header) so the COPY of the
# project sources below works; the patch therefore needs its full repo path here.
# Canonical patch lives in cmake/patches/ and is the SAME one that
# cmake/fetch_dependencies.cmake applies for non-docker builds (install_software,
# plain cmake) - one source of truth for all build paths.
COPY cmake/patches/jana2-master-clearoutputs.patch /tmp/jana2-master-clearoutputs.patch

RUN set -ex && \
    git clone https://github.com/JeffersonLab/JANA2.git "${JANA2_SRC_PATH}" && \
    cd "${JANA2_SRC_PATH}" && git checkout ${VERSION_JANA2} && \
    git apply /tmp/jana2-master-clearoutputs.patch && \
    mkdir -p "${JANA2_BLD_PATH}" && cd "${JANA2_BLD_PATH}" && \
    cmake \
      -DCMAKE_INSTALL_PREFIX="${JANA2_INS_PATH}" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_STANDARD=${CXX_STANDARD} \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DUSE_ROOT=On \
      -DUSE_PODIO=Off \
      -DUSE_ZEROMQ=Off \
      -DUSE_PYTHON=Off \
      "${JANA2_SRC_PATH}" && \
    cmake --build . --target install -j"${BUILD_THREADS}" && \
    rm -rf "${JANA2_BLD_PATH}" "${JANA2_SRC_PATH}" /tmp/jana2-master-clearoutputs.patch

ENV JANA_HOME=${JANA2_INS_PATH} \
    JANA_PLUGIN_PATH="${JANA2_INS_PATH}/plugins" \
    PATH="${JANA2_INS_PATH}/bin:${PATH}" \
    LD_LIBRARY_PATH="${JANA2_INS_PATH}/lib:${LD_LIBRARY_PATH}" \
    CMAKE_PREFIX_PATH="${JANA2_INS_PATH}/lib/cmake/JANA:${CMAKE_PREFIX_PATH}"

# Allocator tuning required to realize the ClearOutputs gain: un-serializing the
# ~2k per-event frees exposes glibc malloc cross-thread contention otherwise
# (ai_rework/notes/DECISIONS.md #14).
ENV GLIBC_TUNABLES=glibc.malloc.tcache_count=4096:glibc.malloc.arena_max=96


### ---------------------------------------------------------------------- ###
###          JANA4ML4FPGA build                                            ###
### ---------------------------------------------------------------------- ###
ENV JANA4ML4FPGA_SRC_PATH=${APP_ROOT}/jana4ml4fpga-src \
    JANA4ML4FPGA_INS_PATH=${APP_ROOT}/jana4ml4fpga

COPY . ${JANA4ML4FPGA_SRC_PATH}

RUN set -ex && \
    cmake -S "${JANA4ML4FPGA_SRC_PATH}" -B "${JANA4ML4FPGA_SRC_PATH}/build" \
      -DCMAKE_INSTALL_PREFIX="${JANA4ML4FPGA_INS_PATH}" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_STANDARD=${CXX_STANDARD} && \
    cmake --build "${JANA4ML4FPGA_SRC_PATH}/build" --target install -j"${BUILD_THREADS}" && \
    rm -rf "${JANA4ML4FPGA_SRC_PATH}/build"

ENV JANA_PLUGIN_PATH="${JANA4ML4FPGA_INS_PATH}/plugins:${JANA_PLUGIN_PATH}" \
    PATH="${JANA4ML4FPGA_INS_PATH}/bin:${PATH}" \
    LD_LIBRARY_PATH="${JANA4ML4FPGA_INS_PATH}/lib:${LD_LIBRARY_PATH}"

WORKDIR ${APP_ROOT}

SHELL ["/bin/bash", "-c"]
