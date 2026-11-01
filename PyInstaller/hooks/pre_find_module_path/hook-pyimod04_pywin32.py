# -----------------------------------------------------------------------------
# Copyright (c) 2026, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
# -----------------------------------------------------------------------------

import os

from PyInstaller import compat, PACKAGEPATH


def pre_find_module_path(api):
    if compat.is_win:
        api.search_dirs = [os.path.join(PACKAGEPATH, 'loader')]
    else:
        api.search_dirs = []
