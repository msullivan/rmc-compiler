rmc-compiler
=========

Implementation of the Relaxed Memory Calculus atomics extension for C
and C++.

Disclaimer: this is all very much work in progress research
code. There are a lot of rough edges, some of the code is in need of
some serious cleanup, and the packaging/configuration/documentation
doesn't exist that much.

RMC is an alternate atomics system based around programmers explicitly
specifying visibility order and execution order constraints on memory
action.

Right now the best documentation for RMC is (sorry) Chapter 3 of my
[thesis proposal] (https://www.msully.net/stuff/thesprop.pdf). Some more
background on the ideas behind the system are in the [RMC slides]
(http://www.msully.net/stuff/rmc-popl-talk.pdf) and the [RMC paper]
(http://www.cs.cmu.edu/~crary/papers/2015/rmc.pdf), although the paper
is pretty theoretical. The concrete syntax in the slides and original
paper is a bit out of date, though, so the best documentation for how
the actual concrete syntax works is the proposal and the code in the
`case_studies/` and `examples/` directories.

You can find me in `##rmc` on `irc.freenode.org`.

Building
-----------

Build with: `./configure [args] && make`

You might need to pass some args to `./configure` to tell it where to
find dependencies, as discussed below in the dependencies list.

The dependencies are:
 * LLVM and clang 3.5 - 3.9, 4.0.
   A path to a clang build/install directory can be provided to
   `./configure` with `--llvm-location=[path]`. If there is an llvm
   dev environment visible on the system (for example, Ubuntu's
   llvm-4.0-dev package), configure will hopefully find it
   automatically.  LLVM installs usually live in /usr/lib/llvm-X.Y/ or
   /usr/local/lib/llvm-X.Y/.

   The binary clang+llvm OS X packages available from the LLVM website
   don't work for this, unfortunately, because of linking/loading
   problems. You'll need to build from source, making sure to pass
   `--enabled-shared` when you run llvm's configure script.

 * To use the (recommended) SMT based backend, you need Microsoft's
   Z3 SMT solver, available from https://github.com/Z3Prover/z3/

   If Z3 is installed at a nonstandard location, pass its installation
   prefix (the directory where its installed include/ and lib/ directories
   live) to `./configure` with `--z3-location=[path]`.

   To disable the SMT backend, pass `--disable-z3` to `./configure.`


Installing
-----------
Actually installing into system directories is Coming Soon.
Until then, it can be used from its build directory.


How to use
-----------

The `rmc-config` script can be used to print the arguments to a compiler
necessary to use RMC.

To build something that uses RMC without running the custom backend
(that is, to use the lower performance fallback), do:
  `cc $(path/to/rmc-config --cflags --fallback) [other args] file.c`

To use the custom RMC backend, do:
  `path/to/clang -O $(path/to/rmc-config --cflags) [other args] file.c`

The clang used must be built with the same LLVM that the RMC compiler
was built against and optimization must be enabled.

Passing `--smt` to `rmc-config` enables the SMT solver based backend and
`--cleanup` enables a backend optimization cleanup pass that should be
safe to use except on POWER on `-O3`.

--

The `run-rmc` script is good for experimenting with RMC. It makes it
easy to target ARM and POWER (at least, if you are on Ubuntu and
install `g++-4.9-multilib-arm-linux-gnueabi` and
`g++-4.9-powerpc-linux-gnu`) and see what the code looks like at various
stages in the pipeline. Check it out to see what all it can do.
(It is also pretty hacky and may well not work on your system...)

--

The Rust support currently doesn't work, but I plan to fix it at some
point.

To use RMC with Rust, you need to be running the nightly and add as
a Cargo dependency

```toml
[dependencies.rmc-plugin]
git = "https://github.com/msullivan/rmc-compiler.git"
[dependencies.rmc]
git = "https://github.com/msullivan/rmc-compiler.git"
```

Then you can pull it into your crate with

```rust
#![feature(plugin)]
#![plugin(rmc_plugin)]
#[macro_use] extern crate rmc;
```

When you compile using the plugin, `rmc-config` must be in your `PATH`
in order for the plugin to find the libraries.

Known bugs of the rust support:
 * It actually currently doesn't work at all
 * The plugin for loading the RMC stuff is janky
 * It only works at all when building with optimizations *enabled*
 * Since we can't specify "noduplicate" on our magic signaling
   functions, LLVM might move things around in ways that our backend
   doesn't understand, causing failure.
 * Also as a result of that, RMC using functions might get inlined
   before we get our hands on them, which means that edges between
   subsequent calls to a function may not work properly.
   For functions where this behavior is important,
   marking them `#[inline(never)]` is advised as a workaround.
