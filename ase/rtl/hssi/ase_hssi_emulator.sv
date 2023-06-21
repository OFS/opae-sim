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
// TODO:
//`ifdef OFS_PLAT_PARAM_HOST_CHAN_IS_NATIVE_AXIS_PCIE_TLP
//`ifdef OFS_PLAT_PARAM_HOST_CHAN_GASKET_PCIE_SS

module ase_hssi_emulator
  #(
    parameter CHANNEL_ID = 0
    )
   (
    // HSSI interface
    ofs_fim_hssi_ss_tx_axis_if data_tx,
    ofs_fim_hssi_ss_rx_axis_if data_rx,
    ofs_fim_hssi_fc_if         fc
    );

    import ase_pkg::*;
    import ase_sim_pkg::*;
    import ase_hssi_pkg::*;
    import ofs_fim_eth_if_pkg::*;

    localparam TDATA_WIDTH = ofs_fim_eth_if_pkg::ETH_PACKET_WIDTH;
    typedef bit [TDATA_WIDTH-1:0] t_tdata;

    localparam TUSER_WIDTH = ofs_fim_eth_if_pkg::ETH_RX_ERROR_WIDTH;
    typedef bit [TUSER_WIDTH-1:0] t_tuser;

    localparam TKEEP_WIDTH = ofs_fim_eth_if_pkg::ETH_TKEEP_WIDTH;
    typedef bit [TKEEP_WIDTH-1:0] t_tkeep;

    localparam NUM_AFU_INTERRUPTS = ofs_fim_cfg_pkg::NUM_AFU_INTERRUPTS;

    // Disable settings
    logic ase_logger_disable;

    /*
     * DPI import/export functions
     */

    // logger buffer message
    export "DPI-C" task buffer_msg_inject;

    import "DPI-C" context function void hssi_param_init(input t_ase_hssi_param_cfg params);

    import "DPI-C" context function void hssi_reset(input int chan);

    import "DPI-C" context function void hssi_stream_host_to_afu(
                                            input  longint cycle,
                                            input  int chan,
                                            output int tvalid,
                                            output int tlast,
                                            output t_tdata tdata,
                                            output t_tuser tuser,
                                            output t_tkeep tkeep);

    import "DPI-C" context function void hssi_stream_afu_to_host(
                                            input  longint cycle,
                                            input  int chan,
                                            input  int tvalid,
                                            input  int tlast,
                                            input  t_tdata tdata,
                                            input  t_tuser tuser,
                                            input  t_tkeep tkeep);

    import "DPI-C" context function int hssi_stream_afu_to_host_tready(
                                            input  longint cycle,
                                            input  int chan);

    t_ase_hssi_param_cfg param_cfg;
    assign param_cfg.tdata_width_bits = TDATA_WIDTH;
    assign param_cfg.tuser_width_bits = TUSER_WIDTH;
    initial hssi_param_init(param_cfg);

    /* ***************************************************************************
     * Buffer message injection into event logger
     * ---------------------------------------------------------------------------
     * Task sets buffer message to be posted into hssi_transactions.tsv log
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
        @(posedge data_rx.clk);
        buffer_msg_en = 0;
        @(posedge data_rx.clk);
    end
    endtask

    /*
     * Logger module
     */
`ifndef ASE_DISABLE_LOGGER
    assign ase_logger_disable = 0;

    // hssi_logger instance
    ase_hssi_logger
      #(
        .LOGNAME("log_ase_events.tsv")
        )
      ase_hssi_logger
       (
        // Logger control
        .finish_logger(finish_trigger),
        .stdout_en(1),
        // Buffer message injection
        .log_string_en(buffer_msg_en),
        .log_timestamp_en(buffer_msg_tstamp_en),
        .log_string(buffer_msg),

        .clk(data_rx.clk),
        .SoftReset(SoftReset)
        );
`else
    assign ase_logger_disable = 1;
`endif //  `ifndef ASE_DISABLE_LOGGER

    longint cycle_counter;
    int rx_tvalid;
    int rx_tlast;
    t_tdata rx_tdata;
    t_tkeep rx_tkeep;
    t_tuser rx_tuser;
    t_tdata tx_tdata;
    t_tkeep tx_tkeep;
    t_tuser tx_tuser;

    always_ff @(posedge data_rx.clk)
    begin
        if (!data_rx.rst_n)
        begin
            cycle_counter <= 0;
        end
        else
        begin
            cycle_counter <= cycle_counter + 1;
        end
    end

    // Receive one cycle's worth of HSSI data via DPI-C
    task get_rx_hssi_messages();
        // Call the software even if flow control prevents a new message
        hssi_stream_host_to_afu(cycle_counter, CHANNEL_ID,
                                rx_tvalid, rx_tlast, rx_tdata, rx_tuser, rx_tkeep);
        data_rx.rx.tvalid <= rx_tvalid[0];
        if (rx_tvalid[0])
        begin
            data_rx.rx.tlast <= rx_tlast[0];
            data_rx.rx.tdata <= rx_tdata;
            data_rx.rx.tkeep <= rx_tkeep;
            data_rx.rx.tuser <= rx_tuser;
        end
    endtask // get_rx_hssi_messages


    // Send one cycle's worth of HSSI data via DPI-C. tvalid and tready
    // are already known true.
    task send_tx_hssi_messages();
        tx_tdata = data_tx.tx.tdata;
        tx_tkeep = data_tx.tx.tkeep;
        tx_tuser = data_tx.tx.tuser;

        hssi_stream_afu_to_host(cycle_counter, CHANNEL_ID, 1,
                                   (data_tx.tx.tlast ? 1 : 0),
                                   tx_tdata, tx_tuser, tx_tkeep);
    endtask // send_tx_hssi_messages


    //
    // Main simulation control.
    //
    always @(posedge data_rx.clk)
    begin
        if (!data_rx.rst_n)
        begin
            data_rx.rx.tvalid <= 1'b0;
            data_rx.rx.tlast <= 1'b0;
            data_rx.rx.tdata <= '0;
            data_rx.rx.tkeep <= '0;
            data_rx.rx.tuser <= '0;

            data_tx.tready <= 1'b0;
            hssi_reset(CHANNEL_ID);
        end
        else
        begin
            // Receive new TLP messages from host
            get_rx_hssi_messages();

            // Send TLP messages to host
            if (data_tx.tx.tvalid && data_tx.tready)
            begin
                send_tx_hssi_messages();
            end

            // Can the host receive a message next cycle?
            data_tx.tready <= hssi_stream_afu_to_host_tready(cycle_counter, CHANNEL_ID) &&
                                           !reset_lockdown;
        end
    end

    assign fc.rx_pause = fc.tx_pause;
    assign fc.rx_pfc = fc.tx_pfc;

endmodule // ase_hssi_emulator
