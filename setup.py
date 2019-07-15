import setuptools
from distutils.core import setup, Extension

_quickjs = Extension('_quickjs',
                     define_macros=[('CONFIG_VERSION', '"2019-07-09"')],
                     sources=[
                         'module.c', 'third-party/quickjs.c', 'third-party/cutils.c',
                         'third-party/libregexp.c', 'third-party/libunicode.c'
                     ])

setup(name='quickjs',
      version='1.0.2',
      description='Wrapping the quickjs C library.',
      packages=["quickjs"],
      ext_modules=[_quickjs])
