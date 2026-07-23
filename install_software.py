#!/usr/bin/env python3
"""
JANA4ML4FPGA installer: bootstraps a self-contained Miniforge environment with
the build dependencies (ROOT, fmt, spdlog, cmake, compilers, git), then builds
and installs the project.

Miniforge is used on purpose: it is the community, 100%-conda-forge installer
downloaded from GitHub - no anaconda.com/`defaults` channel, which is blocked at
JLab. JANA2 is NOT installed here; the project fetches and builds it itself (see
cmake/fetch_dependencies.cmake), so this script only has to provide ROOT + a
C++20 toolchain and then run cmake.

Usage:
    python3 install_software.py                 # all steps
    python3 install_software.py -s setup_conda  # a single step
    python3 install_software.py -s build_soft   # rebuild only
    python3 install_software.py --clear         # remove everything it installed

Environment variables:
    ML4FPGA_TOP_DIR      install location (conda + generated scripts).
                         Default: this script's directory.
    ML4FPGA_SOURCE_DIR   project checkout to build. Default: this script's dir if
                         it contains CMakeLists.txt, else $TOP_DIR/JANA4ML4FPGA
                         (git-cloned on first build).
    ML4FPGA_BRANCH       branch to clone when the source is fetched. Default: main.
    ML4FPGA_REPO_URL     repo to clone. Default: JeffersonLab/jana4ml4fpga.

Steps (default order): gen_scripts, install_conda, setup_conda, build_soft
"""

import argparse
import os
import platform
import shlex
import shutil
import subprocess
from collections import OrderedDict
from datetime import datetime
from os import path

CONDA_ENV_NAME = "ml4fpga"
ENV_NAME_TOP_DIR = "ML4FPGA_TOP_DIR"
INSTALL_SCRIPTS_DIR_NAME = "install_scripts"

# Conda-forge build dependencies. Deliberately minimal: the active project links
# only ROOT (fmt/spdlog are otherwise auto-fetched); JANA2 is built by the
# project. Add analysis packages (uproot, pandas, ...) to your env by hand if you
# want the python/ notebooks.
CONDA_PACKAGES = "root fmt spdlog cmake make gcc gxx git"


class InstallInfo:
    """All paths derived from the top directory. One place to change layout."""

    def __init__(self):
        self.this_script_dir = path.dirname(path.abspath(__file__))
        self.top_dir = os.environ.get(ENV_NAME_TOP_DIR, self.this_script_dir)
        os.environ[ENV_NAME_TOP_DIR] = self.top_dir

        self.conda_dir = path.join(self.top_dir, "miniforge")
        self.conda_env_name = CONDA_ENV_NAME
        self.conda_env_dir = path.join(self.conda_dir, "envs", self.conda_env_name)

        # Project source to build. Resolution priority:
        #   1. $ML4FPGA_SOURCE_DIR                       (explicit override)
        #   2. this script's directory, if it is a checkout (has CMakeLists.txt)
        #   3. $TOP_DIR/JANA4ML4FPGA                     (git-cloned if missing)
        # The old script relied on `edpm install jana4ml4fpga` to clone the source;
        # since edpm is gone, build_software.sh clones it here when needed.
        self.repo_url = os.environ.get(
            "ML4FPGA_REPO_URL", "https://github.com/JeffersonLab/jana4ml4fpga.git")
        self.branch = os.environ.get("ML4FPGA_BRANCH", "main")
        self.source_override = os.environ.get("ML4FPGA_SOURCE_DIR")
        self.auto_clone_dir = path.join(self.top_dir, "jana4ml4fpga")
        source_dir = self.source_override
        if not source_dir:
            if path.isfile(path.join(self.this_script_dir, "CMakeLists.txt")):
                source_dir = self.this_script_dir  # running from inside the repo
            else:
                source_dir = self.auto_clone_dir  # will be cloned
        self.source_dir = source_dir
        self.build_dir = path.join(self.source_dir, "build")
        self.install_dir = path.join(self.source_dir, "install")  # matches CMakeLists default

        self.scripts_dir = path.join(self.top_dir, INSTALL_SCRIPTS_DIR_NAME)
        self.script_setup_conda = path.join(self.scripts_dir, "setup_conda.sh")
        self.script_build_soft = path.join(self.scripts_dir, "build_software.sh")
        self.script_openssl_cnf = path.join(self.scripts_dir, "openssl.cnf")
        self.script_env_sh = path.join(self.top_dir, "setup_env.sh")
        self.script_env_csh = path.join(self.top_dir, "setup_env.csh")

    def asdict(self):
        # Only path-like string attributes are used by the templates' .format();
        # non-template attributes (source_override, auto_clone_dir) are harmless.
        return dict(self.__dict__)

    def print_self(self):
        print("\nInstall info:")
        for key, value in self.asdict().items():
            print(f"  -{key:<20} {value}")
        print()


install_info = InstallInfo()
install_info.print_self()


# ---------------------------------------------------------------------------#
#  Generated file contents                                                    #
# ---------------------------------------------------------------------------#

# conda-forge only (Miniforge already defaults to this; make it explicit + strict)
condarc_content = """\
channel_priority: strict
channels:
  - conda-forge
"""

# JLab's TLS-inspecting proxy rejects modern renegotiation; allow the legacy mode.
openssl_cnf_content = """\
openssl_conf = openssl_init

[openssl_init]
ssl_conf = ssl_sect

[ssl_sect]
system_default = system_default_sect

[system_default_sect]
Options = UnsafeLegacyRenegotiation
"""

# Allocator tuning required to realize the ClearOutputs speedup (~2x readout):
# un-serializing the ~2k per-event frees exposes glibc malloc cross-thread
# contention otherwise. Must be set in the *run* environment, not just at build
# time (see ai_rework/notes/DECISIONS.md #14). Kept in one place:
GLIBC_TUNABLES_VALUE = "glibc.malloc.tcache_count=4096:glibc.malloc.arena_max=96"

template_user_sh = """\
export {env_name_top_dir}={top_dir}

# Activate the conda environment
source $ML4FPGA_TOP_DIR/miniforge/etc/profile.d/conda.sh
conda activate {conda_env_name}

# JANA4ML4FPGA install: put its binaries, libraries and JANA plugins on the paths.
# The install is self-contained - fetch_dependencies.cmake installs the fetched
# JANA2 (libJANA.so, JANA plugins) into this prefix too - so no build-tree paths.
export ML4FPGA_INSTALL_DIR={install_dir}
export JANA_HOME=$ML4FPGA_INSTALL_DIR
export PATH="$ML4FPGA_INSTALL_DIR/bin${{PATH:+:${{PATH}}}}"
export LD_LIBRARY_PATH="$ML4FPGA_INSTALL_DIR/lib${{LD_LIBRARY_PATH:+:${{LD_LIBRARY_PATH}}}}"
export JANA_PLUGIN_PATH="$ML4FPGA_INSTALL_DIR/plugins${{JANA_PLUGIN_PATH:+:${{JANA_PLUGIN_PATH}}}}"
export CMAKE_PREFIX_PATH="$ML4FPGA_INSTALL_DIR${{CMAKE_PREFIX_PATH:+:${{CMAKE_PREFIX_PATH}}}}"

# glibc allocator tuning - required for the multithreaded readout speedup (~2x)
export GLIBC_TUNABLES={glibc_tunables}
""".format(env_name_top_dir=ENV_NAME_TOP_DIR, glibc_tunables=GLIBC_TUNABLES_VALUE,
           **install_info.asdict())

# NOTE for csh/tcsh (gluon): conda's own `conda activate` is unreliable in tcsh
# (the shell hook is effectively deprecated - sourcing conda.csh then
# `conda activate` just prints usage). So the csh script does NOT use conda
# activate; it puts the env's ROOT + toolchain on the paths directly, which is
# all the *run* environment needs. (The bash script keeps `conda activate`
# because build_software.sh needs the compiler's activate.d hooks.)
template_user_csh = """\
setenv {env_name_top_dir} {top_dir}

# ROOT + toolchain from the conda env (set directly - see note in install_software.py)
setenv CONDA_PREFIX {conda_env_dir}
setenv ROOTSYS {conda_env_dir}
if ( ! $?PATH )            setenv PATH ""
if ( ! $?LD_LIBRARY_PATH ) setenv LD_LIBRARY_PATH ""
if ( ! $?PYTHONPATH )      setenv PYTHONPATH ""
setenv PATH            {conda_env_dir}/bin:${{PATH}}
setenv LD_LIBRARY_PATH {conda_env_dir}/lib:${{LD_LIBRARY_PATH}}
setenv PYTHONPATH      {conda_env_dir}/lib:${{PYTHONPATH}}

# JANA4ML4FPGA install: put its binaries, libraries and JANA plugins on the paths.
# The install is self-contained - fetch_dependencies.cmake installs the fetched
# JANA2 (libJANA.so, JANA plugins) into this prefix too - so no build-tree paths.
setenv ML4FPGA_INSTALL_DIR {install_dir}
setenv JANA_HOME ${{ML4FPGA_INSTALL_DIR}}
if ( ! $?JANA_PLUGIN_PATH ) setenv JANA_PLUGIN_PATH ""
if ( ! $?CMAKE_PREFIX_PATH ) setenv CMAKE_PREFIX_PATH ""
setenv PATH             ${{ML4FPGA_INSTALL_DIR}}/bin:${{PATH}}
setenv LD_LIBRARY_PATH  ${{ML4FPGA_INSTALL_DIR}}/lib:${{LD_LIBRARY_PATH}}
setenv JANA_PLUGIN_PATH ${{ML4FPGA_INSTALL_DIR}}/plugins:${{JANA_PLUGIN_PATH}}
setenv CMAKE_PREFIX_PATH ${{ML4FPGA_INSTALL_DIR}}:${{CMAKE_PREFIX_PATH}}

# glibc allocator tuning - required for the multithreaded readout speedup (~2x)
setenv GLIBC_TUNABLES {glibc_tunables}
""".format(env_name_top_dir=ENV_NAME_TOP_DIR, glibc_tunables=GLIBC_TUNABLES_VALUE,
           **install_info.asdict())

template_setup_conda = """\
set -e
source {conda_dir}/etc/profile.d/conda.sh

# JLab proxy work-arounds (harmless elsewhere)
export PYTHONHTTPSVERIFY=0
export OPENSSL_CONF={script_openssl_cnf}
conda config --set ssl_verify false

# Miniforge ships mamba; use it - it is much faster at solving.
mamba create -y -n {conda_env_name} python=3.12
mamba install -y -n {conda_env_name} -c conda-forge {conda_packages}

echo "==========================================="
echo " C O N D A   I N S T A L L   S U C C E S S "
echo "==========================================="
""".format(conda_packages=CONDA_PACKAGES, **install_info.asdict())

template_build_soft = """\
set -e
echo ""
echo "================================"
echo "  B U I L D    P A C K A G E S  "
echo "================================"
echo ""

source {script_env_sh}

# Project source: {source_dir}
# Override with ML4FPGA_SOURCE_DIR. If it has no CMakeLists.txt it is cloned from
# {repo_url} (branch {branch}). JANA2 itself is fetched and built by the project
# (cmake/fetch_dependencies.cmake), which also applies the ClearOutputs patch.
if [ ! -f "{source_dir}/CMakeLists.txt" ]; then
    echo "No CMakeLists.txt in {source_dir} - cloning {repo_url} (branch {branch})"
    git clone --branch {branch} {repo_url} "{source_dir}"
fi

cmake -S "{source_dir}" -B "{build_dir}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="{install_dir}"
cmake --build "{build_dir}" --target install -j"$(nproc)"
""".format(**install_info.asdict())


# ---------------------------------------------------------------------------#
#  Helpers                                                                     #
# ---------------------------------------------------------------------------#

def run(command, exit_on_error=True):
    """Run a command, streaming output. Returns the exit code."""
    if isinstance(command, str):
        command = shlex.split(command)

    print("=" * 20)
    print("RUN: " + " ".join(command))
    print("=" * 20)

    start_time = datetime.now()
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    for raw in iter(process.stdout.readline, b""):
        line = raw.decode("latin-1").rstrip("\n")
        try:
            print(line)
        except Exception:
            print(str(line.encode("utf-8")))

    retval = process.wait()
    print("------------------------------------------")
    print(f"RUN DONE. RETVAL: {retval} (took {datetime.now() - start_time})\n\n")

    if retval != 0 and exit_on_error:
        print("ERROR. Return value is not 0. Please look at the logs above.\n")
        exit(1)
    return retval


def make_file(file_path, content):
    with open(file_path, "w") as f:
        f.write(content)


def miniforge_url():
    """Latest Miniforge installer URL for this OS/arch (GitHub-hosted)."""
    system = "MacOSX" if platform.system() == "Darwin" else platform.system()
    machine = platform.machine()  # x86_64, aarch64, arm64
    return (
        "https://github.com/conda-forge/miniforge/releases/latest/download/"
        f"Miniforge3-{system}-{machine}.sh"
    )


def is_conda_env_exist():
    env_dir = install_info.conda_env_dir
    return path.isdir(env_dir) and os.listdir(env_dir)


def _remove(target):
    """Remove a file or directory tree if it exists; print what happens."""
    if path.islink(target) or path.isfile(target):
        print(f"  rm    {target}")
        os.remove(target)
    elif path.isdir(target):
        print(f"  rm -r {target}")
        shutil.rmtree(target, ignore_errors=True)
    else:
        print(f"  (absent) {target}")


# ---------------------------------------------------------------------------#
#  Steps                                                                       #
# ---------------------------------------------------------------------------#

def step0_generate_scripts():
    print("Creating scripts directory")
    os.makedirs(install_info.scripts_dir, exist_ok=True)

    print("Generating scripts")
    make_file(install_info.script_env_sh, template_user_sh)
    make_file(install_info.script_env_csh, template_user_csh)
    make_file(install_info.script_openssl_cnf, openssl_cnf_content)
    make_file(install_info.script_setup_conda, template_setup_conda)
    make_file(install_info.script_build_soft, template_build_soft)


def step1_install_miniforge():
    if os.path.isdir(install_info.conda_dir):
        print("Miniforge directory already exists. Skipping installation step.")
        return

    url = miniforge_url()
    print(f"Downloading Miniforge from {url} ...")
    run(f"curl -L {url} -o miniforge.sh")
    run(f"bash miniforge.sh -b -p {install_info.conda_dir}")
    run("rm miniforge.sh")

    make_file(path.join(install_info.conda_dir, ".condarc"), condarc_content)


def step2_setup_conda():
    if is_conda_env_exist():
        print(f"Environment '{install_info.conda_env_name}' exists. Skipping creation.")
        return
    run("bash " + install_info.script_setup_conda)


def step3_build_software():
    run("bash " + install_info.script_build_soft)


def clear_all():
    """Remove everything the installer created, back to the pre-install state:
    the Miniforge tree, the generated install_scripts/ + setup_env.*, and the
    build/ and install/ trees. The project source is left untouched UNLESS this
    installer auto-cloned it into $TOP_DIR/JANA4ML4FPGA (never a checkout the
    script lives in, never an explicit ML4FPGA_SOURCE_DIR)."""
    targets = [
        install_info.conda_dir,       # miniforge
        install_info.scripts_dir,     # install_scripts/
        install_info.script_env_sh,   # setup_env.sh
        install_info.script_env_csh,  # setup_env.csh
        install_info.build_dir,       # <source>/build
        install_info.install_dir,     # <source>/install
    ]

    # Only remove the source tree when WE cloned it (auto-clone location, and no
    # explicit override). Removing build/ and install/ above already covers the
    # in-repo and explicit-source cases without touching the checkout itself.
    remove_clone = (
        install_info.source_override is None
        and install_info.source_dir == install_info.auto_clone_dir
        and install_info.source_dir != install_info.this_script_dir
    )
    if remove_clone:
        targets.append(install_info.auto_clone_dir)

    print("Removing installed artifacts (back to pre-install state):")
    for t in targets:
        _remove(t)

    if not remove_clone and path.isdir(install_info.source_dir):
        print(f"\nLeft the project source untouched: {install_info.source_dir}")
    print("\nDone.")


if __name__ == "__main__":
    steps = OrderedDict()
    steps["scripts"] = step0_generate_scripts
    steps["conda"] = step1_install_miniforge
    steps["setup"] = step2_setup_conda
    steps["build"] = step3_build_software

    steps_help = "Install steps (in default order):\n" + "\n".join(
        "   " + s for s in steps.keys()
    )

    parser = argparse.ArgumentParser(
        epilog=steps_help, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "-s", "--step",
        help="Name of a single installation step. 'all' (default) runs everything.",
        default="all",
    )
    parser.add_argument(
        "--clear", action="store_true",
        help="Remove everything the installer created (miniforge, install_scripts, "
             "setup_env.*, build, install) and exit.",
    )
    args = parser.parse_args()

    if args.clear:
        clear_all()
    elif args.step == "all":
        for step_func in steps.values():
            step_func()
    elif args.step in steps:
        steps[args.step]()
    else:
        parser.print_help()
