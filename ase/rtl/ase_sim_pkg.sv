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
// Protocol-independent ASE driver. Import this from a module that implements
// protocol-specific simulation.
//

package ase_sim_pkg;

    import ase_pkg::*;

    // ASE Initialize function
    import "DPI-C" context task ase_init();

    // Indication that ASE is ready
    import "DPI-C" function void ase_ready();

    // ASE instance check
    import "DPI-C" function int ase_instance_running();

    // Software controlled reset response
    import "DPI-C" function void sw_reset_response();

    // Global dealloc allowed (system idle) flag
    import "DPI-C" function void update_glbl_dealloc(int flag);

    // CONFIG, SCRIPT DEX operations
    import "DPI-C" function void sv2c_config_dex(string str);
    import "DPI-C" function void sv2c_script_dex(string str);

    // ASE config data exchange (read from ase.cfg)
    export "DPI-C" task ase_config_dex;

    // Ready PID
    int ase_ready_pid;

    // Simulation idle flag -- no transactions in flight. This must be set
    // by the protocol-specific parent module when it is safe to trigger
    // system reset.
    logic system_is_idle;

    // Primary clock -- set by parent module
    logic clk;

    // Advance num_clks cycles
    task advance_clock(int num_clks);
        for (int clk_iter = 0; clk_iter <= num_clks; clk_iter = clk_iter + 1)
        begin
            @(posedge clk);
        end
    endtask

    // Reset management
    logic SoftReset = 1;
    logic ase_reset = 1;	// System reset
    logic sw_reset_trig = 1;	// Software-triggered reset
    logic init_reset = 1;
    logic app_reset_trig, app_reset_trig_q;

    // Stop taking any more requests if reset was requested
    logic reset_lockdown = 0;

    // AFU Soft Reset Trigger
    task afu_softreset(int init, int value);
        if (init) begin
            app_reset_trig = 0;
        end
        else begin
            app_reset_trig = value[0];
        end
    endtask

    /*
     * ASE Simulator reset
     * - Use sparingly, only for initialization and reset between session_init(s)
     */
    task system_reset_trig();
        reset_lockdown = 1;
        wait (system_is_idle);
        @(posedge clk);
        ase_reset = 1; 
        advance_clock(20);
        ase_reset = 0;
        advance_clock(20);
        reset_lockdown = 0;
    endtask

    // Reset states
    typedef enum {
        ResetIdle,
        ResetHoldHigh,
        ResetHoldLow,
        ResetWait
    } ResetStateEnum;

    ResetStateEnum rst_state;
    int rst_timeout_counter;
    int rst_counter;

    // Reset FSM -- invoked by ase_sim_every_cycle() below
    task ase_reset_fsm();
        app_reset_trig_q <= app_reset_trig;

        if (ase_reset)
        begin
            rst_state           <= ResetIdle;
            rst_counter         <= 0;
            rst_timeout_counter <= 0;
        end
        else 
        begin
            case (rst_state)
            ResetIdle:
                begin
                    rst_timeout_counter <= 0;
                    // 0 -> 1
                    if (~app_reset_trig_q && app_reset_trig)
                    begin
                        rst_state <= ResetHoldHigh;
                    end
                    // 1 -> 0
                    else if (app_reset_trig_q && ~app_reset_trig)
                    begin
                        rst_state <= ResetHoldLow;
                    end
                    else
                    begin
                        rst_state <= ResetIdle;
                    end
                end

            // Set to 1
            ResetHoldHigh:
                begin
                    if (!system_is_idle)
                    begin
                        if (rst_timeout_counter < `RESET_TIMEOUT_DURATION)
                        begin
                            rst_timeout_counter <= rst_timeout_counter + 1;
                            rst_state           <= ResetHoldHigh;
                        end
                        else
                        begin
                            `BEGIN_RED_FONTCOLOR;
                            $display("  [SIM]  Reset request timed out... Behavior maybe undefined !");
                            `END_RED_FONTCOLOR;
                            sw_reset_trig <= 1;
                            rst_state <= ResetWait;
                        end
                    end
                    else
                    begin
                       sw_reset_trig <= 1;
                       rst_state <= ResetWait;
                    end
                end

            // Set to 0
            ResetHoldLow:
                begin
                    sw_reset_trig <= 0;
                    rst_state <= ResetWait;
                end

            // Wait few cycles
            ResetWait:
                begin
                    if (rst_counter < `SOFT_RESET_DURATION)
                    begin
                        rst_counter <= rst_counter + 1;
                        rst_state <= ResetWait;
                    end
                    else begin
                       rst_counter <= 0;
                       rst_state <= ResetIdle;
                       sw_reset_response();
                    end
                 end

            default:
                begin
                    rst_state <= ResetIdle;
                end

            endcase
        end
    endtask // ase_reset_fsm


    /* ******************************************************************
     *
     * Config data exchange - Supplied by ase.cfg
     * Configuration of ASE managed by a text file, modifiable runtime
     *
     * *****************************************************************/
    task ase_config_dex(ase_cfg_t cfg_in);
    begin
        // Cfg transfer
        cfg.ase_mode                 = cfg_in.ase_mode                 ;
        cfg.ase_timeout              = cfg_in.ase_timeout              ;
        cfg.ase_num_tests            = cfg_in.ase_num_tests            ;
        cfg.enable_reuse_seed        = cfg_in.enable_reuse_seed        ;
        cfg.ase_seed                 = cfg_in.ase_seed                 ;
        cfg.enable_cl_view           = cfg_in.enable_cl_view           ;
        cfg.usr_tps                  = cfg_in.usr_tps                  ;
        cfg.phys_memory_available_gb = cfg_in.phys_memory_available_gb ;
    end
    endtask


    /*
     * Multi-instance multi-user +CONFIG,+SCRIPT instrumentation
     * RUN =>
     * cd <work>
     * ./<simulator> +CONFIG=<path_to_cfg> +SCRIPT=<path_to_run_SEE_README>
     *
     */
    string config_filepath;
    string script_filepath;
    task config_filepath_init();
`ifdef ASE_DEBUG
        if ($value$plusargs("CONFIG=%S", config_filepath))
        begin
           `BEGIN_YELLOW_FONTCOLOR;
           $display("  [DEBUG]  Config = %s", config_filepath);
           `END_YELLOW_FONTCOLOR;
        end

        if ($value$plusargs("SCRIPT=%S", script_filepath))
        begin
        `BEGIN_YELLOW_FONTCOLOR;
         $display("  [DEBUG]  Script = %s", script_filepath);
        `END_YELLOW_FONTCOLOR;
        end
`else
        $value$plusargs("CONFIG=%S", config_filepath);
        $value$plusargs("SCRIPT=%S", script_filepath);
`endif
    endtask // conig_filepath_init


    /*
     * Initialization procedure
     *
     * DESCRIPTION: This procedural block is called when ./simv is
     *              kicked off, helps put the simulation in a known
     *              state.
     *
     * STEPS:
     * - Print startup info
     * - Send initial system reset, cleaning up state machines
     * - Initialize ASE (ase_init executes in SW)
     *   - Set up message queues for IPC (done in SW)
     *   - Set up memory management structure (called in SW)
     * - If ENABLED, start the CA-private memory region (emulated with
     *   software
     * - Then set up the QLP InitDone signal to go indicate readiness
     * - SIMULATION is ready to begin
     *
     */
    task ase_sim_init();
        init_reset <= 1;

        $display("  [SIM]  Simulator started...");

        // Check if simulator is already running in this directory:
        // If YES, kill simulator, post message
        // If NO, continue
        ase_ready_pid = ase_instance_running();
        if (ase_ready_pid != 0) begin
           `BEGIN_RED_FONTCOLOR;
           $display("  [SIM]  An ASE instance is probably still running in current directory !");
           $display("  [SIM]  Check for PID %d", ase_ready_pid);
           $display("  [SIM]  Simulation will exit... you may use a SIGKILL to kill the simulation process.");
           $display("  [SIM]  Also check if '.ase_ready.pid' file is removed before proceeding.");
           `END_RED_FONTCOLOR;
           $finish;
        end

        // Globally write CONFIG, SCRIPT paths
        config_filepath_init();
        if (config_filepath.len() != 0) begin
            sv2c_config_dex(config_filepath);
        end
        if (script_filepath.len() != 0) begin
            sv2c_script_dex(script_filepath);
        end

        // Read seed and print
        $display("  [SIM]  ASE running with seed => %d", cfg.ase_seed);
        // $srandom(cfg.ase_seed);
        // $urandom(cfg.ase_seed);

        // Initialize SW side of ASE
        ase_init();

        init_reset <= 0;

        // AFU reset
        afu_softreset(1, 0);

        // Initial signal values
        $display("  [SIM]  Sending initial reset...");
        system_reset_trig();

        sw_reset_trig <= 0;
        advance_clock(20);

        // Indicate to APP that ASE is ready
        ase_ready();
    endtask // ase_sim_init

    logic system_is_idle_q;

    //
    // Management task -- this must be invoked every cycle.
    //
    task ase_sim_every_cycle();
        SoftReset <= ase_reset | sw_reset_trig;
        ase_reset_fsm();

        // Track changes to system_is_idle and inform the SW.
        system_is_idle_q <= system_is_idle;
        if (system_is_idle != system_is_idle_q)
        begin
            update_glbl_dealloc(system_is_idle);
        end

        if (ase_reset)
        begin
            system_is_idle_q <= 1'b0;
        end
    endtask // ase_sim_every_cycle

endpackage // ase_sim_pkg
