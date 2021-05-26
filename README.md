## The sales pitches:

- The etransfer server/client system allows the client program to initiate
  server to server transfers, just by specifying two remote locations. The
data does not flow through the client's machine and/or network.

- The system natively supports remote wildcards; it is possible to transfer
multiple files irrespective of wether they are remote or local.

- The etransfer tools support TCP and
  [[UDT](https://github.com/netvirt/udt4)] over both IPv4 and IPv6. The UDT
protocol is orders of magnitude faster on long, fat, network connections.

- The etransfer tools do not yet have authentication or authorization built in.

- Memory data source and/or sink available for throughput/bottleneck testing; either
  disk read, disk write or both can be avoided by replacement with
    `/dev/zero:<size>` (source) and/or `/dev/null` (destination)
    with `<size>` being `<number>[kMGT]B` to indicate how many bytes to
    transfer.

[`Version 1.0`](https://github.com/jive-vlbi/etransfer/releases/tag/v1.0) was tagged on May 25 2021.
Building a tagged version consists of downloading the `.tar.gz` or `.zip` archive, extracting it, and then executing `make` in the `etransfer` directory.

## After cloning ...
The code uses [[C++11](https://en.wikipedia.org/wiki/C%2B%2B11)] so a
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
