ValgrindRR
==========
Make a record-replay debugging functionality into Valgrind 3.9.0.
Maybe it will be a patch for Valgrind core, or just a Valgrind tool. 
We want to use record-replay functionality with vgdb for debugging.

building step
-------------
:: 
    
    ./autogen.sh
    ./configure --prefix=`pwd`/inst
    make
    make install
