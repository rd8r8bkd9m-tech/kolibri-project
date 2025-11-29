"""
Kolibri Python SDK - Setup Configuration

Build the C extension with:
    python setup.py build_ext --inplace

Install the package with:
    pip install .

Or install in development mode:
    pip install -e .
"""

from setuptools import setup, Extension
import sys
import os

# Compiler flags for optimization
extra_compile_args = []
if sys.platform != 'win32':
    extra_compile_args = [
        '-O3',           # Maximum optimization
        '-ffast-math',   # Fast floating point (not used but doesn't hurt)
    ]
    # Add march=native only on non-Windows platforms and when not cross-compiling
    if os.environ.get('KOLIBRI_PORTABLE', '0') != '1':
        extra_compile_args.append('-march=native')

# Define the C extension module
kolibri_module = Extension(
    'kolibri._kolibri',
    sources=['kolibri/libkolibri.c'],
    extra_compile_args=extra_compile_args,
)

# Read long description from README if available
long_description = "Kolibri Python SDK - Ultra-fast byte-to-decimal encoding"
readme_path = os.path.join(os.path.dirname(__file__), 'README.md')
if os.path.exists(readme_path):
    with open(readme_path, 'r', encoding='utf-8') as f:
        long_description = f.read()

setup(
    name='kolibri-fast',
    version='1.0.0',
    description='Ultra-fast byte-to-decimal encoding',
    long_description=long_description,
    long_description_content_type='text/markdown',
    author='Kolibri Project',
    author_email='kolibri@example.com',
    url='https://github.com/kolibri-project/kolibri',
    packages=['kolibri'],
    ext_modules=[kolibri_module],
    python_requires='>=3.8',
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3.12',
        'Programming Language :: C',
        'Topic :: Software Development :: Libraries',
    ],
    extras_require={
        'test': ['pytest>=6.0'],
    },
)
