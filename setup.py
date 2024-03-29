# -*- encoding: utf-8 -*-

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension

setup(
    name='pywinble',
    version='0.2.8',
    description='Clears the contents of bytes or integers containing cryptographic material',
    author=u'Erik Aronesty',
    author_email='erik@getvida.io',
    url='https://github.com/vidaid/pywinble',
    license='MIT',
    ext_modules=[Extension('pywinble', ['pywinble.cpp'],
        extra_compile_args = ["/std:c++17",],

        )],
)
