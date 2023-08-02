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
   echo "Runs fuzz testing for the ENABLE_CL_VIEW configuration of ase.cfg."
   echo
   echo "Syntax: fuzz-ase_cfg_clview.sh [-h|d|b|i]"
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

fuzz_ase_cfg_clview() {
  if [ $# -lt 1 ]; then
    printf "usage: fuzz-ase_cfg_clview <ITERS>\n"
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

    # Copy regress.sh and ase.cfg, mutate regress.sh and set ENABLE_CL_VIEW
    cp -pf ${TEMPLATE_DIR}/ase_regress.sh.in "${PRJ_DIR}"/ase_regress.sh
    chmod +x "${PRJ_DIR}"/ase_regress.sh
    cp -pf ${TEMPLATE_DIR}/ase.cfg.in "${PRJ_DIR}"/ase.cfg
    local -a origval=('0' '1')
    local newval=''
    (( p = RANDOM % ${#origval[@]} ))
    newval="${origval[$p]}"
    newval="$(printf %s "${newval}" | radamsa)"
    PATTERN="$newval" perl -pi -e 's/ENABLE_CL_VIEW = 0/ENABLE_CL_VIEW = $ENV{PATTERN}/g' "${PRJ_DIR}"/ase.cfg

    # Run simulation
    local sim_log='sim.log'
    pushd "${PRJ_DIR}" > /dev/null || exit 1
    timeout 45 make sim &> $sim_log
    ret=$?

    # Test
    if ! (grep "Simulator started..." $sim_log &> /dev/null); then # vsim failed
      echo "ase_cfg_clview: vsim unsuccessful, skipping check"
    # This one should always pass
    elif [ $ret -ne 0 ]; then # other failure
      echo "ase_cfg_clview: Unexpected error, FAILED"
      cleanup_work_dir
      exit 1
    fi
    
    # Clean up
    cleanup_work_dir
    rm $sim_log
    popd > /dev/null || exit 1
  done
  echo "ase_cfg_clview: SUCCESS"
}

#### Main
# if [ ! -d "${PRJ_DIR}" ] || [ ${REBUILD} -eq 1 ]; then
#   setup_and_compile
# fi
# fuzz_ase_cfg_clview "${iters}"
