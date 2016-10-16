ValgrindRR
==========
Make a record-replay debugging functionality into Valgrind 3.9.0.

We still consider make a Valgrind tool with RR-functionality or
make patch to Valgrind core to let it support RR.

We want to use record-replay functionality with vgdb for debugging.

We are now doing the plan on ``valgrindrr_porting`` branch which is porting `Mojiong's valgrindrr`_ (for valgrind 3.3) to valgrind 3.9.

valgrindrr_porting branch
-------------------------

valgrindrr only support 32bits platform currently, but we can use it on 64bits linux to debug 32bits binary.

building step
+++++++++++++

- i386 linux:: 
    
    ./autogen.sh
    ./configure --prefix=`pwd`/inst
    make
    make install

- amd64 linux::

    ./autogen.sh
    ./configure --prefix=`pwd`/inst --build=i386-linux
    make
    make install

- ref: valgrind's build step: `build valgrind tool`_

The executable is ``inst/bin/valgrind``.

testing command
+++++++++++++++
- record::

    valgrind --record-replay=1 --log-file-rr=<log_file> <exe> <exe_args>

``<exe>`` and ``<exe_args>`` is client program we want to run on the valgrindrr.

- replay::

    valgrind --record-replay=2 --log-file-rr=<log_file>

testing scenario
++++++++++++++++
RR functionality testing codes are in ``rr_testcode`` directory.

- bugs
    - #1 is for localtime_test.c, the part of ``/bin/date`` program
    - #2 is for file_IO.c.


.. _Mojiong's valgrindrr: http://sourceforge.net/p/valgrind/mailman/valgrind-developers/thread/BAY103-W4642373BA0DDCC8326A9A0AA420@phx.gbl/
.. _build valgrind tool: http://valgrind.org/docs/manual/manual-writing-tools.html#manual-writing-tools.gettingstarted
