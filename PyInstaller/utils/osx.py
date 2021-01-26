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
from macholib.mach_o import LC_SEGMENT_64, LC_SYMTAB, LC_CODE_SIGNATURE


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

    Note: the above fix works only if the single-arch thin executable or
    the last arch slice in a multi-arch fat executable is not signed,
    because LC_CODE_SIGNATURE comes after LC_SYMTAB, and because modification
    of headers invalidates the code signature. On modern arm64 macOS, code
    signature is mandatory, and therefore compilers create a dummy
    signature when executable is built. In such cases, that signature
    needs to be removed before this function is called.

    Mach-O format specification:

    http://developer.apple.com/documentation/Darwin/Reference/ManPages/man5/Mach-O.5.html
    """
    # Estimate the file size after data was appended
    file_size = os.path.getsize(filename)

    # Take the last available header. A single-arch thin binary contains a
    # single slice, while a multi-arch fat binary contains multiple, and we
    # need to modify the last one, which is adjacent to the appended data.
    executable = MachO(filename)
    header = executable.headers[-1]

    # Sanity check: ensure the executable slice is not signed (otherwise
    # signature's section comes last in the __LINKEDIT segment).
    sign_sec = [cmd for cmd in header.commands
                if cmd[0].cmd == LC_CODE_SIGNATURE]
    assert len(sign_sec) == 0, "Executable contains code signature!"

    # Find __LINKEDIT segment by name (16-byte zero padded string)
    __LINKEDIT_NAME = b'__LINKEDIT\x00\x00\x00\x00\x00\x00'
    linkedit_seg = [cmd for cmd in header.commands
                    if cmd[0].cmd == LC_SEGMENT_64
                    and cmd[1].segname == __LINKEDIT_NAME]
    assert len(linkedit_seg) == 1, "Expected exactly one __LINKEDIT segment!"
    linkedit_seg = linkedit_seg[0][1]  # Take the segment command entry
    # Find SYMTAB section
    symtab_sec = [cmd for cmd in header.commands
                  if cmd[0].cmd == LC_SYMTAB]
    assert len(symtab_sec) == 1, "Expected exactly one SYMTAB section!"
    symtab_sec = symtab_sec[0][1]  # Take the symtab command entry
    # Sanity check; the string table is located at the end of the SYMTAB
    # section, which in turn is the last section in the __LINKEDIT segment
    assert linkedit_seg.fileoff + linkedit_seg.filesize == \
           symtab_sec.stroff + symtab_sec.strsize, "Sanity check failed!"

    # Compute the old/declared file size (header.offset is zero for
    # single-arch thin binaries)
    old_file_size = \
        header.offset + linkedit_seg.fileoff + linkedit_seg.filesize
    delta = file_size - old_file_size
    # Expand the string table in SYMTAB section...
    symtab_sec.strsize += delta
    # .. as well as its parent __LINEDIT segment
    linkedit_seg.filesize += delta
    # FIXME: do we actually need to adjust in-memory size as well? It
    # seems unnecessary, as we have no use for the extended part being
    # loaded in the executable's address space...
    #linkedit_seg.vmsize += delta

    # NOTE: according to spec, segments need to be aligned to page
    # boundaries: 0x4000 (16 kB) for arm64, 0x1000 (4 kB) for other arches.
    # But it seems we can get away without rounding and padding the segment
    # size - perhaps because it's the last one?

    # Write changes
    with open(filename, 'rb+') as fp:
        executable.write(fp)

    # In fat binaries, we also need to adjust the fat header. macholib as
    # of version 1.14 does not support this, so we need to do it ourselves...
    if executable.fat:
        from macholib.mach_o import FAT_MAGIC, FAT_MAGIC_64
        from macholib.mach_o import fat_header, fat_arch, fat_arch64
        with open(filename, 'rb+') as fp:
            # Taken from MachO.load_fat() implementation. The fat
            # header's signature has already been validated when we
            # loaded the file for the first time.
            fat = fat_header.from_fileobj(fp)
            if fat.magic == FAT_MAGIC:
                archs = [fat_arch.from_fileobj(fp)
                         for i in range(fat.nfat_arch)]
            elif fat.magic == FAT_MAGIC_64:
                archs = [fat_arch64.from_fileobj(fp)
                         for i in range(fat.nfat_arch)]
            # Adjust the size in the fat header for the last slice
            arch = archs[-1]
            arch.size = file_size - arch.offset
            # Now write the fat headers back to the file
            fp.seek(0)
            fat.to_fileobj(fp)
            for arch in archs:
                arch.to_fileobj(fp)
