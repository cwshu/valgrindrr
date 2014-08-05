ValgrindRR
==========
Make a record-replay debugging functionality into Valgrind 3.9.0.

We still consider make a Valgrind tool with RR-functionality or
make patch to Valgrind core to let it support RR.

We want to use record-replay functionality with vgdb for debugging.

``valgrindrr_porting`` branch is the plan for porting `Mojiong's valgrindrr`_ (for valgrind 3.3) to valgrind 3.9.

building step
-------------
same as valgrind. refer to `build valgrind tool`_

:: 
    
    ./autogen.sh
    ./configure --prefix=`pwd`/inst
    make
    make install

The executable is ``inst/bin/valgrind``.

testing
-------
valgrindrr_porting branch
+++++++++++++++++++++++++
- record::

    valgrind --record-replay=1 --log-file-rr=<log_file> <exe> <exe_args>

``<exe>`` and ``<exe_args>`` is client program we want to run on the valgrindrr.

- replay::

    valgrind --record-replay=2 --log-file-rr=<log_file>



.. _Mojiong's valgrindrr: http://sourceforge.net/p/valgrind/mailman/valgrind-developers/thread/BAY103-W4642373BA0DDCC8326A9A0AA420@phx.gbl/
.. _build valgrind tool: http://valgrind.org/docs/manual/manual-writing-tools.html#manual-writing-tools.gettingstarted
