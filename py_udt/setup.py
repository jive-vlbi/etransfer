from distutils.core import setup, Extension

from os.path import join

module = Extension(
    '_udt',
    sources = ['udt_py.cxx'],
    include_dirs = [".", "../libudt4"],
    libraries = ['udt4'],
    library_dirs = ['../libudt4']#,
    #extra_link_args = ['-Wl,-R../libudt4'],
)

setup (
    name = 'udt_py',
    version = '1.0',
    description = 'Python bindings for UDT',
    ext_modules = [module]
)
