Overview 
========

librpc is a remote procedure call (RPC) library used by several kernel servers
in the [Phoenix](https://github.com/smherwig/phoenix) SGX microkernel.


<a name="building"/>Building and Installing
===========================================

librpc depends on [librho](https://github.com/smherwig/librho).  librpc's
Makefile assumes that librho is installed in the user's home directory; if this
is not the case, edit the `INCLUDES` variable at the top of the Makefile.

To download and build librpc, enter:

```
cd ~/src
git clone https://github.com/smherwig/phoenix-librpc librpc
cd librpc
make
```

The build creates two libraries: `librpc.a`, and a position-independent
version, `librpc-pic.a`; the former is for statically linking into an
executable; the latter for statically linking into a shared object.


To install, enter:

```
make install
```

By default, the librpc libraries and header (`rpc.h`) are installed to
`/usr/local/`.  To install to a different location, such as the user's home,
enter:

```
make install INSTALL_TOP=$HOME
```

The rest of this README assumes that the librpc source is located at
`$HOME/src/librpc/`, and that librpc is installed under `$HOME`.


<a name="micro-benchmarks"/> Micro-benchmarks
=============================================

I assume that [phoenix](https://github.com/smherwig/phoenix#building) is built
and located at `$HOME/src/phoenix` and that
[makemanifest](https://github.com/smherwig/phoenix-makemanifest) is cloned to
`~/src/makemanifest`:

To understand the cost of the RPC mechanism, we design an experiment where a
client issues RPCs to download a fixed-sized payload repeatedly, and compute
the mean time for the RPC to complete.

We perform this experiment in three environments:
1. **non-SGX**: client and server execute outside of an SGX enclave
2. **SGX**: client and server execute within an SGX enclave
3. **exitless**: client and server execute within an SGX enclave and use
   Phoenix's exitless system calls. 


Build the benchmarking tools:

```
cd ~/src/librpc/bench
make
```

The server is `rpcbenchserver`, and the client `rpcbenchclient`.  The server
takes two arguments: the URL on which to listen for connections, and the size
of the payload to serve, in bytes.  The client also takes two arguments: the
URL to connect to, and the number of requests to perform.  Both have additional
options, such as for keying material.


Copy the keying material:

```
cp ~/share/phoenix/root.crt ~/src/librpc/bench
cp ~/share/phoenix/proc.crt ~/src/librpc/bench
cp ~/share/phoenix/proc.key ~/src/librpc/bench
```


non-SGX
-------

In one terminal, run the server, here with a 0-byte payload:

```
cd ~/src/librpc/bench
./rpcbenchserver -Z root.crt proc.crt proc.key tcp://127.0.0.1:9000 0
```

In another terminal, run the client, which will issue 100,000 RPCs to the server:

```
./rpcbenchclient -r root.crt tcp://127.0.0.1:9000 100000
```


SGX
---

Ensure that `bench/rpcbenchserver.conf` and `bench/rpcbenchclient.conf` both
have the directive `THREADS 1`.  


Package the server to run on Phoenix:

```
cd ~/src/makemanifest
./make_sgx.py -g ~/src/phoenix -k ~/share/phoenix/enclave-key.pem -p \
    ~/src/librpc/bench/rpcbenchserver.conf -t $PWD -v -o rpcbenchserver
```


Package the client to run on Phoenix:

```
cd ~/src/makemanifest
./make_sgx.py -g ~/src/phoenix -k ~/share/phoenix/enclave-key.pem -p \
    ~/src/librpc/bench/rpcbenchclient.conf -t $PWD -v -o rpcbenchclient
```

In one terminal, run the server:

```
cd ~/src/makemanifest/rpcbenchserver
./rpcbenchserver.manifest.sgx -Z /etc/root.crt /etc/proc.crt /etc/proc.key \
        tcp://127.0.0.1:9000 0
```

In another terminal, run the client:

```
cd ~/src/makemanifest/rpcbenchclient
./rpcbenchclient.manifest.sgx -r /etc/root.crt tcp://127.0.0.1:9000 100000
```

To count futex calls or hardware performance counters, build Phoenix with `make
SGX=1 DEBUG=1`, and repeat the packaging steps.  Futexes can be counted by
redirecting the server and client's stderr and counting the number of lines
containing the string `futex called`.  Hardware counters can be counted as with
the `perf-stat(1)` tool.


exitless
--------

Ensure that `rpcbenchserver.conf` and `rpcbenchclient.conf` both have the
directive `THREADS 1 exitless`, rather than `THREADS 1`.  Repeat as before for
SGX.


