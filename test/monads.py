Just    = type('Just', (), {'__init__': lambda x, y: setattr(x, 'value', y), '__str__':lambda x: "Just {0}".format(getattr(x,'value'))})
Nothing = type('Nothing', (), {})

# the maybe decorator
def Maybe(f):
    def call(*args):
        if Nothing in args:
            return Nothing
        res = f(*map(lambda x:x.value if isinstance(x, Just) else x, args))
        return Nothing if res is Nothing else (res if isinstance(res, Just) else Just(res))
    return call


import math

@Maybe
def positive(x):
    return Just(x) if x>0 else Nothing

@Maybe
def log(x):
    return Just(math.log(x)) if x>0 else Nothing

@Maybe
def add(x, y):
    return x+y
