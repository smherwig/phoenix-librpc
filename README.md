Overview 
========

librpc is a remote procedure call (RPC) library used by several kernel servers
in the Phoenix SGX microkernel.


Building and Installing
=======================

First, install [librho](https://github.com/smherwig/librho).
Next, download and build librpc:

```
git clone https://github.com/smherwig/phoenix-librpc librpc
cd librpc
make
```

librpc's Makefile assumes that librho is installed in the user's home
directory; if this is not the case, edit the `INCLUDES` variable at the top of
the Makefile.

The build creates two libraries: `librpc.a`, and a position-independent
version, `librpc-pic.a`; the former is for statically linking into an
executable; the latter for statically linking into a shared object.


To install, enter:

```
make install
```

By default, the librpc libraries and header (`rpc.h`) are installed to `/usr/local/`.
To install to a different, location, say, `/home/smherwig`, enter

```
make install INSTALL_TOP=/home/smherwig
```

The rest of this README assumes that the librpc source is located at
`$HOME/src/librpc/`, and that librpc is installed under `$HOME`.


Micro-benchmarks
================

To understand the cost of the RPC mechanism, we design an experiment where a
client issues an RPC to download a payload 100,000 times, and compute the mean
time for the RPC to complete.  We vary the payload size form 0-bytes to 1 MiB.

We perform this experiment in three environments:
1. **non-SGX**: client and server execute outside of an SGX enclave
2. **SGX**: client and server execute within an SGX enclave
3. **exitless**: client and server execute within an SGX enclave and use Phoenix's exitless system calls. 


First, build the benchmarking tools:

```
cd ~/src/librpc/bench
make
```

The server is `rpcbenchserver`, and the client `rpcbenchclient`.
The server takes two arguments: the URL on which to listen for connections, and
the size of the payload to serve, in bytes.  The client also takes two
arguments: the URL to connect to, and the number of requests to perform.  Both
have additional options, such as for keying material.

Next, create or copy over the keying material.  I will assume the keying
material is from the
[phoenix-nginx-eval](https://github.com/smherwig/phoenix-nginx-eval), but
OpenSSL may also be used to create a root certificate (`root.crt`) and a leaf
certificate and key (`proc.crt`, `proc.key`).

```
cd ~
git clone https://github.com/smherwig/phoenix-nginx-eval nginx-eval
cp ~/nginx-eval/config/root.crt ~/src/librpc/bench/
cp ~/nginx-eval/config/proc.crt ~/src/librpc/bench/
cp ~/nginx-eval/config/proc.key ~/src/librpc/bench/
```


The SGX benchmarks require the [phoenix](https://github.com/smherwig/phoenix)
libOS and
[phoenix-makemanifest](https://github.com/smherwig/phoenix-makemanifest)
configuration packager. Download and setup these two projects.  The
instructions here assume that the phoenix source is located at `$HOME/src/phoenix`
and the phoenix-makemanifest project at `$HOME/src/makemanifest`.


non-SGX
-------

In one terminal, run the server, here with a 0-byte payload:

```
cd librpc/bench
./rpcbenchserver -Z root.crt proc.crt proc.key tcp://127.0.0.1:9000 0
```

In another terminal, run the client:

```
cd librpc/bench
./rpcbenchclient -r root.crt tcp://127.0.0.1:8089 100000
```


SGX
---

Make sure that `bench/rpcbenchserver.conf` and `bench/rpcbenchclient.conf` both
have the directive `THREADS 1`.  


Package the server to run on Phoenix:

```
cd ~/src/makemanifest
./make_sgx.py -g ~/src/phoenix -k enclave-key.pem -p \
    ~/src/librpc/bench/rpcbenchserver.conf -t $PWD -v -o rpcbenchserver
cd rpcbenchserver
cp manifest.sgx rpcbenchserver.manifest.sgx
```


Package the client to run on Phoenix:

```
cd ~/src/makemanifest
./make_sgx.py -g ~/src/phoenix -k enclave-key.pem -p \
    ~/src/librpc/bench/rpcbenchclient.conf -t $PWD -v -o rpcbenchclient
cd rpcbenchclient
cp manifest.sgx rpcbenchclient.manifest.sgx
```

In one terminal, run the server:

```
cd ~/src/makemanifest/rpcbenchserver
./rpcbenchserver.manifest.sgx -Z /etc/root.crt /etc/proc.crt /etc/proc.key tcp://127.0.0.1:9000 0
```

In another terminal, run the client:

```
cd ~/src/makemanifest/rpcbenchclient
./rpcbenchclient.manifest.sgx -r /etc/root.crt tcp://127.0.0.1:9000 100000
```


To count futex calls or hardware performance
counters, build Phoenix with `make SGX=1 DEBUG=1`, and repeat the packaging
steps.  Futexes can be counted by redirecting the server and client's stderr
and counting the number of lines containing the string `futex called`.
Hardware counters can be counted as with the `perf-stat(1)` tool.


exitless
--------

Make sure that `rpcbenchserver.conf` and `rpcbenchclient.conf` both have the
directive `THREADS 1 exitless`, rather than `THREADS 1`.  Repeat as before for
SGX.


