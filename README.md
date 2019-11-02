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


Micro-benchmarks
================

Vary the payload from 0 bytes, 1024 (1 KiB), 10240 (10 KiB), 102400(100 KiB), 1048567
(1 MiB).

## non-SGX

```
./rpcbenchserver -Z root.crt proc.crt proc.key tcp://127.0.0.1:9000 0
```

```
./rpcbenchclient -r root.crt tcp://127.0.0.1:8089 100000
```

If collecting hardware perform counters, do:


```
./rpcbenchserver -Z root.crt proc.crt proc.key tcp://127.0.0.1:9000 0
```

```
./rpcbenchclient -r root.crt -s 30 tcp://127.0.0.1:8089 100000
```

```
cd ~/phoenix/perftools
pax aux | grep rpcbenchclient | awk '{print $1}' | xargs ./myperfpid.sh
```


## SGX

Make sure that `rpcbenchserver.conf` and `rpcbenchclient.conf` both have the
directive `THREADS 1`.

Build the manifest.sgx file for the server:

```
cd ~/phoenix/makemanifest

./make_sgx.py -g ~/ws/phoenix -k enclave-key.pem -p \
    ~/phoenix/librpc/bench/rpcbenchserver.conf -t $PWD -v -o rpcbenchserver
cd rpcbenchserver
cp manifest.sgx rpcbenchserver.manifest.sgx


Build the manifest.sgx file for the client

```
cd ~/phoenix/makemanifest
./make_sgx.py -g ~/ws/phoenix -k enclave-key.pem -p \
    ~/phoenix/librpc/bench/rpcbenchclient.conf -t $PWD -v -o rpcbenchclient
cd rpcbenchclient
cp manifest.sgx rpcbenchclient.manifest.sgx
```

Run the tests

```
./rpcbenchserver.manifest.sgx -Z /etc/root.crt /etc/proc.crt /etc/proc.key tcp://127.0.0.1:9000 0
```

```
./rpcbenchclient -r /etc/root.crt tcp://127.0.0.1:9000 100000
```

To count futex calls and hardware counters, build Graphene with `make SGX=1 DEBUG=1`


## exitless

Make sure that `rpcbenchserver.conf` and `rpcbenchclient.conf` both have the
directive `THREADS 1 exitless`.  Repeat as before for SGX.

