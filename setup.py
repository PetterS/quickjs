import glob
import sys

from setuptools import setup, Extension


def get_c_sources(include_headers=False):
    sources = ['module.c'] + glob.glob("third-party/*.c")
    if include_headers:
        sources += glob.glob("third-party/*.h")
    return sources


_quickjs = Extension(
    '_quickjs',
    define_macros=[('CONFIG_VERSION', '"2019-07-09"')],
    # HACK.
    # See https://github.com/pypa/packaging-problems/issues/84.
    sources=get_c_sources(include_headers=("sdist" in sys.argv)),
    headers=glob.glob("third-party/*.h"))

long_description = """
Thin Python wrapper around https://bellard.org/quickjs/ .
"""

setup(author="Petter Strandmark",
      author_email="petter.strandmark@gmail.com",
      name='quickjs',
      url='https://github.com/PetterS/quickjs',
      version='1.2.0',
      description='Wrapping the quickjs C library.',
      long_description=long_description,
      packages=["quickjs"],
      ext_modules=[_quickjs])
