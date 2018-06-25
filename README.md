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

The daemon /must/ have at least one command and one data channel:

```bash
    server$ .../etd --command tcp://:4004 --data udt://:8008
```
Now the daemon listens on all the machine's IPv4 addresses, port 4004 for client requests and data is
transferred into the daemon using UDT over port 8008. (Note: these values are also the
compiled in defaults).

I.e. to transfer some files from a machine into the server (it is important
to prevent the shell from expanding the wildcard pattern):
```bash
    client$ .../etc '/mnt/data/eg098a/*' server:4004/tmp/
```

Both tools support the "--help" command line option explain all options.


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
