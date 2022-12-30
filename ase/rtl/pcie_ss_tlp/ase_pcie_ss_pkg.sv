//
// Copyright (c) 2020, Intel Corporation
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// Neither the name of the Intel Corporation nor the names of its contributors
// may be used to endorse or promote products derived from this software
// without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

`include "platform.vh"

//
// Data types for AXI-S PCIe SS emulation

package ase_pcie_ss_pkg;

    typedef struct {
        int tdata_width_bits;
        int tuser_width_bits;
        int max_outstanding_dma_rd_reqs;    // DMA tags must be less than this value
        int max_outstanding_mmio_rd_reqs;   // MMIO tags must be less than this value
        int num_afu_interrupts;
        int num_of_sop;                     // Maximum number of SOPs in one tdata
        int num_of_seg;                     // Maximum number of segments in one tdata
        int max_rd_req_bytes;               // Maximum size of a DMA read request
        int max_wr_payload_bytes;           // Maximum size of a DMA write request
        int request_completion_boundary;    // Minimum size of a read completion
        int ordered_completions;            // Keep completions in order?
        int emulate_tag_mapper;             // Accept duplicate DMA read request tags?

        // ASE currently supports only one active function. Set the default function.
        // This could be overridden by ASE configuration at run time.
        int default_pf_num;
        int default_vf_num;
        int default_vf_active;
    } t_ase_pcie_ss_param_cfg;

endpackage // ase_pcie_ss_pkg
