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

# $ afu_sim_setup --help
# usage: afu_sim_setup [-h] -s SOURCES [-p PLATFORM] [-t {VCS,QUESTA,MODELSIM}] [-f] [--ase-mode ASE_MODE] [--ase-verbose] dst

# Generate an ASE simulation environment for an AFU. An ASE environment is instantiated from the OPAE installation and then configured for the specified AFU. AFU source files are specified in a text file that is parsed by
# rtl_src_config, which is also part of the OPAE base environment.

# positional arguments:
#   dst                   Target directory path (directory must not exist).

# options:
#   -h, --help            show this help message and exit
#   -s SOURCES, --sources SOURCES
#                         AFU source specification file that will be passed to rtl_src_config. See "rtl_src_config --help" for the file's syntax. rtl_src_config translates the source list into either Quartus or RTL simulator syntax.
#   -p PLATFORM, --platform PLATFORM
#                         FPGA Platform to simulate.
#   -t {VCS,QUESTA,MODELSIM}, --tool {VCS,QUESTA,MODELSIM}
#                         Default simulator.
#   -f, --force           Overwrite target directory if it exists.
#   --ase-mode ASE_MODE   ASE execution mode (default, mode 3, exits on completion). See ase.cfg in the target directory.
#   --ase-verbose         When set, ASE prints each CCI-P transaction to the command line. Transactions are always logged to work/ccip_transactions.tsv, even when not set. This switch sets ENABLE_CL_VIEW in ase.cfg.
fuzz_afu_sim_setup() {
  if [ $# -lt 1 ]; then
    printf "usage: fuzz_afu_sim_setup <ITERS>\n"
    exit 1
  fi

  # Create a tmp run dir for easy clean-up
  local tmp_dir='tmp'
  if [ ! -d $tmp_dir ]; then
    mkdir $tmp_dir
  fi
  cd $tmp_dir || exit 1

  local source_dir='../../../samples/hello_world/hw/rtl/sources.txt'

  local -i iters=$1
  local -i i
  local -i p
  local -i n

  local -a short_parms=(\
'-h' '' \
'-s' "$source_dir" \
'-p' 'discrete' \
'-t' 'VCS' \
'-t' 'QUESTA' \
'-t' 'MODELSIM' \
'-f' '' \
'' '-i' \
)

  local -a long_parms=(\
'--help' '' \
'--sources' "$source_dir" \
'--platform' 'discrete' \
'--tool' 'VCS' \
'--tool' 'QUESTA' \
'--tool' 'MODELSIM' \
'--force' '' \
'--ase-mode' '' \
'--ase-verbose' '' \
'' '--invalid' \
)

  local cmd=''
  local -i num_parms
  local parm=''

  for (( i = 0 ; i < iters ; ++i )); do

    printf "===Fuzz Iteration: %d===\n" "$i"

    ## Short parm test
    cmd='afu_sim_setup '
    (( num_parms = 1 + RANDOM % (${#short_parms[@]} / 2) ))
    for (( n = 0 ; n < num_parms ; ++n )); do
      (( p = RANDOM % (${#short_parms[@]} / 2) ))
      parm="${short_parms[$p*2]}"
      cmd="${cmd} ${parm}"
      if [ -n "${short_parms[$p*2+1]}" ]; then
        parm="${short_parms[$p*2+1]}"
        parm="$(printf %s "${parm}" | radamsa)"
        cmd="${cmd} ${parm}"
      fi
    done
    # Add a dest dir
    parm="$(printf build_sim | radamsa)"
    cmd="${cmd} ${parm}" 
    # Run
    # printf "%s\n" "${cmd}"
    ${cmd}

    ## Long parm test
    cmd='afu_sim_setup '
    (( num_parms = 1 + RANDOM % (${#long_parms[@]} / 2) ))
    for (( n = 0 ; n < num_parms ; ++n )); do
      (( p = RANDOM % (${#long_parms[@]} / 2) ))
      parm="${long_parms[$p*2]}"
      cmd="${cmd} ${parm}"
      if [ -n "${long_parms[$p*2+1]}" ]; then
        parm="${long_parms[$p*2+1]}"
        parm="$(printf %s "${parm}" | radamsa)"
        cmd="${cmd} ${parm}"
      fi
    done
    # Add a dest dir
    parm="$(printf build_sim | radamsa)"
    cmd="${cmd} ${parm}" 
    # Run
    # printf "%s\n" "${cmd}"
    ${cmd}

  done

  cd ..
  rm -rf $tmp_dir
  echo "afu_sim_setup: SUCCESS"
}

# declare -i iters=1
# if [ $# -gt 0 ]; then
#  iters=$1
# fi
# fuzz_afu_sim_setup ${iters}
