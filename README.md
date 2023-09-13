## The sales pitches:

- The etransfer server/client system allows the client program to initiate
  server to server transfers, just by specifying two remote locations. The
data does not flow through the client's machine and/or network.

- The system natively supports remote wildcards; it is possible to transfer
multiple files irrespective of wether they are remote or local.

- The etransfer tools support TCP and [UDT](https://github.com/netvirt/udt4) over both IPv4 and IPv6. The UDT protocol is orders of magnitude faster on long, fat, network connections.

- Single e-transfer daemon command and data channels are sufficient to support multiple parallel clients. The daemon allows specifiying multiple command and/or data channels for the purpose of offering the service over multiple protocols or to fine-tune support of specific protocol(s) on specific interfaces, see [Example](#Example).

- The etransfer tools do not yet have authentication or authorization built in.

- Memory data source and/or sink available for throughput/bottleneck testing; either
  disk read, disk write or both can be avoided by replacement with
    `/dev/zero:<size>` (source) and/or `/dev/null` (destination)
    with `<size>` being `<number>[kMGT]B` to indicate how many bytes to
    transfer.

<!--- line breaking in Markdown according to
      https://stackoverflow.com/a/36600196  -->
[`Version 1.1`](https://github.com/jive-vlbi/etransfer/releases/tag/v1.1) was tagged on Feb 10 2022; log into file-in-directory, compile issues, NFS workaround, fix bug in UUID generator and SIGSEGV in fmtTime<br/>
[`Version 1.0.1`](https://github.com/jive-vlbi/etransfer/releases/tag/v1.0.1) was tagged on Jun 02 2021; bug in v1.0 found after release: superfluous comma in regex for multiple data channel "parsing"<br/>
[`Version 1.0`](https://github.com/jive-vlbi/etransfer/releases/tag/v1.0) was tagged on May 25 2021<br/><br/>

Building a tagged version consists of downloading the `.tar.gz` or `.zip` archive, extracting it, and then executing `make` in the `etransfer` directory.

## After cloning ...
The code uses [C++11](https://en.wikipedia.org/wiki/C%2B%2B11) so a
suitable, C++11 compliant compiler is necessary.

```bash
    $ git clone https://github.com/jive-vlbi/etransfer.git
    $ cd etransfer
    $ make
    <time passes, no errors should happen ...>
    $
```

After this, the executable files `etd` (the e-transfer daemon) and `etc`
(e-transfer client) can be found in a subdirectory typically called
`<arch>-native-opt` where `<arch>` is a string representing the current
operating system, e.g. `Linux-x86-64` or `Darwin-x86\_64`. It is possible to
compile the same source tree on different systems with or without debug
information.

## GCC9 / CentOS7 build problems

Several users wrote in to complain about this:

    In file included from /usr/local/include/c++/9.2.0/random:38,
    from /.../etransfer/src/etdc_uuid.h:28,
    from /.../etransfer/src/etdc_etd_state.h:25,
                from src/etc.cc:23:
    /usr/local/include/c++/9.2.0/cmath:589:11: error: ‘::isinf’ has not been declared
    using ::isinf;
          ^~~~~
    Make: *** [Linux-x86_64-native-debug/src/etc.cco] Error 1

This seems a hiccup in the header files of the compiler and can be remedied
by the following steps:

- load a more up-to-date compiler on CentOS 7 in a new bash environment:
  ```bash
  # may need to do this first
  $> yum install centos-release-scl -y
  $> yum install devtoolset-11-* -y

  # this installs the new(er) compiler
  $> scl enable devtoolset-11 bash
  ```
  or follow [this github gist recipe for GCC9 on CentOS7](https://gist.github.com/nchaigne/ad06bc867f911a3c0d32939f1e930a11)

- Remove the directives below from the `Makefile`:

    **-D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -U_GNU_SOURCE**

- run `make` as per normal instructions

Thanks to @ChristianP and @AbelC for testing and suggesting.

## Running
The tools operate as a standard daemon/client pair.

The daemon _must_ have at least one command and one data channel specified:

```bash
    server$ .../etd --command tcp://:4004 --data udt://:8008
```
Now the daemon listens on all the machine's IPv4 addresses, port 4004 for client requests and data is
transferred into the daemon using UDT over port 8008. (Note: these port numbers are also the
compiled in defaults, the protocols are NOT defaulted).

To transfer some files from your local machine into the server, use the
e-transfer client `etc`:
```bash
    client$ .../etc '/mnt/data/eg098a/*' server:/tmp/
```
Note: it is important to prevent the shell from expanding the wildcard pattern!

Remote source and/or destination paths are specified a little more complex
than e.g. in simple `scp(1)`:
```bash
   [[tcp|udt][6]://][user@]host[#port]:/path 
```
So `host:/path` is the absolute minimum which needs to be specified for a
remote URL and is shorthand for `tcp://host#4004:/path`.

The reason the protocol type and version can be encoded in each URL is
because the e-transfer client _specifically_ enables triggering remote
daemon to remote daemon transfers. As such, having a global "use IPv6"
option or "use port XXXX" (like in `scp(1)`) is not feasible; one remote
daemon may be listening on TCP/IPv4:4004 whilst the other may be reachable
over UDT/IPv6:46227.


Both the e-transfer daemon and client support the "--help" command line option explain all options.


## File copy modes

Because the etransfer programs expect to transfer large files over long
distances, it supports resuming of interrupted transfers. This is done by
comparing file sizes at the source and destination and only the remaining
bytes are transferred.

Although called resuming, the system regards each file to be transferred as
a “resume” operation. The difference is in how the combination of existence
and/or length of the destination file is handled. By default, an existing
remote file will not be overwritten and an error message generated. Three
modes are supported to change this behaviour:


- overwrite: this restarts the file transfer. The remote file is truncated
to zero size after which the whole source file is transferred.
- skip existing: this assumes that if the remote file exists, it is
complete. This is not checked. No bytes are transferred and no error is
generated. The client continues transferring the next file.
- resume: this is the actual resume operation. If the source file is longer
than the destination file, the remaning bytes are transferred. If the source
file’s size is shorter or equal to the destination no bytes are transferred
and no error is generated.


## Extra
The server administrator may start the etransfer server with multiple
command- and/or data protocols and/or port numbers. Only the protocol(s) and
port number(s) of the command connection(s) should have to be made public - 
for the data connection the client program loops over all data addresses the
server program was started with, in order, and uses the first one which
succesfully connects.

Note that:
```bash
    server$ .../etd --command tcp:// --data udt://
```

is short for:

```bash
    server$ .../etd --command tcp://0.0.0.0:4004 --data udt://0.0.0.0:8008
```


## Example

On a multi-homed server with e.g. an internal network interface
(non-routable) and an external one, the `etd` server administrator may want
to offer a `tcp` based data channel on the internal interface (within a data
centre `tcp` is faster than `udt`) whilst on the external interface it will
offer the `udt` protocol:

```bash
    server$ .../etd --command tcp:// --data tcp://192.168.1.20 --data udt://192.42.120.32 ...
```

Explanation:

A single, `tcp` based, command channel is enough to service all clients. The
server will communicate the available data channels to the client in the
order they were listed on its command line. The client will attempt to
connect to all data channels in the received order and use the first
channel that succesfully connects.

In the example above, clients will first try to connect to the (unroutable) IP
address, meaning that internal clients will use that one. External clients
will see that connection attempt fail and will attempt to connect to the
next data channel, in this case the `udt` one.


Using multiple data channels it is possible to indicate a preference to use
`tcp over IPv6` for the data by running the daemon like this:

```bash
    server$ .../etd --command tcp:// --command tcp6:// --data tcp6://192.168.1.20 --data tcp://192.168.1.20 ...
```

Explanation: `IPv6` and `IPv4` are separate address spaces so having the
same port number should not collide. For the command channels the order does
not matter since the user chooses on the `etc` command line which service
(`tcp` or `tcp6`) to connect to.

In this configuration a client connecting to the daemon's`tcp6` command channel may still end up using the `tcp over IPv4` data channel. If this is undesired the daemon should be
started twice; once with `tcp` command+data channels and once with a `tcp6`
command+data channels:

```bash
    server$ .../etd --command tcp:// --data tcp://192.168.1.20 --data udt://:8009
    server$ .../etd --command tcp6:// --data tcp6://192.168.1.20 --data udt6://:8009 ...
```

> Note: for some unknown reason it is not very stable to run `udt` and
> `tcp` data channels on the same port number, even though these protocols
> _should_ be separate address spaces. 

In case of problems with this (same port number on different address spaces)
it should be easy enough to run the different protocols on different ports -
it's transparent to the client, modulo firewall configuration(s) blocking
those port(s) at either end of the transfer.
