#-----------------------------------------------------------------------------
# Copyright (c) 2026, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
#-----------------------------------------------------------------------------

# The pyimod03_ctypes bootstrap module contains conditional imports of ctypes; suppress the corresponding references to
# prevent ctypes from being unconditionally bundled with the frozen application.
excludedimports = ['ctypes']
