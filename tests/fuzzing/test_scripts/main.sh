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

shopt -o -s nounset

source fuzz-afu_sim_setup.sh
source fuzz-ase_cfg_clkmhz.sh
source fuzz-ase_cfg_clview.sh
source fuzz-ase_cfg_mode.sh
source fuzz-ase_cfg_numtest.sh
source fuzz-ase_cfg_physmem.sh
source fuzz-ase_cfg_seed.sh
source fuzz-ase_cfg_timeout.sh
source fuzz-ase_cfg.sh
source fuzz-ase_config.sh
source fuzz-ase_log_var.sh
source fuzz-ase_regress.sh
source fuzz-ase_script.sh
source fuzz-ase_workdir.sh

fuzz_all() {
  if [ $# -lt 1 ]; then
    printf "usage: fuzz_all <ITERS>\n"
    exit 1
  fi
  local -i iters=$1

  fuzz_afu_sim_setup "${iters}"
  fuzz_ase_cfg_clkmhz "${iters}"
  fuzz_ase_cfg_clview "${iters}"
  fuzz_ase_cfg_mode "${iters}"
  fuzz_ase_cfg_numtest "${iters}"
  fuzz_ase_cfg_physmem "${iters}"
  fuzz_ase_cfg_seed "${iters}"
  fuzz_ase_cfg_timeout "${iters}"
  fuzz_ase_cfg "${iters}"
  fuzz_ase_config "${iters}"
  fuzz_ase_log_var "${iters}"
  fuzz_ase_regress "${iters}"
  fuzz_ase_script "${iters}"
  fuzz_ase_workdir "${iters}"
}

declare -i iters=1
if [ $# -gt 0 ]; then
  iters=$1
fi
if [ ! -d ${PRJ_DIR} ]; then
  setup_and_compile
fi
fuzz_all "${iters}"
