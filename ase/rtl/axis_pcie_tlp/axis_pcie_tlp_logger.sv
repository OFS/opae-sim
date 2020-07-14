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

module axis_pcie_tlp_logger
  #(
    parameter LOGNAME   = "CHANGE_MY_NAME.log"
    )
   (
    // Configure enable
    input logic finish_logger,
    input logic stdout_en,
    // Buffer message injection
    input logic log_timestamp_en,
    input logic log_string_en,
    ref string 	log_string,

    input logic clk,
    input logic SoftReset
    );

   // Log file descriptor
   int log_fd;

   // Reset management
   logic SoftReset_q;

   string softreset_str;

   // Registers for comparing previous states
   always @(posedge clk) begin
      SoftReset_q <= SoftReset;
   end

   /*
    * FUNCTION: print_and_post_log wrapper function to simplify logging
    */
   function void print_and_post_log(string formatted_string);
      begin
	 if (stdout_en)
	   $display(formatted_string);
	 $fwrite(log_fd, formatted_string);
	 $fflush();
      end
   endfunction // print_and_post_log

   /*
    * Watcher process
    */
   initial begin : logger_proc
      // Display
      $display("  [SIM]  Transaction Logger started");

      // Open transactions.tsv file
      log_fd = $fopen(LOGNAME, "w");

      // Watch CCI port
      forever begin
	 // -------------------------------------------------- //
	 // Indicate Software controlled reset
	 // -------------------------------------------------- //
	 if (SoftReset_q != SoftReset) begin
	    $sformat(softreset_str,
		     "%d\tSoftReset toggled from %b to %b\n",
		     $time,
		     SoftReset_q,
		     SoftReset);
	    print_and_post_log(softreset_str);
	 end

	 // -------------------------------------------------- //
	 // Buffer messages
	 // -------------------------------------------------- //
	 if (log_string_en) begin
	    if (log_timestamp_en) begin
	       $fwrite(log_fd, "-----------------------------------------------------\n");
	       $fwrite(log_fd, "%d\t%s\n", $time, log_string);
	    end
	    else begin
	       $fwrite(log_fd, "-----------------------------------------------------\n");
	       $fwrite(log_fd, "%s\n", log_string);
	    end
	 end

	 // -------------------------------------------------- //
	 // FINISH command
	 // -------------------------------------------------- //
	 if (finish_logger == 1) begin
	    $fclose(log_fd);
	 end

	 // -------------------------------------------------- //
	 // Wait till next clock
	 // -------------------------------------------------- //
	 $fflush(log_fd);
	 @(posedge clk);
      end
   end

endmodule
