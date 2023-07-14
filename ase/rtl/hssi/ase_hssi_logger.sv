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

module ase_hssi_logger
  #(
    parameter LOGNAME   = "log_ase_hssi_events.log"
    )
   (
    // Configure enable
    input logic finish_logger,
    input logic stdout_en,
    // Buffer message injection
    input logic log_timestamp_en,
    input logic log_string_en,
    ref string  log_string,

    input logic clk,
    input logic SoftReset
    );

    import "DPI-C" context function void hssi_open_logfile(string logname);
    import "DPI-C" context function void hssi_write_logfile(string msg);

    // Reset management
    logic SoftReset_q;

    string msg;

    // Registers for comparing previous states
    always @(posedge clk) begin
        SoftReset_q <= SoftReset;
    end

    /*
     * FUNCTION: print_and_post_log wrapper function to simplify logging
     */
    function void print_and_post_log(string formatted_string);
        if (stdout_en)
          $display(formatted_string);
        hssi_write_logfile(formatted_string);
    endfunction // print_and_post_log

    /*
     * Watcher process
     */
    initial begin : logger_proc
        // Display
        $display("  [SIM]  HSSI Transaction Logger started");

        // Open transactions.tsv file
        hssi_open_logfile(LOGNAME);

        forever begin
            // -------------------------------------------------- //
            // Indicate Software controlled reset
            // -------------------------------------------------- //
            if (SoftReset_q != SoftReset) begin
                $sformat(msg,
                         "%d\tSoftReset toggled from %b to %b\n",
                         $time,
                         SoftReset_q,
                         SoftReset);
                print_and_post_log(msg);
            end
            
            // -------------------------------------------------- //
            // Buffer messages
            // -------------------------------------------------- //
            if (log_string_en) begin
                if (log_timestamp_en) begin
                    $sformat(msg, "-----------------------------------------------------\n%d\t%s\n", $time, log_string);
                end
                else begin
                    $sformat(msg, "-----------------------------------------------------\n%s\n", log_string);
                end
                hssi_write_logfile(msg);
            end

            @(posedge clk);
        end
    end

endmodule
