#include "hssi_plugin_api.h"
#include "hssi_stream.h"

// Since RX HSSI ignores in ready we can always send data
// => realistically we only need a FIFO of size 2
#define LOOPBACK_FIFO_SIZE 4

// ========================================================================
//
//  Context and general functions
//
// ========================================================================

//
// Context for each chanel
//
typedef struct hssi_chan_context_s
{
    // FIFOs with data
    // FIFOs are filled via TX and read by RX
    int tlast_fifo[LOOPBACK_FIFO_SIZE];
    char *tdata_fifo[LOOPBACK_FIFO_SIZE];
    char *tuser_fifo[LOOPBACK_FIFO_SIZE];
    char *tkeep_fifo[LOOPBACK_FIFO_SIZE];
    int sptr, eptr;
} hssi_chan_context_t;

hssi_chan_context_t chan_context[MAX_CHANNELS];

void hssi_plugin_reset(int chan)
{
    for (int i=0; i<LOOPBACK_FIFO_SIZE; i++) {
        chan_context[chan].tlast_fifo[i] = 0;
        chan_context[chan].tdata_fifo[i] = (char *)ase_malloc(hssi_param_cfg.tdata_width_bits/8);
        chan_context[chan].tuser_fifo[i] = (char *)ase_malloc(hssi_param_cfg.tuser_width_bits/8);
        chan_context[chan].tkeep_fifo[i] = (char *)ase_malloc(hssi_param_cfg.tdata_width_bits/(8*8));
    }
    chan_context[chan].sptr = 0;
    chan_context[chan].eptr = 0;
}

// ========================================================================
//
//  RX side functions
//
// ========================================================================
int hssi_plugin_set_next_rx(
    long long cycle,
    int chan,
    int *tvalid,
    int *tlast,
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
)
{
    *tvalid = 0;

    // Get and update fifo start pointer
    int i = chan_context[chan].sptr;
    // Check empty
    if (i == chan_context[chan].eptr)
        return 0;
    chan_context[chan].sptr = (chan_context[chan].sptr+1)%LOOPBACK_FIFO_SIZE;


    // Get data from the FIFO
    *tvalid = 1;
    *tlast = chan_context[chan].tlast_fifo[i];
    memcpy(tdata, chan_context[chan].tdata_fifo[i], hssi_param_cfg.tdata_width_bits/8);
    memcpy(tkeep, chan_context[chan].tkeep_fifo[i], hssi_param_cfg.tdata_width_bits/(8*8));
    memcpy(tuser, chan_context[chan].tuser_fifo[i], hssi_param_cfg.tuser_width_bits/8);

    return 0;
}

// ========================================================================
//
//  TX side functions
//
// ========================================================================
int hssi_plugin_get_next_tx(
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
    
    // Get and update fifo endpointer
    int i = chan_context[chan].eptr;
    // FIFO will never be full!
    chan_context[chan].eptr = (chan_context[chan].eptr+1)%LOOPBACK_FIFO_SIZE;

    // Store data in the FIFO
    chan_context[chan].tlast_fifo[i] = tlast;
    memcpy(chan_context[chan].tdata_fifo[i], tdata, hssi_param_cfg.tdata_width_bits/8);
    memcpy(chan_context[chan].tkeep_fifo[i], tkeep, hssi_param_cfg.tdata_width_bits/(8*8));
    memcpy(chan_context[chan].tuser_fifo[i], tuser, hssi_param_cfg.tuser_width_bits/8);

    return 0;
}
