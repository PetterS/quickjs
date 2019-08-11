import glob
import sys

from setuptools import setup, Extension

CONFIG_VERSION = '2019-07-28'
extra_link_args = []

if sys.platform == "win32":
    # To build for Windows:
    # 1. Install MingW-W64-builds from https://mingw-w64.org/doku.php/download
    #    It is important to change the default to 64-bit when installing if a
    #    64-bit Python is installed in windows.
    # 2. Put the bin/ folder inside x86_64-8.1.0-posix-seh-rt_v6-rev0 in your
    #    system PATH when compiling.
    # 3. The code below will moneky-patch distutils to work.
    import distutils.cygwinccompiler
    distutils.cygwinccompiler.get_msvcr = lambda: []
    # Escaping works differently.
    CONFIG_VERSION = f'\\"{CONFIG_VERSION}\\"'
    # Make sure that pthreads is linked statically, otherwise we run into problems
    # on computers where it is not installed.
    extra_link_args = ["-Wl,-Bstatic", "-lpthread"]
else:
    CONFIG_VERSION = f'"{CONFIG_VERSION}"'


def get_c_sources(include_headers=False):
    sources = ['module.c'] + glob.glob("third-party/*.c")
    if include_headers:
        sources += glob.glob("third-party/*.h")
    return sources


_quickjs = Extension(
    '_quickjs',
    define_macros=[('CONFIG_VERSION', CONFIG_VERSION)],
    # HACK.
    # See https://github.com/pypa/packaging-problems/issues/84.
    sources=get_c_sources(include_headers=("sdist" in sys.argv)),
    extra_link_args=extra_link_args)

long_description = """
Thin Python wrapper around https://bellard.org/quickjs/ .
"""

setup(author="Petter Strandmark",
      author_email="petter.strandmark@gmail.com",
      name='quickjs',
      url='https://github.com/PetterS/quickjs',
      version='1.5.1',
      description='Wrapping the quickjs C library.',
      long_description=long_description,
      packages=["quickjs"],
      ext_modules=[_quickjs])
