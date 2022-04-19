## libase plugin

libase is implemented as a plugin for libopae-c following the plugin model as
described [here](http://github.com/OPAE/opae-sdk/pluginis/README.md)

## Building agains OPAE
Building libase assumes a development package is installed on the target build
system. In addition to the runtime libraries, the development package includes
other collateral helpful for development of opae-based solutions. Specifically,
headers and camke configuration files will be needed by this project.
This project includes cmake directive `find_package(opae)` which can find the
cmake configuration files. Cmake's searches common installation prefixes (like
/usr or /usr/local) when finding opae package. If opae development files are
installed at a non-standard prefix, one may help cmake find it by setting one
of two environment variables as described below.
* opae_DIR : points to opae's cmake configuration files installed in
`{prefix}/lib/opae-{opae_version}`
* CMAKE_PREFIX_PATH : points to an installation prefix.


Example:
OPAE 2.0.1 is installed using prefix /some/arbitrary/path.
Then, to configure cmake for this project, run:

Using opae_DIR:
```bash
> git clone https://github.com/OPAE/opae-sim.git
> cd opae-sim
> mkdir build
> opae_DIR=/some/arbitrary/path/lib/opae-2.0.1 cmake ..
> make
```


Using CMAKE_PREFIX_PATH:
```bash
> git clone https://github.com/OPAE/opae-sim.git
> cd opae-sim
> mkdir build
> CMAKE_PREFIX_PATH=/some/arbitrary/path cmake ..
> make
```

