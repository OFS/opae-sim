# opae-sim

The Application Simulation Environment (ASE) is an RTL simulator for OPAE-based AFUs. The
simulator emulates both the OPAE SDK software user space API and the AFU RTL interface.
The majority of the FIM as well as devices such as PCIe and local memory are emulated
with simple functional models, mostly written in C. As a result, ASE runs far faster than
typical UVM models. Production software intended to run with hardware can be
linked with ASE's OPAE SDK emulator and used to drive the AFU.

ASE can emulate only a subset of possible hardware configurations. The following are its
major limitations:

* MMIO reads and writes must use the OPAE SDK API's methods. Direct software access to
MMIO space using pointers will not be detected by the ASE software runtime and will not
generate PCIe transactions.
* Only host channels and local memory are emulated. Devices such as Ethernet are not yet
supported.
* While accelerators with multiple AFU host channels can be run with ASE, only one port
will receive transactions. All MMIO traffic will be directed to a single AFU port.

To build and install ASE please follow the instructions in the [README](ase/README.md).
