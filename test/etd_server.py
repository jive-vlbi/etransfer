# implements server side mechanics of ETD/ETC
# note that this class does not know anything about s
import glob, os, random, string, threading, time, re, glob, socket, itertools, fnmatch, trace, functools, errno, etdc_fd, operator

trace.dont_trace()

random.seed()

chars = string.letters + string.digits
def mkUUID():
    return ''.join(random.sample(chars, random.randint(10, 15)))
    #return ''.join(random.sample(string.printable, random.randint(10, 15)))

@trace.trace
def doXFER(fdin, fdout, uuid):
    while True:
        fp = fdin.tell()
        x  = fdin.read( 2 * 1024 * 1024 )
        if not x:
            break
        msg = {'uuid': uuid, 'fpos': fp, 'sz': len(x)}
        fdout.send(repr(msg))
        fdout.send(x)
        #ok = fdout.recv(1024)
    fdin.close()
    fdout.close()
    return True

class ETDInterface(object):
    def __init__(self, *args, **kwargs):
        pass

    def listPath(self, path):
        raise RuntimeError, "listPath() not implemented by concrete class"

    def requestFileWrite(self, path, mode):
        raise RuntimeError, "requestFileWrite() not implemented by concrete class"

    def requestFileRead(self, path, alreadyhave):
        raise RuntimeError, "requestFileRead() not implemented by concrete class"

    def sendFile(self, path, uuid, alreadyhave, remote):
        raise RuntimeError, "sendFile() not implemented by concrete class"

    def getFile(self, path, uuid, start, src):
        raise RuntimeError, "getFile() not implemented by concrete class"

    def removeUUID(self, uuid):
        raise RuntimeError, "removeUUID() not implemented by concrete class"

    def status(self):
        raise RuntimeError, "status() not implemented by concrete class"

    def dataChannelAddr(self):
        raise RuntimeError, "dataChannelAddr() not implemented by concrete class"

def all_access(credentials):
    return True

def no_access(credentials):
    return False

no_acl_entry = { 'read': no_access, 'write': no_access }

# This is the real ETDServer
class ETDServer(ETDInterface):
    def __init__(self, *args, **kwargs):
        super(ETDServer, self).__init__(*args, **kwargs)
        self.credentials = None
        self.acl         = {
                                '/tmp/*':  { 'read': all_access, 'write': all_access },
                                '/home/*': { 'write': all_access }
                             }
        self.transfer    = kwargs.get('xfers', {})
        self.isRemote    = False
        self.dataChannel = kwargs.get('data', None)

    # Only has limited functionality to offer
    @trace.trace
    def listPath(self, path):
        self._accessOk(path, 'read')
        # only accept absolute paths
        return filter(os.path.isfile, glob.glob(path))

    def close(self):
        pass
#    def openServer(self, destination, mode, connection):
#        if self.transfer is not None:
#            raise RuntimeError, "Not idle"
#        # valid destination?
#        self._accessOk(destination, 'write')
#        # connection must be protocol:port or something like that
#        #   port may be explicit or auto-assigned
#        incoming = mk_server(connection)
#        destfile = mk_client('file:'+destination+":"+mode)
#        uuid     = mkUUID()
#        # Spawn handler for this transfer:
#        #   - accept on 'incoming'
#        #   - write to 'destfile'
#        #   - only allow traffic with correct uuid
#        self.transfer = (incoming, destfile, uuid)
#        # before returning, extract server details
#        # such that client knows where to connect
#        return (uuid, incoming.get_sockname(), destfile.size())

#    def appendData(self, protocol, uuid, path, data):
#        entry = self.transfer.get(protocol, {}).get(uuid, None)
#        if entry is None:
#            raise RuntimeError, "Invalid UUID for PROTOCOL"
#        if entry['path'] != path:
#            raise RuntimeError, "Invalid PATH sent"
#        entry['fd'].write( data )
#        return "ok"

    # this is a request to prepare for an incoming 
    # data transfer. This verifies that the path is
    # writeable and creates the file in the requested
    # mode. Comes up with unique id for this specific transfer.
    # Adds the transfer to the list of acceptable transfers for
    # the indicated protocol.
    # Returns: the unique id, the data port for the protocol 
    #          and the number of bytes present for the file
    @trace.trace
    def requestFileWrite(self, path, mode):
        if self.transfer:
            raise RuntimeError, "Server already transferring"
        (fd, alreadyhave) = self._mk_path(path, mode)
        uuid              = mkUUID()
        print "ETDServer:requestFileWrite/fd=",fd," alreadyhave=",alreadyhave," uuid=",uuid
        self.transfer[uuid] = {'path':path, 'fd':fd} 
        return (uuid, alreadyhave)

    @trace.trace
    def requestFileRead(self, path, alreadyhave):
        # test if we may read the file
        self._accessOk(path, 'read')
        fd   = etdc_fd.mk_client('file', path, 'read')
        sz   = fd.size()
        uuid = mkUUID()
        self.transfer[uuid] = {'path':path, 'fd':fd }
        if alreadyhave<=sz:
            fd.seek( alreadyhave )
        return (uuid, sz - alreadyhave)

    @trace.trace
    def sendFile(self, path, uuid, start, dest):
        if self.transfer:
            raise RuntimeError, "Already busy transferring"
        self._accessOk(path, 'read')
        #fd = open(path, 'r')
        fd = etdc_fd.mk_client("file", path, 'r')
        self.transfer[ uuid ] = {'path': path, 'fd': fd}
        # should start thread to do the transfer
        sok = etdc_fd.mk_client( *dest )
        fd.seek( start )
        doXFER(fd, sok, uuid)

    @trace.trace
    def removeUUID(self, uuid):
        #self.transfer[uuid]['fd'].close()
        del self.transfer[ uuid ]

    @trace.trace
    def getFile(self, uuidSrc, uuidDst, remain, src):
        if uuidDst not in self.transfer:
            raise RuntimeError, "Unconfigured UUID={0}".format(uuidDst)
        sok = etdc_fd.mk_client( *src )
        # should start thread to do the transfer
        cmd = {'uuid': uuidSrc, 'push':1, 'sz':remain }
        fd  = self.transfer[uuidDst]['fd']
        sok.send(repr(cmd))
        try:
            while remain>0:
                b = sok.recv( min(remain, 2*1024*1024) )
                fd.write( b )
                remain = remain - len(b)
        except Exception, E:
            print "getFile/exception: ", repr(E)
        fd.close()
        sok.close()
        return True

    @trace.trace
    def getFileOld(self, path, uuid, start, src):
        if self.transfer and uuid not in self.transfer:
            raise RuntimeError, "Already busy transferring {0}".format( self.transfer.keys() )
        sok = etdc_fd.mk_client( *src )
        # should start thread to do the transfer
        cmd = {'uuid': uuid, 'fpos':start, 'path':path, 'push':1 }
        fd  = self.transfer[uuid]['fd']
        fd.seek( start )
        sok.send(repr(cmd))
        try:
            while True:
                fd.write( sok.recv(2*1024*1024) )
        except Exception, E:
            print "getFile/exception: ", repr(E)
        fd.close()
        sok.close()
        return True

    @trace.trace
    def dataChannelAddr(self):
        return self.dataChannel.getsockname() if self.dataChannel else None

    def status(self):
        if not self.transfer:
            return 'idle'
        else:
            # update transfer status
            return self.transfer

    @trace.trace
    def _mk_path(self, path, mode):
        # have we been denied logical access?
        if self._accessOk(path, 'write'):
            (directory, _) = os.path.split(path)
            try:
                os.makedirs(directory)
            except os.error, E:
                # if the directory already exists then that's not an error
                if E.errno!=errno.EEXIST:
                    raise
            fd = etdc_fd.mk_client('file', path, mode)
            return (fd, fd.size())
            #return (open(path, mode), 0)
        raise RuntimeError, "Access denied"

    # throw on disallowed access
    @trace.trace
    def _accessOk(self, path, method):
        #print "_accessOk({0}, {1})".format( path, method )
        if not path or len(path)==0 or path[0]!='/':
            raise RuntimeError, "Invalid path {0}".format(path)
        # does the pattern list any privileged dirs?
        #if self.credentials not in self.acl.get(path, {}).get(method, {}):
        #if not self.acl.get(path, no_acl_entry).get(method, no_access)( self.credentials ):
        #    raise RuntimeError, "You are not allowed to {} {}".format(method, path)
        for (pattern, methods) in self.acl.iteritems():
            if fnmatch.fnmatch(path, pattern) and methods.get(method, no_access)(self.credentials):
                return True
        raise RuntimeError, "You are not allowed to {0} {1}".format(method, path)




# This is an ETDProxy - it looks like a "local" ETDServer but
# it uses the channel to map function calls to protocol and 
# parses protocol replies into return values
rxReplySplit = re.compile(r"((?P<cmd>[^\r\n]*)\r?\n)")
rxLine       = re.compile(r"^(?P<returncode>OK|ERR)(\s+(?P<msg>.+)?)?$")
extract_cmd  = lambda x: x.group('cmd')

class reductor_state(object):
    def __init__(self):
        self.done  = False
        self.state = (True, [])


    def lines(self):
        return self.state[1]

    def __call__(self, dummy, line):
        if self.done:
            print "IGNORING LINE: {0}".format( line )
            return self

        mo = rxLine.match(line)
        if mo is None:
            raise RuntimeError, "'{0}' is NOT a valid reply line".format(line)
        ret        = mo.group('returncode')
        msg        = mo.group('msg') 
        self.done  = (ret=="ERR" or (ret=="OK" and not msg))
        self.state = (self.state[0] and ret=="OK", self.state[1]+([msg] if msg else []))
        return self

    def __str__(self):
        return "State: done={0} state={1}".format(self.done, self.state)


class ETDProxy(ETDInterface):
    def __init__(self, conn, *args, **kwargs):
        super(ETDProxy, self).__init__(*args, **kwargs)
        self.connection = conn
        # peer = (ip, port) - we're local if the peer address is one of
        # the ip addresses of the machine we're executing on
        remoteIP = conn.getpeername()[0]
        self.isRemote   = False if remoteIP in ['127.0.0.1'] else remoteIP
        print "ETDProxy: peer=",self.isRemote

    @trace.trace
    def listPath(self, path):
        self.connection.send("list {0}\n".format(path))
        return self._getReply()

    @trace.trace
    def requestFileWrite(self, path, mode):
        fields = [ (re.compile(r"UUID=(?P<uuid>\S+)"), lambda mo, obj: setattr(obj,'uuid', mo.group('uuid'))),
                   (re.compile(r"alreadyHave=(?P<num>[0-9]+)"),
                       lambda mo, obj: setattr(obj,'alreadyhave', int(mo.group('num')))),
                   (re.compile(r"dataChannel=\s*(?P<proto>\S+)\s+(?P<host>\S+)\s+(?P<port>\S+)"),
                       lambda mo, obj: setattr(obj,'addr',
                             (mo.group('proto'), (None if mo.group('host')=='None' else mo.group('host'), int(mo.group('port')))))) ]
        def reductor(acc, l):
            for (rx, action) in fields:
                mo = rx.search(l)
                action(mo, acc) if mo else None
            return acc
        self.connection.send("write_file {0} {1}\n".format(path, mode))
        o = reduce(reductor, self._getReply(), type('', (), {'uuid':None, 'alreadyhave':None, 'addr':(None, (None, None))})())
        return (o.uuid, o.alreadyhave)

    @trace.trace
    def requestFileRead(self, path, alreadyhave):
        self.connection.send("read_file {0} {1}\n".format(path, alreadyhave))
        fields = [ (re.compile(r"UUID=(?P<uuid>\S+)"), lambda mo, obj: setattr(obj,'uuid', mo.group('uuid'))),
                   (re.compile(r"remain=(?P<number>-?[0-9]+)"),
                    lambda mo, obj: setattr(obj,'remain', int(mo.group('number')))) ]
        def reductor(acc, l):
            for (rx, action) in fields:
                mo = rx.search(l)
                action(mo, acc) if mo else None
            return acc
        o = reduce(reductor, self._getReply(), type('', (), {'remain':-1, 'uuid':None})())
        return (o.uuid, o.remain)

    @trace.trace
    def sendFile(self, path, uuid, alreadyhave, dest):
        # generate command and send it across!
        self.connection.send("send_file {0} {1} {2} {3}\n".format(path, uuid, alreadyhave, "{0}:{1}:{2}".format(dest[0], *dest[1])))
        self._getReply()

    @trace.trace
    def removeUUID(self, uuid):
        self.connection.send("remove_uuid {0}\n".format(uuid))
        self._getReply()

    @trace.trace
    def dataChannelAddr(self):
        self.connection.send("data_channel_addr\n")
        fields = [ (re.compile(r"dataChannel=\s*(?P<proto>\S+)\s+(?P<host>\S+)\s+(?P<port>\S+)"),
                    lambda mo, obj: setattr(obj,'addr',
                          (mo.group('proto'), (None if mo.group('host')=='None' else mo.group('host'), int(mo.group('port')))))) ]
        def reductor(acc, l):
            for (rx, action) in fields:
                mo = rx.search(l)
                action(mo, acc) if mo else None
            return acc
        return reduce(reductor, self._getReply(), type('', (), {'addr':(None, (None, None))})()).addr

    def close(self):
        return self.connection.close()

    @trace.trace
    def _getReply(self):
        # keep on reading until we have either ERR
        # or a single "OK"
        reply = reductor_state()
        txt   = ""
        while not reply.done:
            tmp = self.connection.recv(2048)
            if not tmp:
                raise RuntimeError, "Remote end closed connection"
            txt = txt + tmp
            reply = reduce(reply, map(extract_cmd, rxReplySplit.finditer(txt)), reply)
            # Remember remaining characters for next iteration
            txt  = re.sub(rxReplySplit, "", txt)
        # make sure no dangling text remains
        if txt:
            raise RuntimeError, "There was dangling text '{0}'".format( txt )
        if reply.state[0]==False:
            raise RuntimeError, "\n".join(reply.state[1])
        return reply.lines()




## The server wrapper combines a server and
## a connection - it reads commands from the
## connection and translates those to calls
## to the actual ETDServer
rxCmdSplit   = re.compile(r"((?P<cmd>[^\r\n]*)\r?\n)")
rxList       = re.compile(r"^list\s+(?P<path>\S.*)$", re.I)
rxWriteFile  = re.compile(r"^write_file\s+(?P<path>\S+)\s+(?P<mode>new|append|write)$", re.I)
rxReadFile   = re.compile(r"^read_file\s+(?P<path>\S+)\s+(?P<have>[0-9]+)$", re.I)
rxSendFile   = re.compile(r"^send_file\s+(?P<path>\S+)\s+(?P<uuid>\S+)\s+(?P<start>[0-9]+)\s+(?P<dest>\S+)$", re.I)
rxRemoveUUID = re.compile(r"^remove_uuid\s+(?P<uuid>\S+)$", re.I)
rxDataAddr   = re.compile(r"^data_channel_addr$", re.I)
rxQuit       = re.compile(r"^quit$", re.I)

# deal with unknown commands
rxAny      = re.compile(r"^(.*)$")
def unknownCommand(*args):
    raise RuntimeError, "Unknown command: {0}".format(*args)

class ETDServerWrapper(threading.Thread):
    def __init__(self, conn, evt, *args, **kwargs):
        threading.Thread.__init__(self, name=str(conn))
        #super(ETDServerWrapper, self).__init__(name=str(conn))
        self.connection = conn
        self.event      = evt
        self.etdserver  = ETDServer(*args, **kwargs)
        self.connection.settimeout( 0.1 )
        # the commands we interpret
        self.commands   = [(rxList,       lambda *args: self.listPath(*args)),
                           (rxQuit,       lambda *args: self.quit()),
                           (rxWriteFile,  lambda *args: self.requestFileWrite(*args)),
                           (rxReadFile,   lambda *args: self.requestFileRead(*args)),
                           (rxSendFile,   lambda *args: self.sendFile(*args)),
                           (rxRemoveUUID, lambda *args: self.removeUUID(*args)),
                           (rxDataAddr,   lambda *args: self.dataChannelAddr(*args)),
                           # leave this one as last
                           (rxAny,        lambda *args: unknownCommand(*args)) ]
        # start ourselves
        self.start()

    def run(self):
        print "[{0}]: ETDServerWrapper running - waiting for commands".format( self.name )
        txt = ""
        while True:
            try:
                # event set?
                self.event.wait( 0.01 )
                if self.event.isSet():
                    print "{0}: signalled to shutdown".format( self.name )
                    break
                # attempt to read from sokkit, append to characters we already have
                tmp = self.connection.recv(8192)
                if not tmp:
                    # remote side closed connection
                    break
                txt = txt + tmp
                #print "COMMANDS: {0}".format( list(rxCmdSplit.finditer(txt)) )
                # Extract our commands and execute'm; they are \n limited
                map(lambda x: self.handle(x.group('cmd')), rxCmdSplit.finditer(txt))
                # Remember remaining characters for next iteration
                txt  = re.sub(rxCmdSplit, "", txt)
            except socket.timeout:
                continue
            except Exception as E:
                print "ETDServerWrapper: OH NOES - {0}".format(str(E))
                break
        print "[{0}]: closing connection".format( self.name )
        self.connection.close()
        print "ETDServerWrapper exiting thread"
        # if the control connection has died, we automatically clean up the transfers that we
        # were doing
        for uuid in list(self.etdserver.transfer.keys()):
            print "ETDServerWrapper/clean up UUID={0}".format( uuid )
            self.etdserver.removeUUID(uuid)

    @trace.trace
    def handle(self, command):
        # should use dropwhile to find the first match in the commands
        DROPWHILE = itertools.dropwhile
        IMAP      = itertools.imap
        try:
            (mo, rx, fun) = next(DROPWHILE(lambda mo_f: mo_f[0] is None,
                                           IMAP(lambda rx_f: (rx_f[0].match(command), rx_f[0], rx_f[1]), self.commands)))
            print "Got command {0}, groups={1}".format(rx.pattern, mo.groups())
            map(self.connection.send, fun(*mo.groups()))
        except Exception, E:
            print "[{0}]: Exception ({1}) whilst executing '{2}'".format(self.name, E, command)
            self.connection.send("ERR {0}\n".format(repr(E)))

    @trace.trace
    def quit(self, *args):
        self.connection.close()
        return []
      
    # we make ourselves look like an ETDServer
    # but we transform the output of the call
    # to the real ETDServer object into protocol
    @trace.trace
    def listPath(self, path):
        try:
            return map(lambda x: "OK "+x+"\n", self.etdserver.listPath(path)+[""])
        except Exception, E:
            return ["ERR {0}\n".format(repr(E))]

    @trace.trace
    def requestFileWrite(self, path, mode):
        try:
            (uuid, alreadyhave) = self.etdserver.requestFileWrite(path, mode)
            return map(functools.partial(str.__add__,"OK "),
                       ["UUID={0}\n".format(uuid), "alreadyHave={0}\n".format(alreadyhave), '\n'])
        except Exception, E:
            return ["ERR {0}\n".format(repr(E))]

    @trace.trace
    def requestFileRead(self, path, alreadyhave):
        try:
            (uuid, remain) = self.etdserver.requestFileRead(path, int(alreadyhave))
            return map(functools.partial(str.__add__,"OK "),
                       ["UUID={0}\n".format(uuid), "remain={0}\n".format(remain), '\n'])
        except Exception, E:
            return ["ERR {0}\n".format(repr(E))]

    @trace.trace
    def dataChannelAddr(self):
        try:
            datachannel = self.etdserver.dataChannelAddr()
            return ["OK dataChannel={0} {1} {2}\n".format(datachannel[0], *datachannel[1]), "OK\n"]
        except Exception, E:
            return ["ERR {0}\n".format(repr(E))]

    @trace.trace
    def sendFile(self, path, uuid, alreadyhave, dest):
        try:
            # dest = "proto:host:port"
            dest = dest.split(':')
            self.etdserver.sendFile(path, uuid, int(alreadyhave), (dest[0], (dest[1], int(dest[2]))))
            return ["OK\n"]
        except Exception, E:
            return ["ERR {0}\n".format(repr(E))]

    @trace.trace
    def removeUUID(self, uuid):
        try:
            self.etdserver.removeUUID(uuid)
            return ["OK\n"]
        except Exception, E:
            return ["ERR {0}\n".format(repr(E))]


class ETDDataServer(threading.Thread):
    def __init__(self, dataAddr, xfers):
        threading.Thread.__init__(self, name=repr(dataAddr))
        self.xfers     = xfers
        self.dataAddr  = dataAddr
        self.lissensok = etdc_fd.mk_server(*self.dataAddr)
        self.clients   = []

        # let's not forget to actually run ourselves ...
        self.start()

    @trace.trace
    def run(self):
        print "Starting data channel [",self.dataAddr,"]"
        while True:
            try:
                self.lissensok.settimeout(0.1)
                (newfd, remote_addr) = self.lissensok.accept()
                print "Incoming connection from {0}".format( remote_addr )
                if newfd is None:
                    print "accept() returned None?"
                    continue
                self.clients.append( (newfd, ETDDataHandler(newfd, remote_addr, self.xfers)) )
            except socket.timeout:
                pass
            except Exception, E:
                print "Closing data channel: ", repr(E)
                self.lissensok.close()
                print "Joining data threads ..."
                map(lambda clnt: clnt[0].close(), self.clients)
                print "Dataserver Done."
                break

    @trace.trace
    def close(self):
        self.lissensok.close()

    @trace.trace
    def getsockname(self):
        return self.dataAddr;

rxHDR      = re.compile(r"^({[^}]*})")
rxComma    = re.compile(r",\s+") 
rxKeyValue = re.compile(r"^'([^']+)':\s*(\S.*)$")
xForm_t    = {'fpos': int, 'sz': int, 'uuid': lambda x: x.strip("''")}
identity   = lambda x: x
xForm      = lambda kv: (kv[0], xForm_t.get(kv[0], identity)(kv[1]))

class ETDDataHandler(threading.Thread):
    def __init__(self, fd, rem, xfers):
        threading.Thread.__init__(self, name="{0} {1}".format(fd, rem))
        self.sokkit      = fd
        self.remote_addr = rem
        self.xfers       = xfers
        # and we must start ourselves
        self.start()

    @trace.trace
    def run(self):
        print "ETDDataHandler[{0}]: starting".format( self.remote_addr )
        try:
            msg = ''
            while True:
                # Ok, get the message, this is a header followed by the data
                # MSG = "{'key':value, 'other key':other value, ..}DATA"
                msg = msg + self.sokkit.recv(1024)
                print "ETDDataHandler[{0}]: got {1} bytes".format( self.remote_addr, len(msg))
                x = rxHDR.split(msg)
                if len(x)!=3 and len(msg)>2048:
                    raise RuntimeError, "This is not a proper formatted header:\n{0}".format(msg[:80])
                (_, hdr, data) = rxHDR.split(msg)
                hdr = dict(map(xForm, map(operator.methodcaller('groups'), map(rxKeyValue.match, rxComma.split(hdr.strip("{}"))))))
                print "ETDDataHandler[{0}]: hdr={1}".format( self.remote_addr, hdr )

                uuid = hdr['uuid']
                if uuid not in self.xfers:
                    raise RuntimeError, "ETDDataHandler[{0}]: incoming data for unknown UUID={1}".format(self.remote_addr, uuid)

                # dispatch based on message content
                fd = self.xfers[uuid]['fd']
                n  = hdr['sz']
                if 'push' in hdr:
                    org_n = n 
                    print "ETDDataHandler[{0}]: need to push {1} bytes sz={2}".format(self.remote_addr, n, fd.size())
                    while n>0:
                        b = fd.read(max(n, 2*1024*1024))
                        self.sokkit.send(b)
                        n = n - len(b)
                    print "ETDDataHandler[{0}]: pushed {1} bytes".format(self.remote_addr, org_n - n)
                    break
                else:
                    fpos = hdr['fpos']
                    while len(data)<n:
                        data = data + self.sokkit.recv(n - len(data))
                    # Ok got all data!
                    print "ETDDataHandler[{0}]: got {1} bytes of data payload (expect {2})".format(self.remote_addr, len(data), n)
                    fd.write( data )
                msg = ''
        except Exception, E:
            print "ETDDataHandler[{0}]: {1}".format( self.remote_addr, repr(E))
        print "ETDDataHandler[{0}]: closing connection".format( self.remote_addr )
        self.sokkit.close()
