from distutils.core import setup, Extension

_quickjs = Extension('_quickjs', sources=['module.c'])

setup(name='quickjs',
      version='1.0',
      description='Wrapping the quickjs C library.',
      packages=["quickjs"],
      ext_modules=[_quickjs])
