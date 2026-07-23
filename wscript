# SPDX-License-Identifier: GPL-3.0-or-later
# SIS - SPARC/RISC-V instruction simulator
#
# Copyright (C) 1995-2026 Jiri Gaisler
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import shutil

from waflib import Context
from waflib.Tools import waf_unit_test

APPNAME = 'sis'
VERSION = '2.30'

SOURCES = [
    'erc32.cc',
    'grlib.cc',
    'leon3.cc',
    'exec.cc',
    'func.cc',
    'help.cc',
    'sparc.cc',
    'riscv.cc',
    'leon2.cc',
    'sis.cc',
    'interf.cc',
    'remote.cc',
    'elf.cc',
    'greth.cc',
    'tap.cc',
    'gr740.cc',
    'rv32.cc',
    'sisio.cc',
]

HEADERS = ['fcntl.h', 'stdlib.h', 'termios.h']


def options(opt):
    opt.load('compiler_c compiler_cxx')
    opt.load('waf_unit_test')
    opt.add_option('--enable-l1cache',
                   action='store_true',
                   default=False,
                   help='enable L1 cache emulation')
    opt.add_option('--enable-coverage',
                   action='store_true',
                   default=False,
                   help='instrument for gcov and build without optimization')
    opt.add_option('--enable-optimization',
                   action='store',
                   default='2',
                   metavar='LEVEL',
                   help='optimization level, -O<level> or the MSVC equivalent '
                   '[default: 2]')


def configure(conf):
    conf.load('compiler_c compiler_cxx')
    conf.load('waf_unit_test')

    # compiler_c is loaded only for check_endianness, which is a C test.
    level = conf.options.enable_optimization

    # gcc changes the arc graph under optimization, so a coverage build is
    # unoptimized to keep the branch count stable across compiler versions.
    if conf.options.enable_coverage:
        if conf.env.CXX_NAME == 'msvc':
            conf.fatal('--enable-coverage needs a gcc compatible compiler')
        level = '0'
        conf.env.append_value('CXXFLAGS', ['--coverage'])
        conf.env.append_value('LINKFLAGS', ['--coverage'])

    if conf.env.CXX_NAME == 'msvc':
        msvc_opt = {'0': '/Od', '1': '/O1', 's': '/Os'}.get(level, '/O2')
        conf.env.append_value('CXXFLAGS',
                              [msvc_opt, '/Zi', '/EHsc', '/std:c++17'])
    else:
        conf.env.append_value('CXXFLAGS', ['-O' + level, '-g', '-std=c++17'])
    conf.env.append_value('DEFINES', ['FAST_UART'])

    for header in HEADERS:
        conf.check_cxx(header_name=header, mandatory=False)

    if conf.check_endianness() == 'big':
        conf.define('WORDS_BIGENDIAN', 1)

    if conf.options.enable_l1cache:
        conf.define('ENABLE_L1CACHE', 1)

    conf.define('PACKAGE_VERSION', VERSION)

    if conf.env.DEST_OS == 'win32':
        conf.env.append_value('LIB', ['ws2_32', 'kernel32'])
        if conf.env.CXX_NAME == 'msvc':
            # POSIX names the CRT still provides, and the deprecation
            # warnings it raises for them.
            conf.env.append_value('DEFINES', ['_CRT_SECURE_NO_WARNINGS',
                                              '_WINSOCK_DEPRECATED_NO_WARNINGS'])

    conf.write_config_header('config.h', remove=False)


def build(bld):
    lib = [] if bld.env.DEST_OS == 'win32' else ['m']

    # The simulator is a static library so that the unit tests can link
    # against it.  The executable is only an entry point.
    bld.stlib(source=SOURCES,
              features='cxx cxxstlib',
              name='sislib',
              target='sis',
              includes=['.'],
              lib=lib)

    bld.program(source=['main.cc'],
                features='cxx cxxprogram',
                target='sis',
                includes=['.'],
                use='sislib',
                lib=lib)

    bld.recurse('tests')
    bld.add_post_fun(waf_unit_test.summary)
    bld.add_post_fun(waf_unit_test.set_exit_code)


def dtb(ctx):
    """regenerate rv32dtb.h from rv32.dts, run from the top of the tree"""
    for tool in ['dtc', 'xxd']:
        if shutil.which(tool) is None:
            ctx.fatal('%s is required to regenerate rv32dtb.h' % tool)
    for cmd in ['dtc -O dtb rv32.dts -o rv32.dtb',
                'xxd --include rv32.dtb > rv32dtb.h']:
        if ctx.exec_command(cmd):
            ctx.fatal('failed: %s' % cmd)


class DtbContext(Context.Context):
    cmd = 'dtb'
    fun = 'dtb'
