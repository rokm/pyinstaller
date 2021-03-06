#-----------------------------------------------------------------------------
# Copyright (c) 2021, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
#-----------------------------------------------------------------------------

from PyInstaller.archive import writers, readers

import os
import hashlib

import pytest


@pytest.mark.large_data
@pytest.mark.parametrize("num_files,file_size", [
    (5,    100),  # 5x100 MiB: sanity check
    (6,    512),  # 6x512 MiB: large archive; test entry offsets
    (1, 3*1024),  # 1x3 GiB: large archive; test entry size
])
def test_carchive_large_data_files(tmpdir, num_files, file_size):
    archive_file = tmpdir.join('archive.dat')
    file_hashes = {}
    toc = []

    # Create source data files...
    tmpdir.mkdir('src')
    for i in range(num_files):
        dst_filename = 'file{0}.dat'.format(i)
        src_filename = tmpdir.join('src', dst_filename)
        # Write file
        with open(src_filename, 'wb') as fp:
            fp.write(os.urandom(file_size*1024*1024))  # file_size in MiB
        # Compute the file hash
        with open(src_filename, "rb") as fp:
            file_hash = hashlib.md5()
            chunk = fp.read(8192)
            while chunk:
                file_hash.update(chunk)
                chunk = fp.read(8192)
            file_hashes[dst_filename] = file_hash.hexdigest()
        # Create TOC entry
        toc_entry = dst_filename, src_filename, 1, 'x'
        toc.append(toc_entry)

    # ... and gather them into CArchive
    writer = writers.CArchiveWriter(archive_file, toc, 'dummyname')
    del writer

    # Clean up the source files to immediately free the space
    tmpdir.join('src').remove()

    # Open the CArchive, and read the files from it
    reader = readers.CArchiveReader(archive_file)
    for data_file_name, data_file_hash in file_hashes.items():
        # Extract file data...
        _, content = reader.extract(data_file_name)
        # ... and validate its hash
        file_hash = hashlib.md5()
        file_hash.update(content)
        assert data_file_hash == file_hash.hexdigest()
