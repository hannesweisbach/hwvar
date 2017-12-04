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

## Benchmark setup and options

Each of the benchmarks can individually be tuned for the working set size of its
data and the runtime. Because benchmarks kernels have different notions of
problem size, each benchmark has a `--<benchmark name>-size` option. The
`--<benchmark name>-rounds` option gives the number of times a benchmark kernel
is called to achieve the desired overall runtime.

The `--tune` options determines/estimates the numbers for the options
automatically. To also run the benchmarks with the calculated problem size use
the `--auto` option.

The `--size` option specifies the desired working set size, the L1 cache size as
determined by hwloc is used as the default. The benchmark is tuned to use 90% of
the given capacity, so that spare capacity is available for stack data, etc. The
size option is interpreted in bytes. The suffixes `kK`, `mM`, and `gG` are
supported for kibi, mebi and gibi. Because the benchmark might not be able to
use the selected size completely, the actual size and percentage of the selected
size are printed.

The `--time` options sets the desired runtime in seconds. The default is 20s.
With the working set size set, the benchmark is run 10 times to get an
approximate runtime for a single invocation. This value is then used to
approximate the number of invocation required to get the selected aggregate
runtime.

The `--size` and `--time` options take only effect when used with the `--tune`
or `--auto` option.

## Building

Currently autotools and CMake are supported. For boths build systems out-of-tree
builds should be used.

The MiniFE and HPCCG kernel are currently referenced via git submodules. After
checkout issue `git submodule update --init` to check them out and update them.
CMake checks for this automatically at configuration time and performs the call
to git submodule, if necessary.

The benchmark suite depends on the hwloc library. If hwloc is not available in a
standard directory, it has to made available manually.

For example, to compile the benchmark suite using fcc and hwloc installed in
`~/install/hwloc` issue the following commands:

```
export HWLOC_DIR=$HOME/install/hwloc
export LD_LIBRARY_PATH=${HWLOC_DIR}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export CPPFLAGS="-I${HWLOC_DIR}/include/"
export LDFLAGS="-L${HWLOC_DIR}/lib/"
export CFLAGS="-Xg -std=gnu99 -Xg -Kfast"
export CC=/opt/FJSVmxlang/GM-2.0.0-05/bin/fcc 
export CXX=/opt/FJSVmxlang/GM-2.0.0-05/bin/fcc (or fpp, or f++?)

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
  -DHWLOC_DIR=<path to hwloc>
```

Don't forget to add optimization flags specific to your compiler, unless you
want to benchmark unoptimized code!
