//
// Copyright (c) 2021, Intel Corporation
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

#include <assert.h>

#include "hssi_stream.h"
#include "hssi_plugin_api.h"

static FILE *logfile;

static bool in_reset[MAX_CHANNELS];

t_ase_hssi_param_cfg hssi_param_cfg;

// ========================================================================
//
//  Utilities
//
// ========================================================================

static unsigned long next_rand = 1;
static bool did_rand_init = false;
static bool unlimited_bw_mode = false;

// Local repeatable random number generator
static int32_t hssi_rand(void)
{
    if (!did_rand_init)
    {
        did_rand_init = true;
        unlimited_bw_mode = (getenv("ASE_UNLIMITED_BW") != NULL);
    }

    if (unlimited_bw_mode) return 0;

    next_rand = next_rand * 1103515245 + 12345;
    return ((uint32_t)(next_rand/65536) % 32768);
}

// ========================================================================
//
//  Logging
//
// ========================================================================

// Print a full tdata payload if n_dwords is 0
static void fprintf_hssi_bitvec(FILE *stream, const svBitVecVal *payload, int n_dwords)
{
    if (n_dwords == 0)
    {
        n_dwords = hssi_param_cfg.tdata_width_bits / 32;
    }

    fprintf(stream, "0x");
    for (int i = n_dwords - 1; i >= 0; i -= 1)
    {
        uint32_t dw;
        svGetPartselBit(&dw, payload, i * 32, 32);
        if ((i & 1) && (i != n_dwords - 1)) fprintf(stream, "_");
        fprintf(stream, "%08x", dw);
    }
}

void fprintf_hssi_afu_to_host(
    FILE *stream,
    long long cycle,
    int chan,
    bool eop,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    fprintf(stream, "HSSI TX chan[%d]: %lld (hssi clock cycles!) %s ", chan, cycle, (eop ? "eop" : "   "));

    fprintf(stream, " ");
    fprintf_hssi_bitvec(stream, tdata, 0);
    fprintf(stream, " tkeep ");
    fprintf_hssi_bitvec(stream, tkeep, hssi_param_cfg.tdata_width_bits / (32 * 8));

    fprintf(stream, "\n");
    fflush(stream);
}

void fprintf_hssi_host_to_afu(
    FILE *stream,
    long long cycle,
    int chan,
    bool eop,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    fprintf(stream, "HSSI RX chan[%d]: %lld (hssi clock cycles!) %s ", chan, cycle, (eop ? "eop" : "   "));

    fprintf(stream, " ");
    fprintf_hssi_bitvec(stream, tdata, 0);
    fprintf(stream, " tkeep ");
    fprintf_hssi_bitvec(stream, tkeep, hssi_param_cfg.tdata_width_bits / (32 * 8));

    fprintf(stream, "\n");
    fflush(stream);
}

// ========================================================================
//
//  DPI-C methods that communicate with the SystemVerilog simulation
//
// ========================================================================

int hssi_param_init(const t_ase_hssi_param_cfg *params)
{
    memcpy((char *)&hssi_param_cfg, (const char *)params, sizeof(hssi_param_cfg));

    return 0;
}
                                                       
int hssi_reset(int chan)
{
    if (chan >= MAX_CHANNELS)
        return 0;
    
    if (!in_reset[chan])
    {
        in_reset[chan] = true;
    } else {
        return 0;
    }

    hssi_plugin_reset(chan);
    return 0;
}
                                                       
//
// Get a host->AFU HSSI message for a single channel. Called once per
// cycle via DPI-C for each HSSI channel.
//
int hssi_stream_host_to_afu(
    long long cycle,
    int chan,
    int *tvalid,
    int *tlast,
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
)
{
    if (chan >= MAX_CHANNELS) {
        *tvalid = 0;
        return 0;
    }
    in_reset[chan] = false;

    hssi_plugin_set_next_rx(cycle, chan, tvalid, tlast, tdata, tuser, tkeep);

    if (*tvalid)
        fprintf_hssi_host_to_afu(logfile, cycle, chan, *tlast, tdata, tuser, tkeep);

    return 0;
}

//
// Receive an AFU->host HSSI message for a single channel. Called only
// when a channel has valid data.
//
int hssi_stream_afu_to_host(
    long long cycle,
    int chan,
    int tvalid,
    int tlast,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
)
{
    if (chan >= MAX_CHANNELS)
        return 0;
    if (!tvalid)
        return 0;

    hssi_plugin_get_next_tx(cycle, chan, tvalid, tlast, tdata, tuser, tkeep);

    fprintf_hssi_afu_to_host(logfile, cycle, chan, tlast, tdata, tuser, tkeep);

    return 0;
}


//
// Get the next cycle's tready state for the AFU to host stream.
//
int hssi_stream_afu_to_host_tready(
    long long cycle,
    int chan
)
{
    // Random back-pressure and available tags
    return ((hssi_rand() & 0xff) < 0xf0);
}

//
// Open a log file. The SystemVerilog HSSI emulator and the C code share the
// same log file.
//
int hssi_open_logfile(
    const char *logname
)
{
    logfile = fopen(logname, "w");
    if (logfile == NULL)
    {
        fprintf(stderr, "Failed to open log file: %s\n", logname);
        logfile = stdout;
    }
    return logfile ? 0 : 1;
}

//
// Write a message to the shared log file.
//
int hssi_write_logfile(
    const char *msg
)
{
    fprintf(logfile, "%s", msg);
    fflush(logfile);
    return 0;
}
