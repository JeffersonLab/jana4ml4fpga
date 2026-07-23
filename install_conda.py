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
    python3 install_conda.py                 # all steps
    python3 install_conda.py -s setup_conda  # a single step
    python3 install_conda.py -s build_soft   # rebuild only

Steps (default order): gen_scripts, install_conda, setup_conda, build_soft
The install goes into $ML4FPGA_TOP_DIR (default: this script's directory).
"""

import argparse
import os
import platform
import shlex
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

        self.scripts_dir = path.join(self.top_dir, INSTALL_SCRIPTS_DIR_NAME)
        self.script_setup_conda = path.join(self.scripts_dir, "setup_conda.sh")
        self.script_build_soft = path.join(self.scripts_dir, "build_software.sh")
        self.script_openssl_cnf = path.join(self.scripts_dir, "openssl.cnf")
        self.script_env_sh = path.join(self.top_dir, "setup_env.sh")
        self.script_env_csh = path.join(self.top_dir, "setup_env.csh")

    def asdict(self):
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

template_user_sh = """\
export {env_name_top_dir}={top_dir}

# Activate the conda environment
source $ML4FPGA_TOP_DIR/miniforge/etc/profile.d/conda.sh
conda activate {conda_env_name}
""".format(env_name_top_dir=ENV_NAME_TOP_DIR, **install_info.asdict())

template_user_csh = """\
setenv {env_name_top_dir} {top_dir}

# Activate the conda environment
source $ML4FPGA_TOP_DIR/miniforge/etc/profile.d/conda.csh
conda activate {conda_env_name}
""".format(env_name_top_dir=ENV_NAME_TOP_DIR, **install_info.asdict())

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

# JANA2 is fetched and built by the project (cmake/fetch_dependencies.cmake).
cmake -S {this_script_dir} -B {this_script_dir}/build -DCMAKE_BUILD_TYPE=Release
cmake --build {this_script_dir}/build --target install -j"$(nproc)"
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
    print(f"------------------------------------------")
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


if __name__ == "__main__":
    steps = OrderedDict()
    steps["gen_scripts"] = step0_generate_scripts
    steps["install_conda"] = step1_install_miniforge
    steps["setup_conda"] = step2_setup_conda
    steps["build_soft"] = step3_build_software

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
    args = parser.parse_args()

    if args.step == "all":
        for step_func in steps.values():
            step_func()
    elif args.step in steps:
        steps[args.step]()
    else:
        parser.print_help()
