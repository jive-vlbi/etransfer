## The sales pitches:

- The etransfer server/client system allows the client program to initiate
  server to server transfers, just by specifying two remote locations. The
data does not flow through the client's machine and/or network.

- The system natively supports remote wildcards; it is possible to transfer
multiple files irrespective of wether they are remote or local. Since Jun 2026 (v2.0), the daemon also [supports access control lists (ACLs)](#access-control-lists) to restrict read and/or write access to specific files or directories.

- The etransfer tools support TCP, [UDT](https://github.com/netvirt/udt4), and [SRT](https://www.srtalliance.org/) over both IPv4 and IPv6. The UDT and SRT protocols are orders of magnitude faster on long, fat, network connections; SRT adds more stability on top of similar congestion-control behaviour. (See also [UDT or SRT?](#transport-protocols-udt-or-srt))

- Single e-transfer daemon command and data channels are sufficient to support multiple parallel clients. The daemon allows specifiying multiple command and/or data channels for the purpose of offering the service over multiple protocols or to fine-tune support of specific protocol(s) on specific interfaces, see [Example](#Example).

- Since Jun 2026 (v3.0) the tools can be compiled with optional [TLS 1.3 support](#tls-transport-encrypted-command-and-data-channels) (`make TLS=1`), adding encrypted `tls://` / `tls6://` command- and data channels with ssh-style certificate pinning at the client.

- ~~The etransfer tools do not yet have authentication or authorization built in.~~
- Since mid-June 2026 the etransfer tools support [encryption (TLS1.3) and ssh-style public-key authentication!](#authentication-and-authorization)

- Memory data source and/or sink available for throughput/bottleneck testing; either
  disk read, disk write or both can be avoided by replacement with
    `/dev/zero:<size>` (source) and/or `/dev/null` (destination)
    with `<size>` being `<number>[kMGT]B` to indicate how many bytes to
    transfer.

<!--- line breaking in Markdown according to
      https://stackoverflow.com/a/36600196  -->
[`Version 2.0`](https://github.com/jive-vlbi/etransfer/releases/tag/v2.0) was tagged on Jun 03 2026; introduces TCP-keepalive, idle-transfer-timeout, Access Control Lists in the daemon (etd); adds per-file progress-reporting to the client (etc); compiles actual git version info into the binaries (--version is now useful); command-line constraint violation error message more human friendly; adds SRT as another fast UDP-based data transport protocol; fixes: a hang-on-^C, compile issues on Debian (and possible other) Linux systems, some POSIX/strictness violations, log output lines are tagged with the transfer UUID for disentanglement, generated formatters now retain all iomanips (even the transient ones like std::setw)<br/><br/>
[`Version 1.2`](https://github.com/jive-vlbi/etransfer/issues/30) introduces optional [SRT](https://www.srtalliance.org/) data channels alongside TCP and UDT.<br/>
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
    # -j <number> is optional
    #    it makes the compilation go faster on multicore systems
    #    keeping <number> <= N_cores is a good setting probably
    $ make [-j <number>]
    <time passes, no errors should happen ...>
    $
```

After this, the executable files `etd` (the e-transfer daemon) and `etc`
(e-transfer client) can be found in a subdirectory typically called
`<arch>-native-opt` where `<arch>` is a string representing the current
operating system, e.g. `Linux-x86-64` or `Darwin-x86\_64`. It is possible to
compile the same source tree on different systems with or without debug
information.

## GCC[10|12] / Debian[11|12] "Bullseye"/"Bookworm" build problems
**NOTE: this is fixed in v2.0**
In the git repo select an older README.md version to find the original error and fix.
Still thanks to @AarónG for reporting this first!


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
   [[tcp|udt|srt][6]://][user@]host[#port]:/path 
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

## Daemon features

### Authentication and authorization

Since v3.0 the daemon and client support TLS 1.3 encrypted command channels and ssh-style public-key authentication. This is **optional** and must be enabled at build time:

```bash
make clean
make TLS=1 [-j <number>]
```

OpenSSL >= 1.1.1 (or LibreSSL) is required. The zero-dependency non-TLS build remains supported: compile without `TLS=1` to keep the original `tcp://`/`udt://`/`srt://` behaviour. The protocol surface is additive: a TLS-enabled daemon can still offer `tcp://` command channels for older clients.

The main goal is to encrypt the _command_ channel (`tls://` / `tls6://`). You can also use `tls://` for data channels, but it runs over TCP so it is slower than UDT/SRT; use it only when you need end-to-end encrypted data.

#### Daemon setup

1. **Generate a self-signed certificate and key** (a CA is unnecessary; clients pin the certificate fingerprint, see below):

   ```bash
   server$ openssl req -x509 -newkey rsa:3072 -nodes \
               -keyout etd.key -out etd.crt \
               -days 3650 -subj "/CN=$(hostname -f)"
   server$ chmod 600 etd.key
   ```

   ECDSA (`-newkey ec -pkeyopt ec_paramgen_curve:P-256`) works too and gives smaller handshakes.

2. **Create the authorized-keys directory.** This is a directory that contains one file per allowed principal. The file name is the principal name; the file contents are one or more OpenSSH `authorized_keys` lines, exactly like `~/.ssh/authorized_keys`:

   ```bash
   server$ mkdir -p /etc/etransfer/keystore
   server$ chmod 700 /etc/etransfer/keystore
   ```

3. **Start the daemon** with a TLS command channel and the auth keystore:

   ```bash
   server$ .../etd --command tls://:4004 --data srt://:8008 \
                   --cert etd.crt --key etd.key \
                   --authorized-keys /etc/etransfer/keystore
   ```

   To make authentication **mandatory**, add `--require-auth`. When this is set, `etd` will refuse to start unless:
   - `--authorized-keys` is also set, and
   - _every_ `--command` channel is `tls://` or `tls6://` (cleartext command channels can never authenticate, so they would be unusable).

   `etd` prints the TLS fingerprint at startup:

   ```text
   2026-06-17 13:08:59.78: TLS certificate 'etd.crt' sha256=29:4c:dd:cf:49: ... (snip)
   ```

   Share this with users out-of-band, or publish a line like `host#port sha256=...` so they can pre-seed their known-hosts file.

#### Adding authorized users

For each user that should be allowed:
- Pick a principal name (e.g. `alice`).
- Have the user share their **public** key (the `.pub` file from `ssh-keygen`, or an entry copied from `~/.ssh/authorized_keys`).
- Create `/etc/etransfer/keystore/alice` and paste the key(s) in it. Multiple keys per principal are allowed, one per line. Comments on the key line are preserved and logged.

Example keystore file `/etc/etransfer/keystore/alice`:

```text
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... alice@laptop
ssh-rsa AAAA... alice@desktop
```

#### Client setup

1. **Trust the daemon's certificate.** The client stores fingerprints in `~/.etransfer_known_hosts` (one `host#port fingerprint` pair per line). The first contact can be handled automatically or interactively:
   - `--tls-verify=strict` (default): only connect if the fingerprint is already pinned in `~/.etransfer_known_hosts`.
   - `--tls-verify=tofu` (alias `accept-new`): trust and record an unknown host on first contact, but refuse if the fingerprint later changes.
   - `--tls-verify=ask`: prompt for confirmation on first contact when running interactively; in a script or pipeline it falls back to `strict` so it never auto-trusts.

   A changed fingerprint is always refused. To pre-seed a known host, add a line such as:

   ```text
   server.example.com#4004 sha256=29:4c:dd:cf:49:...
   ```

   The fingerprint format is the same as `openssl x509 -in etd.crt -noout -fingerprint -sha256` (case-insensitive).

2. **Generate an SSH key pair** if you do not have one:

   ```bash
   client$ ssh-keygen -t ed25519 -f ~/.ssh/etransfer_id_ed25519 -N ''
   ```

   The private key file can be unencrypted or passphrase-protected (the passphrase is prompted for if needed). Share only the `.pub` file with the daemon admin.

3. **Run `etc` with authentication.** Use `--identity` to point to your private key file and `tls://<principal>@<host>#<port>:/path` to tell the daemon who you are:

   ```bash
   client$ .../etc --identity ~/.ssh/etransfer_id_ed25519 \
                   /local/path 'tls://alice@server.example.com#4004:/remote/path'
   ```

   The `--identity` argument points to the **private** key.

   If the URL does not contain a `user@` prefix, the client falls back to `--principal <name>`, and if that is also omitted, to the local login name. The easiest workflow is to put the principal in the URL:

   ```bash
   client$ .../etc --identity ~/.ssh/etransfer_id_ed25519 \
                   --principal alice \
                   /local/path 'tls://server.example.com#4004:/remote/path'
   ```

#### What happens when authentication is required

With `--require-auth` on the daemon:

- A client that connects without `--identity` is rejected with `ERR authentication required` as soon as it tries a real command.
- A client that presents an identity not in the principal's keystore file is rejected with `ERR authentication failed`.
- A client that connects to the daemon over `tcp://` (or `udt://`/`srt://`) can never authenticate; `etd` refuses to start if `--require-auth` is combined with a non-TLS `--command` channel.

The command channel is authenticated; data channels are protected by the transfer UUIDs that are only handed out after a successful command-channel request, so an unauthenticated command channel cannot obtain a data channel capability.


### Access control lists

The daemon can optionally read an access control list via the `--acl <file>` option. The file must be readable by the daemon and contains a YAML document describing per-section allow/deny glob patterns. Patterns follow the rules of `fnmatch(3)` with the `FNM_PATHNAME` flag; slashes must be matched explicitly, and a trailing `**` enables recursive matching beneath the given prefix. A bare `"**"` applies to all paths.

```yaml
read:
  default:
    allow: "**"
  deny:
    - "/restricted/**"

write:
  default:
    deny: "**"
  allow:
    - "/data/projects/**"
    - "/scratch/*/uploads"
```

Both top-level sections (`read` and `write`) are required; the daemon refuses to
start if either mapping is missing. Inside a section:

- `default` is a single rule that specifies whether matching paths should be allowed or denied when no other rule matches. The `allow`/`deny` key inside the default rule holds a glob pattern; if the path matches the pattern the decision is applied, otherwise the request is rejected. To apply the default to every path use "**".
- `allow` and `deny` are optional lists of glob patterns evaluated in the order shown above. The first matching rule decides the outcome.

Additional globbing notes:

- Patterns use `fnmatch(3)` with `FNM_PATHNAME`, so `/foo/*` matches only immediate children of `/foo`.
- A pattern ending in `**` (e.g. `/data/**`) matches the named directory and everything deeper beneath it.
- `**` is only allowed as the final token in a pattern (with an optional `/` before it).


### TCP keepalive controls

Motivation: long-lived TCP command connections can be silently dropped by stateful firewalls and load-balancers when they sit idle, e.g. during a long/slow data transfer when no commands/replies flow between the client and daemon. The daemon now supports explicit TCP keepalive management (see note below the options though) instead of relying on per-host defaults.

New options:

- `--tcp-keepalive=true|false` – toggle `SO_KEEPALIVE` on accepted command
  sockets. The default stays `false`; pass `true` to enable probing.
- `--tcp-keepcount <n>` – (if supported by the platform) send at most `<n>`
  probes before giving up. Minimum value is 1.
- `--tcp-keepinterval <seconds>` – (if supported) number of seconds between
  individual probes once keepalive is enabled.
- `--tcp-keepidle <seconds>` – (if supported) idle time before the first
  keepalive probe fires.

The tuning flags are automatically exposed if the underlying OS indicate support. The tuning settings only take effect when keepalive is switched on. Unsupported parameters are ignored without failing the daemon startup.

For background on Linux-specific keepalive defaults and sysctls, see the
[Linux TCP keepalive tuning guide](https://www.man7.org/linux/man-pages/man7/tcp.7.html#TCP_KEEPALIVE).

### Idle transfer timeout / automatic cleanup

Motivation: previously, when clients would disappear mid-transfer (e.g. crashes, network splits, aborted processes), the daemon would keep their session state around, keeping a lock on the destination file, preventing starting a new transfer until the daemon was restarted. This can now be mitigated by setting an idle timeout, after which a transfer gets automatically removed from the daemon's state and retransfer can be attempted.

New option:

- `--inactive-timeout <seconds>` – if set to a positive value, the daemon spawns a watchdog thread that cancels transfers which have seen no progress for the specified number of seconds. The default is “disabled” (timeout ≤ 0).

When a timeout triggers, the daemon force-closes the transfer’s command and data connections and removes the lock on the destination path in question, freeing it for a new attempt. Clients may want to use the `--resume` file copy mode to pick up where the stalled transfer left off.

### TLS transport (encrypted command and data channels)

Motivation: the plain `tcp://` command channel sends everything - paths, directory listings, transfer commands - in the clear. Compiling with TLS support adds `tls://` and `tls6://` as additional channel schemes; these are ordinary TCP connections upgraded to TLS 1.3 immediately after connecting. See `docs/tls-design.md` for the design and the planned authentication follow-up.

#### Building with TLS support

TLS support is optional and off by default - the zero-dependency build is preserved. To enable it, OpenSSL >= 1.1.1 (or LibreSSL) including development headers must be installed, then:

```bash
    $ make TLS=1 [-j <number>]
```

On macOS the Makefile asks `brew --prefix openssl@3` for the location of the (keg-only) homebrew OpenSSL automatically. Note that the object directory is shared between TLS and non-TLS builds, so run `make clean` when switching between the two.

#### Creating the daemon's certificate

A `tls://` listener requires a certificate and private key in PEM format, passed via the `--cert` and `--key` options. There is **no need to run a CA**: the client does not validate the certificate chain, it pins the certificate's SHA256 fingerprint ssh-known_hosts style (see below), so a self-signed certificate is exactly as trustworthy as a CA-signed one. Generate one like this:

```bash
    server$ openssl req -x509 -newkey rsa:3072 -nodes \
                -keyout etd.key -out etd.crt \
                -days 3650 -subj "/CN=$(hostname -f)"
    server$ chmod 600 etd.key
```

(Any key type OpenSSL supports for TLS 1.3 works; substitute e.g. `-newkey ec -pkeyopt ec_paramgen_curve:P-256` for a smaller/faster ECDSA key. The `CN` is informational only - the client displays it, but trust is decided on the fingerprint.)

#### Starting a TLS listener

```bash
    server$ .../etd --command tls://:4004 --data {any of the supported protocols}://:8008 \
                    --cert /path/to/etd.crt --key /path/to/etd.key
```

One `--cert`/`--key` pair serves all `tls`/`tls6` listeners of the daemon. The key file must be readable by the user the daemon runs as (mind `--run-as`) and should not be readable by anyone else. Mixing schemes is fine, e.g. a `tls://` command channel with `srt://` + `tcp://` data channels, or offering both `tcp://` and `tls://` command channels during a migration period.

Older clients (protocol version < 5) are automatically not offered `tls`/`tls6` data channels - same filtering mechanism as for SRT - so adding a TLS data channel does not break existing clients as long as a non-TLS data channel remains available for them.

#### Client side: trust-on-first-use pinning

The client connects with:

```bash
    client$ .../etc /local/path 'tls://server#4004:/remote/path'
```

On first contact with a daemon the client stores the certificate's SHA256 fingerprint in `~/.etransfer_known_hosts` (one `host#port fingerprint` pair per line) and trusts it from then on - the same trust-on-first-use model as ssh. If the certificate later changes, the client refuses the connection and prints both fingerprints; if (and only if) the change is expected - e.g. the daemon admin rolled a new key - remove the stale entry from `~/.etransfer_known_hosts` and reconnect.

To close the first-contact window entirely the daemon admin can publish the certificate fingerprint out-of-band. The daemon prints it at startup when loading the certificate:

```text
    TLS certificate 'etd.crt' sha256=ab:12:...:ef
```

or it can be extracted from the certificate file at any time:

```bash
    server$ openssl x509 -in etd.crt -noout -fingerprint -sha256
```

Users then pre-seed their `~/.etransfer_known_hosts` with a line containing `host#port` followed by the fingerprint (`:`-separated hex; matching is case-insensitive so the uppercase `openssl` output can be pasted as-is, minus the `sha256 Fingerprint=` prefix).


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

## Transport protocols: UDT or SRT?

The daemon can advertise several data-channel protocols; the client will try them in order until one connects. In practice you will usually choose between:

- **UDT (UDP-based Data Transfer)** – a high-performance protocol that shines on dedicated, high-latency, or high-bandwidth links. It is extremely efficient for bulk-data moves when you control both ends of the network path and do not need encryption.
- **SRT (Secure Reliable Transport)** – built on UDT’s ideas but a more stable implementation. Should add more robust NAT/firewall traversal. Definitely try to advertise an SRT data channel when traversing less predictable networks.

**Note:** It is recommended to put SRT before UDT as clients will try to connect in order of daemon command line order. Older clients that do not support SRT do not get offered the SRT data channel - it will be filtered out, so to be ultimately compatible configure both SRT and UDT channels. Newer clients that do support SRT will prefer that over UDT in such a configuration.
