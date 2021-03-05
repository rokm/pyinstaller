#-----------------------------------------------------------------------------
# Copyright (c) 2013-2021, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
#-----------------------------------------------------------------------------


# import a very simple and rarely used pure-python lib ...
import getopt
# ... and a module importing a shared lib
import ssl

print('Hello World!')

# try reading secret from a file in sub-directory
import sys  # noqa: E402
import os  # noqa: E402

secret_file = os.path.join(sys._MEIPASS, 'test_data', 'secret.txt')
with open(secret_file, 'r') as fp:
    secret = fp.read().strip()
print("Read secret from %s: %r" % (secret_file, secret))
assert secret == 'Secret1234'

# import numpy, which has several extensions in its package subdirectories
import numpy  # noqa: E402, F401
print("Imported numpy!")
