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
#include <pcap.h>

#include "ase_common.h"
#include "hssi_stream.h"

#define MAX_CHANNELS 16

static FILE *logfile;

t_ase_hssi_param_cfg hssi_param_cfg;

static bool in_reset;
static uint64_t cur_cycle;


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

#define hssi_error_and_kill(format, ...) { \
    ASE_ERR(format, __VA_ARGS__); \
    start_simkill_countdown(); \
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
//  General PCAP functions
//
// ========================================================================

//
// Context for each chanel
//
typedef struct hssi_chan_context_s {
    // General PCAP handles
    pcap_t *pcap_in;
    pcap_t *pcap_out;
    pcap_dumper_t *pcap_out_dump;
    char errbuf[PCAP_ERRBUF_SIZE];
    // Current packets
    unsigned rx_len;
    const u_char *rx_packet;
    unsigned tx_len;
    u_char tx_packet[65535];
    // Just some general stats
    unsigned rx_pkts;
    unsigned rx_bytes;
    unsigned tx_pkts;
    unsigned tx_bytes;
} hssi_chan_context_t;

hssi_chan_context_t chan_context[MAX_CHANNELS];

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
    if (!in_reset)
    {
        in_reset = true;
    }

    // Close and clear everything
    if (chan_context[chan].pcap_in != NULL)
        pcap_close(chan_context[chan].pcap_in);
    if (chan_context[chan].pcap_out != NULL)
        pcap_close(chan_context[chan].pcap_out);
    if (chan_context[chan].pcap_out_dump != NULL)
        pcap_dump_close(chan_context[chan].pcap_out_dump);
    memset((char *)&chan_context[chan], 0, sizeof(chan_context[chan]));
    
    // Get the PCAPs dir
    char pcaps_path[2000];
    char *pcaps_path_envvar = "ASE_HSSI_PCAPS_PATH";
    if(!getenv(pcaps_path_envvar))
        hssi_error_and_kill("Environment variable %s not set.\n", pcaps_path_envvar);
    sprintf(pcaps_path, "%s", getenv(pcaps_path_envvar));
    // Open the pcaps
    char str_buff[2048];
    sprintf(str_buff, "%s/in%d.pcap", pcaps_path, chan);
    chan_context[chan].pcap_in = pcap_open_offline(str_buff, chan_context[chan].errbuf);
    sprintf(str_buff, "%s/out%d.pcap", pcaps_path, chan);
    chan_context[chan].pcap_out = pcap_open_dead(DLT_EN10MB, 65535);
    if (chan_context[chan].pcap_out == NULL)
        hssi_error_and_kill("Failed to setup output PCAP - %s\n", str_buff);
    chan_context[chan].pcap_out_dump = pcap_dump_open(chan_context[chan].pcap_out, str_buff);
    if (chan_context[chan].pcap_out_dump == NULL)
        hssi_error_and_kill("Failed to open output PCAP - %s\n", str_buff);

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
    cur_cycle = cycle;
    in_reset = false;

    *tvalid = 0;

    // No PCAP.in => nothing to send
    if (chan_context[chan].pcap_in == NULL)
        return 0;

    if (cur_cycle <= 100000)
        return 0;
    
    // Try to read next packet if we don't have one
    struct pcap_pkthdr *header;
    if (chan_context[chan].rx_packet == NULL) {
        if (pcap_next_ex(chan_context[chan].pcap_in, &header, &(chan_context[chan].rx_packet)) < 0)
            chan_context[chan].rx_packet = NULL;
        else
            chan_context[chan].rx_len = header->len;
    }

    // If there are no packets do nothing
    if (chan_context[chan].rx_packet == NULL)
        return 0;

    // At this point we will have some data
    *tvalid = 1;
    memset(tuser, 0, hssi_param_cfg.tuser_width_bits/8);
    // Entire word
    if (chan_context[chan].rx_len > hssi_param_cfg.tdata_width_bits/8) {
        *tlast = 0;
        memset(tkeep, ~0, hssi_param_cfg.tdata_width_bits/(8*8));
        memcpy(tdata, chan_context[chan].rx_packet, hssi_param_cfg.tdata_width_bits/8);
        chan_context[chan].rx_len -= hssi_param_cfg.tdata_width_bits/8;
        chan_context[chan].rx_packet += hssi_param_cfg.tdata_width_bits/8;
        chan_context[chan].rx_bytes += hssi_param_cfg.tdata_width_bits/8;
    // Last word
    } else {
        *tlast = 1;
        memset(tkeep, 0, hssi_param_cfg.tdata_width_bits/(8*8));
        memset(tkeep, ~0, chan_context[chan].rx_len/8);
        if (chan_context[chan].rx_len % 8)
            ((char *)tkeep)[chan_context[chan].rx_len/8] = ((char)1 << (chan_context[chan].rx_len % 8)) - 1;
        memcpy(tdata, chan_context[chan].rx_packet, chan_context[chan].rx_len);
        chan_context[chan].rx_len = 0;
        chan_context[chan].rx_packet = NULL;
        chan_context[chan].rx_pkts++;
        chan_context[chan].rx_bytes += chan_context[chan].rx_len;
    }

    if (tvalid)
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
    if (!tvalid)
        return 0;
    
    unsigned len = 0;
    for (len = 0; len < hssi_param_cfg.tdata_width_bits/8; len++)
        if (!svGetBitselBit(tkeep, len))
            break;

    memcpy(chan_context[chan].tx_packet+chan_context[chan].tx_len, tdata, len);

    chan_context[chan].tx_len += len;
    chan_context[chan].tx_bytes += len;

    if (tlast) {
        chan_context[chan].tx_pkts++;
        struct pcap_pkthdr header_out;
        // Set PCAP header
        header_out.ts.tv_usec = cycle;
        header_out.caplen = chan_context[chan].tx_len;
        header_out.len = chan_context[chan].tx_len;
        // Dump the packet into PCAP
        pcap_dump((u_char*)chan_context[chan].pcap_out_dump, &header_out, (const u_char*)(chan_context[chan].tx_packet));
        pcap_dump_flush(chan_context[chan].pcap_out_dump);
        chan_context[chan].tx_len = 0;
    }

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
    cur_cycle = cycle;

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
    logfile = fopen(logname, "a");
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
