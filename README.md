rmc-compiler
=========

Implementation of the Relaxed Memory Calculus atomics extension for C
and C++ (and Rust!).

Disclaimer: this is all very much work in progress research
code. There are a lot of rough edges, some of the code is in need of
some serious cleanup, and the packaging/configuration/documentation
doesn't exist that much.

RMC is an alternate atomics system based around programmers explicitly
specifying visibility order and execution order constraints on memory
action.

Right now the best documentation for the ideas behind the system are
the RMC slides (http://www.msully.net/stuff/rmc-popl-talk.pdf)
and the RMC paper (http://www.cs.cmu.edu/~crary/papers/2015/rmc.pdf),
although the paper is pretty theoretical. The concrete syntax in both
of those is a little out of date, though, so the best documentation
for how the actual concrete syntax works is just the code in the
examples/ directory.


Building
-----------

Build with: ./configure [args] && make

You might need to pass some args to ./configure to tell it where to
find dependencies, as discussed below in the dependencies list.

The dependencies are:
 * LLVM and clang 3.5 or 3.6. A path to a clang build/install
   directory can be provided to ./configure with
   --llvm-location=[path]. If there is an llvm dev environment visible
   on the system (for example, Ubuntu's llvm-3.5-dev package),
   configure will find it automatically.

   The binary clang+llvm OS X packages available from the LLVM website
   don't work for this, unfortunately, because of linking/loading
   problems. You'll need to build from source, making sure to pass
   --enabled-shared when you run llvm's configure script.

 * To use the (recommended) SMT based backend, you need the opt branch
   of Microsoft's Z3 SMT solver, available from
   https://github.com/Z3Prover/z3/tree/opt.

   If Z3 is installed at a nonstandard location, pass its installation
   prefix (the directory where its installed include/ and lib/ directories
   live) to ./configure with --z3-location=[path].
   If you have the master branch installed instead of opt, we can use
   that too, at the cost of performance: pass --disable-z3-opt to
   ./configure.

   To disable the SMT backend, pass --disable-z3 to ./configure.


Installing
-----------
Actually installing into system directories is Coming Soon.
Until then, it can be used from its build directory.


How to use
-----------

The rmc-config script can be used to print the arguments to a compiler
necessary to use RMC.

To build something that uses RMC without running the custom backend
(that is, to use the lower performance fallback), do:
  cc $(path/to/rmc-config --cflags) [other args] file.c

To use the custom RMC backend, do:
  path/to/clang -O $(path/to/rmc-config --cflags --realize) [other args] file.c

The clang used must be built with the same LLVM that the RMC compiler
was built against and optimization must be enabled.

Passing --smt to rmc-config enables the SMT solver based backend and
--cleanup enables a backend optimization cleanup pass that should be
safe to use except on POWER on -O3.

--

The run-rmc script is good for experimenting with RMC. It makes it
easy to target ARM and POWER (at least, if you are on Ubuntu and
install g++-4.9-multilib-arm-linux-gnueabi and
g++-4.9-powerpc-linux-gnu) and see what the code looks like at various
stages in the pipeline. Check it out to see what all it can do.

-- To use RMC with Rust, you need to be running the nightly and add as
a Cargo dependency

```toml
[dependencies.rmc-plugin]
git = "https://github.com/msullivan/rmc-compiler.git"
[dependencies.rmc]
git = "https://github.com/msullivan/rmc-compiler.git"
```

Then you can pull it into you crate with

```rust
#![feature(plugin)]
#![plugin(rmc_plugin)]
#[macro_use] extern crate rmc;
```
