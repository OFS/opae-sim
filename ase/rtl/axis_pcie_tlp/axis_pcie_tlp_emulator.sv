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

// Only import this module when PCIe TLP emulation is required since the
// data structures may not be defined unless the platform supports it.

`ifdef OFS_PLAT_PARAM_HOST_CHAN_IS_NATIVE_AXIS_PCIE_TLP

module axis_pcie_tlp_emulator
  #(
    parameter NUM_TLP_CHANNELS = 1,
    // DMA tags must be less than this value
    parameter MAX_OUTSTANDING_DMA_RD_REQS = 256,
    // MMIO tags must be less than this value
    parameter MAX_OUTSTANDING_MMIO_RD_REQS = 64,
    parameter NUM_AFU_INTERRUPTS = 4,
    // Maximum length of a full payload (over multiple cycles)
    parameter MAX_PAYLOAD_BYTES = 256,
    // Minimum size and alignment of a read completion (RCB)
    parameter REQUEST_COMPLETION_BOUNDARY = 64,
    parameter CHANNEL_PAYLOAD_BYTES = 32
    )
   (
    input  logic pClk,
   
    // Power & error states
    output logic       pck_cp2af_softReset,
    output logic [1:0] pck_cp2af_pwrState,
    output logic       pck_cp2af_error,
   
    // Interface structures
    ofs_plat_host_chan_axis_pcie_tlp_if pcie_tlp_if
    );

    import ase_pkg::*;
    import ase_sim_pkg::*;
    import axis_pcie_tlp_pkg::*;

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

    import "DPI-C" context function void pcie_param_init(input t_ase_axis_param_cfg params);

    import "DPI-C" context function void pcie_tlp_reset();

    import "DPI-C" context function void pcie_tlp_stream_host_to_afu_ch(
                                            input  longint cycle,
                                            input  int ch,
                                            input  int tready,
                                            output t_ase_axis_pcie_tdata tdata,
                                            output t_ase_axis_pcie_rx_tuser tuser);

    import "DPI-C" context function void pcie_tlp_stream_afu_to_host_ch(
                                            input  longint cycle,
                                            input  int ch,
                                            input  t_ase_axis_pcie_tdata tdata,
                                            input  t_ase_axis_pcie_tx_tuser tuser);

    import "DPI-C" context function int pcie_tlp_stream_afu_to_host_tready(
                                            input  longint cycle);

    import "DPI-C" context function void pcie_host_to_afu_irq_rsp(
                                            input  longint cycle,
                                            input  int tready,
                                            output t_ase_axis_pcie_irq_rsp irq_rsp);

    // Scope generator
    initial scope_function();

    t_ase_axis_param_cfg param_cfg;
    assign param_cfg.num_tlp_channels = NUM_TLP_CHANNELS;
    assign param_cfg.max_outstanding_dma_rd_reqs = MAX_OUTSTANDING_DMA_RD_REQS;
    assign param_cfg.max_outstanding_mmio_rd_reqs = MAX_OUTSTANDING_MMIO_RD_REQS;
    assign param_cfg.num_afu_interrupts = NUM_AFU_INTERRUPTS;
    assign param_cfg.max_payload_bytes = MAX_PAYLOAD_BYTES;
    assign param_cfg.request_completion_boundary = REQUEST_COMPLETION_BOUNDARY;
    assign param_cfg.channel_payload_bytes = CHANNEL_PAYLOAD_BYTES;
    initial pcie_param_init(param_cfg);

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
        ase_listener(1);
    end


`ifdef FOOBAR
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
`endif

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
     * CCI Logger module
     */
`ifndef ASE_DISABLE_LOGGER
    assign ase_logger_disable = 0;

    // ccip_logger instance
    axis_pcie_tlp_logger
      #(
        .LOGNAME("log_ase_events.tsv")
        )
      axis_pcie_tlp_logger
       (
        // Logger control
        .finish_logger(finish_trigger),
        .stdout_en(cfg.enable_cl_view[0]),
        // Buffer message injection
        .log_string_en(buffer_msg_en),
        .log_timestamp_en(buffer_msg_tstamp_en),
        .log_string(buffer_msg),
        // CCIP ports
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
    int ch;
    t_ase_axis_pcie_tdata rx_tdata[NUM_TLP_CHANNELS];
    t_ase_axis_pcie_rx_tuser rx_tuser[NUM_TLP_CHANNELS];
    t_ase_axis_pcie_tdata tx_tdata[NUM_TLP_CHANNELS];
    t_ase_axis_pcie_tx_tuser tx_tuser[NUM_TLP_CHANNELS];

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
    assign rx_tready = !pcie_tlp_if.afu_rx_st.tvalid ||
                       pcie_tlp_if.afu_rx_st.tready;

    // Map TLP channel message received via DPI-C to the SystemVerilog type
    task map_rx_tlp_ch(int ch);
        pcie_tlp_if.afu_rx_st.t.data[ch].valid <= rx_tdata[ch].valid[0];
        if (rx_tdata[ch].valid[0])
        begin
            pcie_tlp_if.afu_rx_st.t.data[ch].sop <= rx_tdata[ch].sop[0];
            pcie_tlp_if.afu_rx_st.t.data[ch].eop <= rx_tdata[ch].eop[0];
            pcie_tlp_if.afu_rx_st.t.data[ch].payload <= rx_tdata[ch].payload;
            pcie_tlp_if.afu_rx_st.t.data[ch].hdr <= rx_tdata[ch].hdr;
            pcie_tlp_if.afu_rx_st.t.user[ch] <= '0;
            pcie_tlp_if.afu_rx_st.t.user[ch].mmio_req <= rx_tuser[ch].mmio_req[0];
        end
    endtask // map_rx_tlp_ch

    // Receive one cycle's worth of TLP data via DPI-C
    task get_rx_tlp_messages();
        logic new_tlps_valid;
        new_tlps_valid = 0;
        for (ch = 0; ch < NUM_TLP_CHANNELS; ch = ch + 1)
        begin
            // Call the software even if flow control prevents a new message
            pcie_tlp_stream_host_to_afu_ch(cycle_counter, ch,
                                           (rx_tready ? 1 : 0),
                                           rx_tdata[ch], rx_tuser[ch]);
            new_tlps_valid = new_tlps_valid || rx_tdata[ch].valid[0];

            if (rx_tready)
            begin
                map_rx_tlp_ch(ch);
            end
        end

        if (rx_tready)
        begin
            pcie_tlp_if.afu_rx_st.tvalid <= new_tlps_valid;
        end
    endtask // get_rx_tlp_messages


    t_ase_axis_pcie_irq_rsp rx_irq_rsp;
    logic rx_irq_tready;
    assign rx_irq_tready = !pcie_tlp_if.afu_irq_rx_st.tvalid ||
                           pcie_tlp_if.afu_irq_rx_st.tready;

    // Map interrupt responses received via DPI-C to the SystemVerilog type
    task map_rx_irq_rsp();
        pcie_tlp_if.afu_irq_rx_st.tvalid <= rx_irq_rsp.tvalid[0];
        pcie_tlp_if.afu_irq_rx_st.t.data.rid <= rx_irq_rsp.rid;
        pcie_tlp_if.afu_irq_rx_st.t.data.irq_id <= rx_irq_rsp.irq_id;
    endtask // map_rx_irq_rsp

    // Receive one cycle's interrupt responses
    task get_rx_irq_responses();
        // Call the software even if flow control prevents a new message
        pcie_host_to_afu_irq_rsp(cycle_counter,
                                 (rx_irq_tready ? 1 : 0),
                                 rx_irq_rsp);

        if (rx_irq_tready)
        begin
            map_rx_irq_rsp();
        end
    endtask // get_rx_irq_responses


    // Map TLP channel message for sending via DPI-C to the SystemVerilog type
    ofs_fim_if_pkg::t_axis_irq_tdata tx_irq_data[NUM_TLP_CHANNELS];

    task map_tx_tlp_ch(int ch);
        tx_tdata[ch].valid = { '0, pcie_tlp_if.afu_tx_st.t.data[ch].valid };
        tx_tdata[ch].sop = { '0, pcie_tlp_if.afu_tx_st.t.data[ch].sop };
        tx_tdata[ch].eop = { '0, pcie_tlp_if.afu_tx_st.t.data[ch].eop };
        tx_tdata[ch].payload = pcie_tlp_if.afu_tx_st.t.data[ch].payload;
        tx_tdata[ch].hdr = pcie_tlp_if.afu_tx_st.t.data[ch].hdr;

        // Only for interrupts
        tx_tuser[ch].afu_irq = { '0, pcie_tlp_if.afu_tx_st.t.user[ch].afu_irq };
        tx_irq_data[ch] = ofs_fim_if_pkg::t_axis_irq_tdata'(pcie_tlp_if.afu_tx_st.t.data[ch].hdr);
        tx_tdata[ch].irq_id = tx_irq_data[ch].irq_id;
    endtask // map_tx_tlp_ch

    // Send one cycle's worth of TLP data via DPI-C. tvalid is already known true.
    task send_tx_tlp_messages();
        for (ch = 0; ch < NUM_TLP_CHANNELS; ch = ch + 1)
        begin
            if (pcie_tlp_if.afu_tx_st.t.data[ch].valid)
            begin
                map_tx_tlp_ch(ch);

                pcie_tlp_stream_afu_to_host_ch(cycle_counter, ch,
                                               tx_tdata[ch], tx_tuser[ch]);
            end
        end
    endtask // send_tx_tlp_messages


    //
    // Main simulation control.
    //
    always @(posedge clk)
    begin
        if (SoftReset)
        begin
            pcie_tlp_if.afu_rx_st.tvalid <= 1'b0;
            pcie_tlp_if.afu_rx_st.t <= '0;
            pcie_tlp_if.afu_irq_rx_st.tvalid <= 1'b0;
            pcie_tlp_if.afu_irq_rx_st.t <= '0;
            pcie_tlp_if.afu_tx_st.tready <= 1'b0;
            pcie_tlp_reset();
        end
        else
        begin
            // Receive new TLP messages from host
            get_rx_tlp_messages();
            get_rx_irq_responses();

            // Send TLP messages to host
            if (pcie_tlp_if.afu_tx_st.tready)
            begin
                send_tx_tlp_messages();
            end

            // Can the host receive a message next cycle?
            pcie_tlp_if.afu_tx_st.tready <= pcie_tlp_stream_afu_to_host_tready(cycle_counter) &&
                                            ! reset_lockdown;
        end
    end

endmodule // axis_pcie_tlp_emulator

`endif //  `ifndef OFS_PLAT_PARAM_HOST_CHAN_IS_NATIVE_AXIS_PCIE_TLP
