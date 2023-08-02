#ifndef _HSSI_PLUGIN_API_H_
#define _HSSI_PLUGIN_API_H_

#include "ase_common.h"
#include "hssi_stream.h"

// These is the configuration of the hssi, set and used from hssi_stream.c
extern t_ase_hssi_param_cfg hssi_param_cfg;

// Basic reset function (invoked to reset the emulation)
void hssi_plugin_reset(int chan);

// Basic function that sets the RX values for a given HSSI channel
// (cycle accuracy)
int hssi_plugin_set_next_rx(
    long long cycle,
    int chan,
    int *tvalid,
    int *tlast,
    svBitVecVal *tdata,
    svBitVecVal *tuser,
    svBitVecVal *tkeep
);

// Basic function that receives the TX data from  a given HSSI channel
// (cycle accuracy)
int hssi_plugin_get_next_tx(
    long long cycle,
    int chan,
    int tvalid,
    int tlast,
    const svBitVecVal *tdata,
    const svBitVecVal *tuser,
    const svBitVecVal *tkeep
);

#endif // _HSSI_PLUGIN_API_H_