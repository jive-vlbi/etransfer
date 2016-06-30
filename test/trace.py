obj = type('', (), {})()
obj.level = 0

class trace_ctx:
    def __enter__(self):
        self.level = obj.level; self.value = '<Not called?>';
        obj.level += 1
        return self

    def __call__(self, f, *args, **kwargs):
        print "TRACE[{0:2d}]:{1} {2}({3}, {4})".format(self.level, ' '*(self.level*2), f.func_name,
                                                       ','.join(map(repr,args)), repr(kwargs)[1:-1])
        self.value = f(*args, **kwargs)
        return self.value

    def __exit__(self, tp, val, tb):
        print "TRACE[{0:2d}]:{1} {2}".format(self.level, ' '* (self.level * 2), repr(val) if tp is not None else self.value) 
        obj.level -= 1

def trace_yes(f):
    def do_it(*args, **kwargs):
        with trace_ctx() as ctx:
            return ctx(f, *args, **kwargs)
    return do_it

def trace_no(f):
    return f


trace = trace_yes

def do_trace():
    global trace
    trace = trace_yes

def dont_trace():
    global trace
    trace = trace_no

