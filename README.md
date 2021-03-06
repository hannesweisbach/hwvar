# Hardware Performance Varianten Measurement Utility

Hardware Performance Variation is usually measured using the FTQ/FWQ benchmark.
We argue these measurements are nonsense since they do not load the CPU. "Work"
in FWQ refers to a fixed number of iterations of an empty loop. This effectively
measures the difference in clockspeed of processors.

This framework aims to put small kernels from proxy application and benchmarks
into the work part with the goal of creating a realistic performance variation
profile of a particular platform. Only when execution units are loaded can a
realistic measurement of performance variation be taken.

We provide a set of benchmarks kernels intended to test a variety of instruction
mixes, data access patterns, and instruction-per-byte-ratios.

### FWQ

From the FTQ/FWQ benchmark from the Sequioa benchmark suite. Currently only the
integer increment "work" is available.

### DGEMM

Dense matrix multiplication benchmark from the HPCC benchmark suite. This
benchmark is supposed to measure floating point performance. 

### STREAM

This is John McCalpin's STREAM benchmark kernel, intended to test memory
bandwidth. We use it to test the performance variation of memory access
instructions.

### HACCmk

This is the HACCmk benchmark from the CORAL benchmark suite. It's a
compute-intensive floating point kernel, using additional operations than DGEMM
alone. If the platform (compiler) supports vectorization, this benchmark can
also test the influence of vector instructions on performance variantion.

### SHA256

The SHA256 hash algorithm is used as an integer-only kernel.

### HPCCG

High Performance Computing Conjugate Gradients mini app from the Mantevo
benchmark suite.  Time measurement code and any I/O have been removed from the
benchmark kernel.

### MiniFE

The MiniFE mini app from the Mantevo benchmark suite. Time measurement and I/O
code has been removed from the benchmark kernel.

### Capacity

The capacity benchmark accesses each cache line of an array of twice the
working set size alternating between reads and writes. The intention is to
deterministically cause cache misses and measure the number of cache misses and
the performance variation caused by cache misses.

## Benchmark setup and options

The `--size` option specifies the desired working set size, the L1 cache size
as determined by hwloc is used as the default. The benchmark is tuned to use
90% of the given capacity, so that spare capacity is available for stack data,
etc. The size option is interpreted in bytes. The suffixes `kK`, `mM`, and `gG`
are supported for kibi, mebi and gibi. The default fill factor of 0.9 can be
adjusted with the `--fill` option, which expects a fractional value. For
example to use the entire cache pass 1 or 1.0 and to use 150% of the cache size
use 1.5. Because the benchmark might not be able to use the selected size
completely, the actual size and percentage of the selected size are printed.

Each benchmark is responsible for finding appropriate parameters to fulfill the
size requirement.

With the problem size fixed via the `--size` option, the benchmark runtime can
be adjusted with the `--<benchmark name>-rounds` option. The rounds option
determines how often a benchmark kernel is called in a loop  to make up one
sample. This way it is possible to run the benchmark for a fixed amount of
work.

The `--time` options sets the desired runtime in seconds. The default is 20s.
With the working set size set, the benchmark is run 10 times to get an
approximate runtime for a single invocation. This value is then used to
approximate the number of invocation required to get the selected aggregate
runtime.

If the benchmark should run for an (approximate) amount of wall clock time, the
`--tune` and `--auto` options can be used. The `--tune` options estimates and
prints the required number of rounds to approximately achieve a wall clock
runtime specified with the `--time` parameter. The `--auto` parameter
additionally runs the benchmark(s) with the estimated rounds values.

The `--time` option takes only effect when used with the `--tune` or `--auto`
option.

## Additional Performance Counters

Additional platform-specific performance counters can be samples with the
`--pmcs` options. The option takes a comma-separated list of
(architecture-specific) event names.

### x86_64

On x86_64 performance counters are accessed using Andi Kleen's
[pmu-tools/libjevents](https://github.com/andikleen/pmu-tools). The
`listevents` command gives you a list of available performance counters for
your processor. Ask Intel what mean and if they actually work and/or count what
you think they count and/or what Intel documented they should count.

### AArch64

If you implementation supports ARM PMUv3 you can currently specificy the
follwoing events:

- L1I_CACHE_REFILL
- L1D_CACHE_REFILL
- L1D_CACHE
- MEM_ACCESS
- L2D_CACHE
- L2D_CACHE_REFILL

### Sparc64

Not implemented/supported.

## Building

Currently autotools and CMake are supported. For boths build systems out-of-tree
builds should be used. Tested compilers included gcc, clang and icc.

The MiniFE and HPCCG kernel are currently referenced via git submodules. After
checkout issue `git submodule update --init` to check them out and update them.
CMake checks for this automatically at configuration time and performs the call
to git submodule, if necessary.

The benchmark suite depends on the hwloc library. If hwloc is not available in a
standard directory, it has to made available manually.

Don't forget to add optimization flags specific to your compiler, unless you
want to benchmark unoptimized code!

### Examples Compiling with Fujitsu compiler

For example, to compile the benchmark suite using fcc and hwloc installed in
`~/install/hwloc` issue the following commands:

```
export HWLOC_DIR=$HOME/install/hwloc
export LD_LIBRARY_PATH=${HWLOC_DIR}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export CPPFLAGS="-I${HWLOC_DIR}/include/"
export LDFLAGS="-L${HWLOC_DIR}/lib/"
export CFLAGS="-Xg -std=gnu99 -Xg -Kfast"
export CC=/opt/FJSVmxlang/GM-2.0.0-05/bin/fccpx 
export CXX=/opt/FJSVmxlang/GM-2.0.0-05/bin/FCCpx

<path to source>/configure --build=sparc64-unknown-linux-gnu --host=sparc64-unknown-linux-gnu
```

Or, using cmake (or cmake3, depending on your distro):

```
export HWLOC_DIR=$HOME/install/hwloc
export CFLAGS="-Xg -std=gnu99 -Kfast"
export CXXFLAGS="-Xg -Kfast"
cmake <path to source> -DCMAKE_C_COMPILER=<path to fcc> -DCMAKE_CXX_COMPILER=<path to f++>
```

or:

```
CFLAGS="-Xg -std=gnu99 -Kfast" CXXFLAGS="-Xg -Kfast" cmake <path to source>
  -DCMAKE_C_COMPILER=<path to fcc> -DCMAKE_CXX_COMPILER=<path to f++>
  -DHWLOC_DIR=<path to hwloc> -DCMAKE_BUILD_TYPE=Release
```

### Examples Compiling with gcc

```
module load gcc
Module gcc/7.1.0 loaded.
export CFLAGS="-O3 -ffast-math -ftree-vectorize"
export CXXFLAGS="-O3 -ffast-math -ftree-vectorize"
cmake ../hwvar/ -DCMAKE_BUILD_TYPE=Release
```

### Examples Compiling with icc

```
module load intel
# Module intel/2018.1.163 loaded.
module load gcc
# Module gcc/7.1.0 loaded.
module load hwloc
# Module hwloc/1.11.6 loaded.
export CFLAGS="-ipo -O3 -no-prec-div -fp-model fast=2 -xHost -ip"
export CXXFLAGS="-ipo -O3 -no-prec-div -fp-model fast=2 -xHost -ip"
CC=icc CXX=icpc cmake <path to source> -DCMAKE_BUILD_TYPE=Release
```

```
module load intel/12.1
# Module intel/12.1 loaded.
# Load a compatible gcc version.
# I don't know what that version is for your system.
module load hwloc
# Module hwloc/1.11.6 loaded.
export CFLAGS="-ipo -O3 -no-prec-div -fp-model fast=2 -xHost -ip"
export CXXFLAGS="-ipo -O3 -no-prec-div -fp-model fast=2 -xHost -ip"
CC=icc CXX=icpc cmake <path to source> -DCMAKE_AR:STRING=xiar -DCMAKE_BUILD_TYPE=Release
```

### Examples Compiling with xlc

```
CFLAGS=-O5
CXXFLAGS=-O5
CC=xlc_r CXX=xlC_r cmake <path to source>
```

### Building for BlueGene/Q

You probably have to compile hwloc for the BG/Q, but that works out of the box:

```
export CC=mpicc
export CXX=mpicxx
./configure --prefix=<install path> --disable-shared --enable-static --host=powerpc64-bgq-linux
```

`mpicc` and `mpicxx` are MPI-wrappers around gcc or xlc, depending on which
compiler you loaded. With hwloc compiled and installed proceed to compile HWPerfVar:

```
export CFLAGS="-O5"
export CXXFLAGS="-O5"
export HWLOC_DIR=<path to hwloc install>
cmake -DCMAKE_SYSTEM_NAME=BlueGeneQ-static <path to source>
```

You should probably use the same compiler for hwloc and HWPerfVar. You can
probably also use BlueGeneQ-dynamic, but I haven't tested it. In the scripts
subdirectory there's a `BlueGeneQ-build.sh` script showcasing how to compile
for BG/Q.
