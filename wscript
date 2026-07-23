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

from waflib import Context

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
    opt.add_option('--enable-l1cache',
                   action='store_true',
                   default=False,
                   help='enable L1 cache emulation')
    opt.add_option('--enable-optimization',
                   action='store',
                   default='2',
                   metavar='LEVEL',
                   help='optimization level passed to -O [default: 2]')


def configure(conf):
    conf.load('compiler_c compiler_cxx')

    # compiler_c is loaded only for check_endianness, which is a C test.
    level = conf.options.enable_optimization
    if conf.env.CXX_NAME == 'msvc':
        conf.env.append_value(
            'CXXFLAGS',
            ['/Od' if level == '0' else '/O2', '/Zi', '/EHsc', '/std:c++17'])
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

    bld.program(source=SOURCES,
                features='cxx cxxprogram',
                target='sis',
                includes=['.'],
                lib=lib)


def dtb(ctx):
    """regenerate rv32dtb.h from rv32.dts"""
    for tool in ['dtc', 'xxd']:
        if not ctx.cmd_and_log(['which', tool], quiet=Context.BOTH,
                               output=Context.STDOUT).strip():
            ctx.fatal('%s is required to regenerate rv32dtb.h' % tool)
    ctx.exec_command('dtc -O dtb rv32.dts -o rv32.dtb')
    ctx.exec_command('xxd --include rv32.dtb > rv32dtb.h')


class DtbContext(Context.Context):
    cmd = 'dtb'
    fun = 'dtb'
