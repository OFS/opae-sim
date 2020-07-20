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
// Data types for AXI-S PCIe TLP emulation

package axis_pcie_tlp_pkg;

    typedef struct {
        int num_tlp_channels;
        int max_outstanding_dma_rd_reqs;    // DMA tags must be less than this value
        int max_outstanding_mmio_rd_reqs;   // MMIO tags must be less than this value
        int num_afu_interrupts;
        int max_payload_bytes;              // Maximum size of a payload
        int request_completion_boundary;    // Minimum size of a read completion
        int channel_payload_bytes;
    } t_ase_axis_param_cfg;

    //
    // Single-bit fields are encoded as "byte" for easier handling in C.
    // Only the low bit will be used.
    //

    // Avoid endian problems by explicit index naming
    typedef struct {
        int h0;
        int h1;
        int h2;
        int h3;
    } t_ase_axis_pcie_hdr;

    // Generic single-channel format, either AFU->host or host->AFU.
    typedef struct {
        bit [255:0] payload;
        bit [127:0] hdr;
        byte irq_id;    // Used only by AFU->host when afu_irq is set in tuser
        byte eop;
        byte sop;
        byte valid;
    } t_ase_axis_pcie_tdata;

    // Only the fields ASE simulates are defined. Any others default to 0.
    typedef struct {
        byte mmio_req;
    } t_ase_axis_pcie_rx_tuser;

    // Only the fields ASE simulates are defined. Any others default to 0.
    typedef struct {
        byte afu_irq;
    } t_ase_axis_pcie_tx_tuser;

    // Interrupt response
    typedef struct {
        shortint rid;
        byte irq_id;
        byte tvalid;
    } t_ase_axis_pcie_irq_rsp;

endpackage // axis_pcie_tlp_pkg
