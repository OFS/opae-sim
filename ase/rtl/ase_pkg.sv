/* ****************************************************************************
 * Copyright(c) 2011-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Intel Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * **************************************************************************
 *
 * Module Info:
 * Language   : System{Verilog} | C/C++
 * Owner      : Rahul R Sharma
 *              rahul.r.sharma@intel.com
 *              Intel Corporation
 *
 * ASE generics (SystemVerilog header file)
 *
 * Description:
 * This file contains definitions and parameters for the DPI
 * module. The intent of this file is that the user should not modify
 * the DPI source files. **Only** this header file must be modified if
 * any DPI parameters need to be changed.
 *
 */

`include "platform.vh"

package ase_pkg;

   // ASE modes
   parameter ASE_MODE_DAEMON          = 1;
   parameter ASE_MODE_TIMEOUT_SIMKILL = 2;
   parameter ASE_MODE_SW_SIMKILL      = 3;
   parameter ASE_MODE_REGRESSION      = 4;

   /*
    * ASE config structure
    * This will reflect ase.cfg
    */
   typedef struct {
      int         ase_mode;
      int 	  ase_timeout;
      int 	  ase_num_tests;
      int 	  enable_reuse_seed;
      int 	  ase_seed;
      int 	  enable_cl_view;
      int 	  usr_tps;
      int 	  phys_memory_available_gb;
   } ase_cfg_t;
   static ase_cfg_t cfg;

endpackage
