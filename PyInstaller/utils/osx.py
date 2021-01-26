#-----------------------------------------------------------------------------
# Copyright (c) 2014-2021, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
#-----------------------------------------------------------------------------


"""
Utils for Mac OS X platform.
"""

import os
import shutil

from ..compat import base_prefix
from macholib.MachO import MachO


def is_homebrew_env():
    """
    Check if Python interpreter was installed via Homebrew command 'brew'.

    :return: True if Homebrew else otherwise.
    """
    # Python path prefix should start with Homebrew prefix.
    env_prefix = get_homebrew_prefix()
    if env_prefix and base_prefix.startswith(env_prefix):
        return True
    return False


def is_macports_env():
    """
    Check if Python interpreter was installed via Macports command 'port'.

    :return: True if Macports else otherwise.
    """
    # Python path prefix should start with Macports prefix.
    env_prefix = get_macports_prefix()
    if env_prefix and base_prefix.startswith(env_prefix):
        return True
    return False


def get_homebrew_prefix():
    """
    :return: Root path of the Homebrew environment.
    """
    prefix = shutil.which('brew')
    # Conversion:  /usr/local/bin/brew -> /usr/local
    prefix = os.path.dirname(os.path.dirname(prefix))
    return prefix


def get_macports_prefix():
    """
    :return: Root path of the Macports environment.
    """
    prefix = shutil.which('port')
    # Conversion:  /usr/local/bin/port -> /usr/local
    prefix = os.path.dirname(os.path.dirname(prefix))
    return prefix


def fix_exe_for_code_signing(filename):
    """
    Fixes the Mach-O headers to make code signing possible.

    Code signing on OS X does not work out of the box with embedding
    .pkg archive into the executable.

    The fix is done this way:
    - Make the embedded .pkg archive part of the Mach-O 'String Table'.
      'String Table' is at end of the OS X exe file so just change the size
      of the table to cover the end of the file.
    - Fix the size of the __LINKEDIT segment.

    Mach-O format specification:

    http://developer.apple.com/documentation/Darwin/Reference/ManPages/man5/Mach-O.5.html
    """
    exe_data = MachO(filename)
    # Take the last available header. The executable is either a single-arch
    # thin Mach-O binary, or a universal2 fat binary. In the latter case,
    # we want to adjust the symbol table of the last (arm64) slice.
    header = exe_data.headers[-1]
    # Every load command is a tuple: (cmd_metadata, segment, [section1, section2])
    cmds = header.commands
    # File size needs to be estimated from the file itself, because with
    # fat binaries, header.size does not account for the appended data
    # anymore...
    file_size = os.path.getsize(filename)

    ## Make the embedded .pkg archive part of the Mach-O 'String Table'.
    # Data about 'String Table' is in LC_SYMTAB load command.
    for c in cmds:
        if c[0].get_cmd_name() == 'LC_SYMTAB':
            data = c[1]
            # Increase the size of 'String Table' to cover the embedded .pkg file.
            new_strsize = file_size - (header.offset + data.stroff)
            data.strsize = new_strsize
    ## Fix the size of the __LINKEDIT segment.
    # __LINKEDIT segment data is the 4th item in the executable.
    linkedit = cmds[3][1]
    new_segsize = file_size - (header.offset + linkedit.fileoff)
    linkedit.filesize = new_segsize
    linkedit.vmsize = new_segsize

    ## Write changes back.
    with open(exe_data.filename, 'rb+') as fp:
        exe_data.write(fp)

    ## In fat binaries, we also need to adjust the fat header.
    # At the time of writing, macholib 1.14 does not support this, so
    # we need to do it ourselves...
    if exe_data.fat:
        from macholib.mach_o import FAT_MAGIC, FAT_MAGIC_64
        from macholib.mach_o import fat_header, fat_arch, fat_arch64
        with open(exe_data.filename, 'rb+') as fp:
            # Taken from MachO.load_fat() implementation. The fat
            # header's signature has already been validated when we
            # loaded the file for the first time.
            fat = fat_header.from_fileobj(fp)
            if fat.magic == FAT_MAGIC:
                archs = [fat_arch.from_fileobj(fp) for i in range(fat.nfat_arch)]
            elif fat.magic == FAT_MAGIC_64:
                archs = [fat_arch64.from_fileobj(fp) for i in range(fat.nfat_arch)]
            # Adjust the size in the fat header for the last slice
            arch = archs[-1]
            arch.size = file_size - arch.offset
            # Now write the fat headers back to the file
            fp.seek(0)
            fat.to_fileobj(fp)
            for arch in archs:
                arch.to_fileobj(fp)
