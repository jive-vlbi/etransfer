# interface + concrete implementations of wrappers:
#   wrap connection specifics into a generic API for 
#   sending/receiving bytes
import socket, trace, os

try:
    import udt
    haveUDT = True
except ImportError:
    haveUDT = False

trace.dont_trace()

# base class
class etdc_fd(object):
    def __init__(self, *args, **kwargs):
        pass


# concrete socket classes
class etdc_tcp(etdc_fd):
    def __init__(self, *args, **kwargs):
        super(etdc_tcp, self).__init__(*args, **kwargs)
        self.socket = kwargs.get('socket', lambda : socket.socket(socket.AF_INET, socket.SOCK_STREAM))()
    
    def getsockname(self):
        # return bound information
        return ("tcp", self.ip, self.port)

    def getpeername(self):
        return self.socket.getpeername()

    def accept(self):
        # we should return a new instance of ourselves!
        (newfd, remote_addr) = self.socket.accept()
        return (etdc_tcp(socket=lambda : newfd), remote_addr)

    def close(self):
        return self.socket.close()

    def settimeout(self, to):
        return self.socket.settimeout(to)

    def recv(self, *args):
        return self.socket.recv(*args)

    def read(self, *args):
        return self.recv(*args)

    def write(self, *args):
        return self.send(*args)

    def connect(self, *args):
        return self.socket.connect(*args)

    def setsockopt(self, *args):
        return self.socket.setsockopt(*args)

    def bind(self, *args):
        (ip, port) = args[0] if args else (None, None)
        ip         = '0.0.0.0' if ip is None else ip
        port       = 0         if port is None else port
        return self.socket.bind((ip, port))

    def listen(self, *args):
        return self.socket.listen(*args)

    def send(self, *args):
        return self.socket.send(*args)

    def tell(self, *args):
        raise RuntimeError, "Seek not supported on tcp socket"

    def seek(self, *args):
        raise RuntimeError, "Seek not supported on tcp socket"

    def size(self):
        raise RuntimeError, "size not supported on tcp socket"




class etdc_udt(etdc_fd):
    bufSize = 256 * 1024 * 1024
    MTU     = 1500

    def __init__(self, *args, **kwargs):
        super(etdc_udt, self).__init__(*args, **kwargs)
        if not haveUDT:
            raise RuntimeError, "UDT socket requested but module not available"
        self.socket = kwargs.get('socket', lambda : udt.socket(socket.AF_INET, socket.SOCK_STREAM, 0))()

    def getpeername(self):
        return self.socket.getpeername()

    def accept(self):
        # we should return a new instance of ourselves!
        (newfd, remote_addr) = self.socket.accept()
        return (etdc_udt(socket=lambda : newfd), remote_addr)

    def close(self):
        return self.socket.close()

    def settimeout(self, to):
        #return self.socket.settimeout(to)
        pass

    def recv(self, buf):
        return self.socket.recv(buf, 0)

    def read(self, *args):
        return self.recv(*args)

    def write(self, *args):
        return self.send(*args)

    def connect(self, *args):
        return self.socket.connect(*args)

    def setsockopt(self, *args):
        return self.socket.setsockopt(*args)

    def bind(self, *args):
        (ip, port) = args[0] if args else (None, None)
        ip         = '0.0.0.0' if ip is None else ip
        port       = 0         if port is None else port
        return self.socket.bind((ip, port))

    def listen(self, *args):
        return self.socket.listen(*args)

    def send(self, buf):
        return self.socket.send(buf, len(buf))

    def tell(self, *args):
        raise RuntimeError, "Seek not supported on tcp socket"

    def seek(self, *args):
        raise RuntimeError, "Seek not supported on tcp socket"

    def size(self):
        raise RuntimeError, "size not supported on tcp socket"

    def tell(self, *args):
        raise RuntimeError, "Seek not supported on udt socket"

    def seek(self, *args):
        raise RuntimeError, "Seek not supported on udt socket"

    def size(self):
        raise RuntimeError, "size not supported on udt socket"




class etdc_file(etdc_fd):
    def __init__(self, path, mode):
        super(etdc_file, self).__init__()
        self.path = path
        self.fd   = open(self.path, mode)

    def get_sockname(self):
        return ("file", self.path, 0)

    def read(self, *args):
        return self.fd.read(*args)

    def write(self, *args):
        return self.fd.write(*args)

    def tell(self, *args):
        return self.fd.tell( *args )

    def seek(self, *args):
        return self.fd.seek( *args )

    def size(self):
        fp = self.fd.tell()
        self.fd.seek(0, os.SEEK_END)
        l  = self.fd.tell()
        self.fd.seek(fp, os.SEEK_SET)
        return l

    def close(self):
        return self.fd.close()



# fd factory - for each protocol how to create the basic channel
protocols = {'tcp':etdc_tcp, 'udt':etdc_udt, 'file':etdc_file}

# per protocol functions to transform the thing into a server
server_setup = {
    'tcp': lambda fd, *args, **kwargs:
            (fd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1),
             fd.bind(*args),
             fd.listen(kwargs.get('backlog', 4))),

    'udt': lambda fd, *args, **kwargs:
            (fd.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, True),
             fd.setsockopt(0, udt.UDT_SNDBUF, kwargs.get('bufSize', etdc_udt.bufSize)),
             fd.setsockopt(0, udt.UDT_RCVBUF, kwargs.get('bufSize', etdc_udt.bufSize)),
             fd.setsockopt(0, udt.UDT_MSS,    kwargs.get('MTU',     etdc_udt.MTU)),
             fd.bind(*args),
             fd.listen(kwargs.get('backlog', 4)))
}

client_setup = {
    'tcp': lambda fd, *args, **kwargs: fd.connect(*args),
    'udt': lambda fd, *args, **kwargs: 
                (fd.setsockopt(0, udt.UDT_SNDBUF, kwargs.get('bufSize', etdc_udt.bufSize)),
                 fd.setsockopt(0, udt.UDT_RCVBUF, kwargs.get('bufSize', etdc_udt.bufSize)),
                 fd.setsockopt(0, udt.UDT_MSS,    kwargs.get('MTU',     etdc_udt.MTU)),
                 fd.connect(*args)),
}

no_op = lambda *args, **kwargs: None

# connection factories
@trace.trace
def mk_server(connection, *args, **kwargs):
    fd = protocols[connection](*args, **kwargs)
    # call whatever is needed to make it into a server
    server_setup.get(connection, no_op)( fd, *args, **kwargs )
    return fd

@trace.trace
def mk_client(connection, *args, **kwargs):
    fd = protocols[connection](*args, **kwargs)
    # call whatever is needed to make it into a client
    client_setup.get(connection, no_op)( fd, *args, **kwargs )
    return fd

