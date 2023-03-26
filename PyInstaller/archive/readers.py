#-----------------------------------------------------------------------------
# Copyright (c) 2013-2023, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
#-----------------------------------------------------------------------------
"""
Python-based archive reader implementations. Forwarded to pyimod01_archive module.
"""

from PyInstaller.loader.pyimod01_archive import ZlibArchiveReader  # noqa: F401
from PyInstaller.loader.pyimod01_archive import CArchiveReader  # noqa: F401
from PyInstaller.loader.pyimod01_archive import ArchiveReadError, NotAnArchiveError  # noqa: F401
from PyInstaller.loader.pyimod01_archive import (  # noqa: F401
    PKG_ITEM_BINARY,
    PKG_ITEM_DEPENDENCY,
    PKG_ITEM_PYZ,
    PKG_ITEM_ZIPFILE,
    PKG_ITEM_PYPACKAGE,
    PKG_ITEM_PYMODULE,
    PKG_ITEM_PYSOURCE,
    PKG_ITEM_DATA,
    PKG_ITEM_RUNTIME_OPTION,
    PKG_ITEM_SPLASH,
)


def pkg_archive_contents(filename, recursive=True):
    """
    List the contents of the PKG / CArchive. If `recursive` flag is set (the default), the contents of the embedded PYZ
    archive is included as well.

    Used by the tests.
    """

    contents = []

    pkg_archive = CArchiveReader(filename)
    for name, toc_entry in pkg_archive.toc.items():
        *_, typecode = toc_entry
        contents.append(name)
        if typecode == PKG_ITEM_PYZ and recursive:
            pyz_archive = pkg_archive.open_embedded_archive(name)
            for name in pyz_archive.toc.keys():
                contents.append(name)

    return contents
