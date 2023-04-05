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
`include "platform_if.vh"

//
// Only import this module when PCIe subsystem (SS) TLP emulation is
// required since the data structures may not be defined unless the
// platform supports it.
//
// PCIe SS is the standard OFS TLP encoding for all releases except
// the initial early access version.
//
`ifdef OFS_PLAT_PARAM_HOST_CHAN_IS_NATIVE_AXIS_PCIE_TLP
`ifdef OFS_PLAT_PARAM_HOST_CHAN_GASKET_PCIE_SS

module ase_pcie_ss_emulator
  #(
    // Minimum size and alignment of a read completion (RCB) in bytes
    parameter REQUEST_COMPLETION_BOUNDARY = 64,
    parameter CHANNEL_PAYLOAD_BYTES = 32,

    // ASE currently supports only one active fuction. This sets the
    // default function.
    parameter PF_NUM = 0,
    parameter VF_NUM = 0,
    parameter VF_ACTIVE = 1
    )
   (
    input  logic pClk,
   
    // Power & error states
    output logic       pck_cp2af_softReset,
    output logic [1:0] pck_cp2af_pwrState,
    output logic       pck_cp2af_error,
   
    // PCIe interface
    pcie_ss_axis_if.source pcie_rx_if,
    pcie_ss_axis_if.sink pcie_tx_if
    );

    import ase_pkg::*;
    import ase_sim_pkg::*;
    import ase_pcie_ss_pkg::*;

    localparam TDATA_WIDTH = ofs_pcie_ss_cfg_pkg::TDATA_WIDTH;
    typedef bit [TDATA_WIDTH-1:0] t_tdata;

    localparam TUSER_WIDTH = ofs_pcie_ss_cfg_pkg::TUSER_VENDOR_WIDTH;
    typedef bit [TUSER_WIDTH-1:0] t_tuser;

    localparam TKEEP_WIDTH = ofs_pcie_ss_cfg_pkg::TDATA_WIDTH / 8;
    typedef bit [TKEEP_WIDTH-1:0] t_tkeep;

    localparam NUM_OF_SOP = ofs_pcie_ss_cfg_pkg::NUM_OF_SOP;
    localparam NUM_OF_SEG = ofs_pcie_ss_cfg_pkg::NUM_OF_SEG;

    // Maximum number of FPGA->host tags. (Tag values must be less than this.)
    localparam MAX_OUTSTANDING_DMA_RD_REQS = ofs_pcie_ss_cfg_pkg::PCIE_EP_MAX_TAGS;
    // Maximum number of host->FPGA tags. (Tag values will be less than this.)
    localparam MAX_OUTSTANDING_MMIO_RD_REQS = ofs_pcie_ss_cfg_pkg::PCIE_RP_MAX_TAGS;

    localparam MAX_RD_REQ_BYTES = ofs_pcie_ss_cfg_pkg::MAX_RD_REQ_BYTES;
    localparam MAX_WR_PAYLOAD_BYTES = ofs_pcie_ss_cfg_pkg::MAX_WR_PAYLOAD_BYTES;

    localparam NUM_AFU_INTERRUPTS = ofs_fim_cfg_pkg::NUM_AFU_INTERRUPTS;

    // Does the PCIe SS sort completions? This capability was added after
    // the initial version so the test is protected by a macro.
`ifdef OFS_PCIE_SS_CFG_FLAG_CPL_REORDER
    localparam CPL_REORDER_EN = ofs_pcie_ss_cfg_pkg::CPL_REORDER_EN;
`else
    localparam CPL_REORDER_EN = 0;
`endif

    // Power and error state
    assign pck_cp2af_pwrState = 2'b0;
    assign pck_cp2af_error = 1'b0;
    // Reset out
    assign pck_cp2af_softReset = SoftReset;

    // Disable settings
    logic ase_logger_disable;

    /*
     * DPI import/export functions
     */
    // Scope function
    import "DPI-C" context function void scope_function();

    // Global listener function
    import "DPI-C" context task ase_listener(int mode);

    // Start simulation structures teardown
    import "DPI-C" context task start_simkill_countdown();

    // Signal to kill simulation
    export "DPI-C" task simkill;

    // Transaction count update ping/pong
    export "DPI-C" task count_error_flag_ping;
    import "DPI-C" function void count_error_flag_pong(int flag);

    // Software controlled process - run clocks
    export "DPI-C" task run_clocks;

    // Software controlled process - Run AFU Reset
    export "DPI-C" task afu_softreset_trig;

    // Simulator global reset (issued at simulator start, or session end)
    export "DPI-C" task ase_reset_trig;

    // cci_logger buffer message
    export "DPI-C" task buffer_msg_inject;

    // MMIO dispatch (Only used by CCI-P emulation, not here. Required
    // in order to define the symbol.)
    export "DPI-C" task mmio_dispatch;

    import "DPI-C" context function void pcie_ss_param_init(input t_ase_pcie_ss_param_cfg params);

    import "DPI-C" context function void pcie_ss_reset();

    import "DPI-C" context function void pcie_ss_stream_host_to_afu(
                                            input  longint cycle,
                                            input  int tready,
                                            output int tvalid,
                                            output int tlast,
                                            output t_tdata tdata,
                                            output t_tuser tuser,
                                            output t_tkeep tkeep);

    import "DPI-C" context function void pcie_ss_stream_afu_to_host(
                                            input  longint cycle,
                                            input  int tvalid,
                                            input  int tlast,
                                            input  t_tdata tdata,
                                            input  t_tuser tuser,
                                            input  t_tkeep tkeep);

    import "DPI-C" context function int pcie_ss_stream_afu_to_host_tready(
                                            input  longint cycle);

    // Scope generator
    initial scope_function();

    t_ase_pcie_ss_param_cfg param_cfg;
    assign param_cfg.tdata_width_bits = TDATA_WIDTH;
    assign param_cfg.tuser_width_bits = TUSER_WIDTH;
    assign param_cfg.max_outstanding_dma_rd_reqs = MAX_OUTSTANDING_DMA_RD_REQS;
    assign param_cfg.max_outstanding_mmio_rd_reqs = MAX_OUTSTANDING_MMIO_RD_REQS;
    assign param_cfg.num_afu_interrupts = NUM_AFU_INTERRUPTS;
    assign param_cfg.num_of_sop = NUM_OF_SOP;
    assign param_cfg.num_of_seg = NUM_OF_SEG;
    assign param_cfg.max_rd_req_bytes = MAX_RD_REQ_BYTES;
    assign param_cfg.max_wr_payload_bytes = MAX_WR_PAYLOAD_BYTES;
`ifndef PLATFORM_FPGA_FAMILY_S10
    // Allow up to 4KB data mover requests. The PCIe SS breaks them down into
    // legal sizes. Technically, the PCIe SS allows much larger requests but
    // OFS doesn't encourage it. Large requests harm fairness through PF/VF
    // arbiters.
    assign param_cfg.max_dm_rd_req_bytes = 4096;
    assign param_cfg.max_dm_wr_payload_bytes = 4096;
`else
    // DM is emulated on S10 and supports only native request sizes.
    assign param_cfg.max_dm_rd_req_bytes = MAX_RD_REQ_BYTES;
    assign param_cfg.max_dm_wr_payload_bytes = MAX_WR_PAYLOAD_BYTES;
`endif
    assign param_cfg.request_completion_boundary = REQUEST_COMPLETION_BOUNDARY;
    assign param_cfg.ordered_completions = CPL_REORDER_EN;
    // For now, we tie completion reordering and tag mapping together. Almost
    // all FIMs have a tag mapper. When completions may be out of order it makes
    // no sense to accept duplicate tags.
    assign param_cfg.emulate_tag_mapper = CPL_REORDER_EN;
    assign param_cfg.default_pf_num = PF_NUM;
    assign param_cfg.default_vf_num = VF_NUM;
    assign param_cfg.default_vf_active = VF_ACTIVE;
    initial pcie_ss_param_init(param_cfg);

    // Finish logger command
    logic finish_trigger = 0;

    assign ase_sim_pkg::system_is_idle = 1'b1;

    // ASE simulator reset
    task ase_reset_trig();
        ase_sim_pkg::system_reset_trig();
    endtask

    // Issue Simulation Finish trigger
    task issue_finish_trig();
       finish_trigger = 1;
       @(posedge clk);
       finish_trigger = 0;
       @(posedge clk);
    endtask

    // ASE clock
    assign clk = pClk;

    // AFU Soft Reset Trigger
    task afu_softreset_trig(int init, int value);
        ase_sim_pkg::afu_softreset(init, value);
    endtask


    /********************************************************************
     *
     * run_clocks : Run 'n' clocks
     * Software controlled event trigger for watching signals
     *
     * *****************************************************************/
    task run_clocks(int num_clks);
        ase_sim_pkg::advance_clock(num_clks);
    endtask

    /* ***************************************************************************
     * Buffer message injection into event logger
     * ---------------------------------------------------------------------------
     * Task sets buffer message to be posted into ccip_transactions.tsv log
     *
     * ***************************************************************************/
    string buffer_msg;
    logic  buffer_msg_en;
    logic  buffer_msg_tstamp_en;

    // Inject task
    task buffer_msg_inject(int timestamp_en, string logstr);
    begin
        buffer_msg = logstr;
        buffer_msg_en = 1;
        buffer_msg_tstamp_en = timestamp_en[0];
        @(posedge clk);
        buffer_msg_en = 0;
        @(posedge clk);
    end
    endtask

    // Ping to get error flag
    task count_error_flag_ping();
        count_error_flag_pong(0);
    endtask


    /* *******************************************************************
     *
     * Unified message watcher daemon
     * - Looks for MMIO Requests, buffer requests
     *
     * *******************************************************************/
    always @(posedge clk) begin : daemon_proc
        // Mode 2 indicates the OFS PCIe SS emulator is active
        ase_listener(2);
    end

    logic first_transaction_seen;
    int inactivity_counter;
    wire any_valid = pcie_tx_if.tvalid || pcie_rx_if.tvalid;

    // First transaction seen
    always @(posedge clk) begin : first_txn_watcher
        if (ase_reset) begin
            first_transaction_seen <= 0;
        end
        else if ( ~first_transaction_seen && any_valid ) begin
            first_transaction_seen <= 1;
        end
    end

    // Inactivity watchdog counter
    always @(posedge clk) begin : inact_ctr
        if (cfg.ase_mode != ASE_MODE_TIMEOUT_SIMKILL) begin
            inactivity_counter <= 0;
        end
        else begin
        // Watchdog countdown
            if (first_transaction_seen && any_valid) begin
                inactivity_counter <= 0;
            end
            else if (first_transaction_seen && ~any_valid) begin
                inactivity_counter <= inactivity_counter + 1;
            end
        end
    end

    // Inactivity management - killswitch
    always @(posedge clk) begin : call_simkill_countdown
        if ( (inactivity_counter > cfg.ase_timeout) && (cfg.ase_mode == ASE_MODE_TIMEOUT_SIMKILL) ) begin
            $display("  [SIM]  Inactivity timeout reached !!\n");
            start_simkill_countdown();
        end
    end


    /*
     * Initialization procedure
     */
    initial begin : ase_entry_point
        ase_sim_pkg::ase_sim_init();
    end


    /*
     * ASE management task
     */
    always @(posedge clk) begin : mgmt
        ase_sim_pkg::ase_sim_every_cycle();
    end


    /*
     * Logger module
     */
`ifndef ASE_DISABLE_LOGGER
    assign ase_logger_disable = 0;

    // ccip_logger instance
    ase_pcie_ss_logger
      #(
        .LOGNAME("log_ase_events.tsv")
        )
      ase_pcie_ss_logger
       (
        // Logger control
        .finish_logger(finish_trigger),
        .stdout_en(cfg.enable_cl_view[0]),
        // Buffer message injection
        .log_string_en(buffer_msg_en),
        .log_timestamp_en(buffer_msg_tstamp_en),
        .log_string(buffer_msg),

        .clk(clk),
        .SoftReset(SoftReset)
        );
`else
    assign ase_logger_disable = 1;
`endif //  `ifndef ASE_DISABLE_LOGGER


    /* ******************************************************************
     *
     * This call is made on ERRORs requiring a shutdown
     * simkill is called from software, and is the final step before
     * graceful closedown
     *
     * *****************************************************************/
    // Flag
    logic       simkill_started = 0;

    // Simkill progress
    task simkill();
        string print_str;
    begin
        simkill_started = 1;
        $display("  [SIM]  Simulation kill command received...");

        // Command to close logfd
        $finish;
    end
    endtask

    // MMIO dispatch. Defined just to keep the linker happy. Used only by CCI-P
    // emulation. For TLPs, MMIO arrives on the normal TLP stream.
    task mmio_dispatch(int initialize, ase_ccip_pkg::mmio_t mmio_pkt);
        $fatal(2, " ** ERROR ** %m: mmio_dispatch not expected for AXI-S PCIe TLPs!");
    endtask // mmio_dispatch

    longint cycle_counter;
    int rx_tvalid;
    int rx_tlast;
    t_tdata rx_tdata;
    t_tkeep rx_tkeep;
    t_tuser rx_tuser;

    t_tdata tx_tdata;
    t_tkeep tx_tkeep;
    t_tuser tx_tuser;

    always_ff @(posedge clk)
    begin
        if (ase_reset)
        begin
            cycle_counter <= 0;
        end
        else
        begin
            cycle_counter <= cycle_counter + 1;
        end
    end


    // Honor flow control on PCIe RX (host->AFU) stream
    logic rx_tready;
    assign rx_tready = !pcie_rx_if.tvalid || pcie_rx_if.tready;

    // Receive one cycle's worth of TLP data via DPI-C
    task get_rx_tlp_messages();
        // Call the software even if flow control prevents a new message
        pcie_ss_stream_host_to_afu(cycle_counter,
                                   (rx_tready ? 1 : 0),
                                   rx_tvalid, rx_tlast, rx_tdata, rx_tuser, rx_tkeep);
        if (rx_tready)
        begin
            pcie_rx_if.tvalid <= rx_tvalid[0];
            if (rx_tvalid[0])
            begin
                pcie_rx_if.tlast <= rx_tlast[0];
                pcie_rx_if.tdata <= rx_tdata;
                pcie_rx_if.tkeep <= rx_tkeep;
                pcie_rx_if.tuser_vendor <= rx_tuser;
            end
        end
    endtask // get_rx_tlp_messages


    // Send one cycle's worth of TLP data via DPI-C. tvalid and tready
    // are already known true.
    task send_tx_tlp_messages();
        tx_tdata = pcie_tx_if.tdata;
        tx_tkeep = pcie_tx_if.tkeep;
        tx_tuser = pcie_tx_if.tuser_vendor;

        pcie_ss_stream_afu_to_host(cycle_counter, 1,
                                   (pcie_tx_if.tlast ? 1 : 0),
                                   tx_tdata, tx_tuser, tx_tkeep);
    endtask // send_tx_tlp_messages


    //
    // Main simulation control.
    //
    always @(posedge clk)
    begin
        if (SoftReset)
        begin
            pcie_rx_if.tvalid <= 1'b0;
            pcie_rx_if.tlast <= 1'b0;
            pcie_rx_if.tdata <= '0;
            pcie_rx_if.tkeep <= '0;
            pcie_rx_if.tuser_vendor <= '0;

            pcie_tx_if.tready <= 1'b0;
            pcie_ss_reset();
        end
        else
        begin
            // Receive new TLP messages from host
            get_rx_tlp_messages();

            // Send TLP messages to host
            if (pcie_tx_if.tvalid && pcie_tx_if.tready)
            begin
                send_tx_tlp_messages();
            end

            // Can the host receive a message next cycle?
            pcie_tx_if.tready <= pcie_ss_stream_afu_to_host_tready(cycle_counter) &&
                                 ! reset_lockdown;
        end
    end

endmodule // ase_pcie_ss_emulator

`endif //  `ifdef OFS_PLAT_PARAM_HOST_CHAN_GASKET_PCIE_SS
`endif //  `ifndef OFS_PLAT_PARAM_HOST_CHAN_IS_NATIVE_AXIS_PCIE_TLP
