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


## libase plugin
libase is implemented as a plugin for libopae-c following the plugin model as
described [here](http://github.com/OPAE/opae-sdk/pluginis/README.md)


## libopae-c-ase
libopae-c-ase is an OPAE API implementation that registers the ase plugin so
that it is loaded automatically. This library is created by using
`opae_add_shared_plugin` cmake function that dynamically generates source code
and calls required directives to generate libopae-c-ase as described above.

## Building against OPAE
Building libase assumes a development package is installed on the target build
system. In addition to the runtime libraries, the development package includes
other collateral helpful for development of OPAE-based solutions. Specifically,
headers and CMake configuration files will be needed by this project.
This project includes the CMake directive `find_package(opae)` which can find the
CMake configuration files. CMake searches common installation prefixes (like
/usr or /usr/local) when finding the OPAE package. If OPAE development files are
installed at a non-standard prefix, one may help CMake find it by setting a cmake/environment variable as described below.
* CMAKE_INSTALL_PREFIX : cmake variable that points to an installation prefix and will also control
where ASE libraries are installed. Use this option when OPAE is installed under
the same prefix where you wish to install ASE artifacts.
* CMAKE_PREFIX_PATH : Environment variable that points to an installation prefix. This only helps to find OPAE.
* opae_DIR : Environment variable that points to OPAE's CMake configuration files installed in
`{prefix}/lib/opae-{opae_version}`. This only helps to find OPAE.


Example:
OPAE 2.0.1 is installed using prefix /some/arbitrary/path.
Then, to configure CMake for this project, run:

Using CMAKE_INSTALL_PREFIX cmake variable to find OPAE and install ASE into the
same prefix.
```bash
> git clone https://github.com/OPAE/opae-sim.git
> cd opae-sim
> mkdir build
> cmake -DCMAKE_INSTALL_PREFIX=/some/arbitrary/path ..
> make
```

Using CMAKE_PREFIX_PATH environment variable:
```bash
> git clone https://github.com/OPAE/opae-sim.git
> cd opae-sim
> mkdir build
> CMAKE_PREFIX_PATH=/some/arbitrary/path cmake ..
> make
```

Using opae_DIR environment variable:
```bash
> git clone https://github.com/OPAE/opae-sim.git
> cd opae-sim
> mkdir build
> opae_DIR=/some/arbitrary/path/lib/opae-2.0.1 cmake ..
> make
```
