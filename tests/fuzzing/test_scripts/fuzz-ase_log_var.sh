#!/bin/bash
# Copyright(c) 2023, Intel Corporation
#
# Redistribution  and  use  in source  and  binary  forms,  with  or  without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of  source code  must retain the  above copyright notice,
#   this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name  of Intel Corporation  nor the names of its contributors
#   may be used to  endorse or promote  products derived  from this  software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
# IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
# LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
# CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
# SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
# INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
# CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

#### Variables

PRJ_DIR='../build_sim'
AFU_DIR='../../../samples/hello_world'
TEMPLATE_DIR='../templates'
REBUILD=0

declare -i iters=1

#### Usage

Usage()
{
   # Display Help
   echo "Runs fuzz testing for environment variables LIBFPGA_LOG and ASE_LOG."
   echo
   echo "Syntax: fuzz-ase_log_var.sh [-h|d|b|i]"
   echo "options:"
   echo "h     Print this Help."
   echo "d     Specify a custom ASE build directory."
   echo "b     Force a cleanup of existing build and rebuild."
   echo "i     Specify number of test iterations."
   echo
}

#### Options

while getopts ":hd:bi:" option; do
   case $option in
      h) # display Help
         Usage
         exit;;
      d) # Set build directory
         PRJ_DIR=$OPTARG;;
      b) # Set if to rebuild
         REBUILD=1;;
      i) # Set test iterations
         iters=$OPTARG;;
     \?) # Invalid option
         echo "Error: Invalid option"
         exit;;
   esac
done

#### Helpers

cleanup_prj_dir() {
  if [ -d "${PRJ_DIR}" ]; then
    echo "Cleaning up project directory..."
    rm -rf "${PRJ_DIR}"
  fi
}

cleanup_work_dir() {
  local readypid
  readypid=$(find . -name .ase_ready.pid)
  if [ -n "${readypid}" ] && [ -f "${readypid}" ]; then
    rm "${readypid}"
  fi
}

setup_and_compile() {
  cleanup_prj_dir

  afu_sim_setup --sources ${AFU_DIR}/hw/rtl/sources.txt --ase-mode 4 "${PRJ_DIR}"
  if [ ! -d "${PRJ_DIR}" ]; then
    echo "Failed to create ASE project directory"
    exit 1
  fi

  make -C "${PRJ_DIR}"
  cp -pf ${TEMPLATE_DIR}/ase_regress.sh.in "${PRJ_DIR}"/ase_regress.sh
  chmod +x "${PRJ_DIR}"/ase_regress.sh
}

fuzz_ase_log_var() {
  if [ $# -lt 1 ]; then
    printf "usage: fuzz-ase_log_var <ITERS>\n"
    exit 1
  fi

  local -i iters=$1
  for (( i = 0 ; i < iters ; ++i )); do
    # Build software if running for the first time
    if [ "$i" -eq 0 ]; then 
      if [ -f ${AFU_DIR}/sw/hello_world ]; then
        make -C ${AFU_DIR}/sw clean > /dev/null
      fi
      make -C ${AFU_DIR}/sw CC=gcc > /dev/null
    fi

    # Copy ase.cfg and regress.sh
    cp -pf ${TEMPLATE_DIR}/ase.cfg.in ${PRJ_DIR}/ase.cfg
    cp -pf ${TEMPLATE_DIR}/ase_regress.sh.in "${PRJ_DIR}"/ase_regress.sh
    chmod +x "${PRJ_DIR}"/ase_regress.sh

    # Mutate and set LIBFPGA_LOG and ASE_LOG
    local -a origval=('-2' '0' '1')
    local newval=''
    (( p = RANDOM % ${#origval[@]} ))
    newval="${origval[$p]}"
    newval="$(printf %s "${newval}" | radamsa)"
    export LIBFPGA_LOG="$newval"
    export ASE_LOG="$newval"

    # Run simulation
    local sim_log='sim.log'
    pushd "${PRJ_DIR}" > /dev/null || exit 1
    timeout 45 make sim &> $sim_log
    ret=$?

    # Test
    if [ $ret -eq 126 ] && [ ${#newval} -ge 131000 ]; then # Env var too long
      echo "ase_log_var: env var length hit command length limit, skipping check"
    else
      if ! (grep "Simulator started..." $sim_log &> /dev/null); then # vsim failed
        echo "ase_log_var: vsim unsuccessful, skipping check"
      # This one should always pass
      elif [ $ret -ne 0 ]; then
        echo "ase_log_var: Unexpected error $ret, FAILED"
        cleanup_work_dir
        exit 1
      fi
    fi
    
    # Clean up
    unset LIBFPGA_LOG
    unset ASE_LOG
    cleanup_work_dir
    rm $sim_log
    popd > /dev/null || exit 1
  done
  echo "ase_log_var: SUCCESS"
}

#### Main
# if [ ! -d "${PRJ_DIR}" ] || [ ${REBUILD} -eq 1 ]; then
#   setup_and_compile
# fi
# fuzz_ase_log_var "${iters}"
