#-----------------------------------------------------------------------------
# Copyright (c) 2005-2023, PyInstaller Development Team.
#
# Distributed under the terms of the GNU General Public License (version 2
# or later) with exception for distributing the bootloader.
#
# The full license is in the file COPYING.txt, distributed with this software.
#
# SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
#-----------------------------------------------------------------------------

# **NOTE** This module is used during bootstrap.
# Import *ONLY* builtin modules or modules that are collected into the base_library.zip archive.
# List of built-in modules: sys.builtin_module_names
# List of modules collected into base_library.zip: PyInstaller.compat.PY3_BASE_MODULES

import sys
import os
import struct
import marshal
import zlib

# In Python3, the MAGIC_NUMBER value is available in the importlib module. However, in the bootstrap phase we cannot use
# importlib directly, but rather its frozen variant.
import _frozen_importlib

PYTHON_MAGIC_NUMBER = _frozen_importlib._bootstrap_external.MAGIC_NUMBER

# For decrypting Python modules.
CRYPT_BLOCK_SIZE = 16

# Type codes for PYZ PYZ entries
PYZ_ITEM_MODULE = 0
PYZ_ITEM_PKG = 1
PYZ_ITEM_DATA = 2
PYZ_ITEM_NSPKG = 3  # PEP-420 namespace package


class ArchiveReadError(RuntimeError):
    pass


class Cipher:
    """
    This class is used only to decrypt Python modules.
    """
    def __init__(self):
        # At build-time the key is given to us from inside the spec file. At bootstrap-time, we must look for it
        # ourselves, by trying to import the generated 'pyi_crypto_key' module.
        import pyimod00_crypto_key
        key = pyimod00_crypto_key.key

        assert type(key) is str
        if len(key) > CRYPT_BLOCK_SIZE:
            self.key = key[0:CRYPT_BLOCK_SIZE]
        else:
            self.key = key.zfill(CRYPT_BLOCK_SIZE)
        assert len(self.key) == CRYPT_BLOCK_SIZE

        import tinyaes
        self._aesmod = tinyaes
        # Issue #1663: Remove the AES module from sys.modules list. Otherwise it interferes with using 'tinyaes' module
        # in users' code.
        del sys.modules['tinyaes']

    def __create_cipher(self, iv):
        # The 'AES' class is stateful, and this factory method is used to re-initialize the block cipher class with
        # each call to xcrypt().
        return self._aesmod.AES(self.key.encode(), iv)

    def decrypt(self, data):
        cipher = self.__create_cipher(data[:CRYPT_BLOCK_SIZE])
        return cipher.CTR_xcrypt_buffer(data[CRYPT_BLOCK_SIZE:])


class ZlibArchiveReader:
    """
    Reader for PyInstaller's PYZ (ZlibArchive) archive. The archive is used to store collected byte-compiled Python
    modules, as individually-compressed entries.
    """
    _PYZ_MAGIC_PATTERN = b'PYZ\0'

    def __init__(self, filename, start_offset=None, check_pymagic=False):
        self._filename = filename
        self._start_offset = start_offset

        self.toc = {}

        self.cipher = None

        # Try to create Cipher() instance; if encryption is not enabled, pyimod00_crypto_key is not available, and
        # instantiation fails with ImportError.
        try:
            self.cipher = Cipher()
        except ImportError:
            pass

        # If no offset is given, try inferring it from filename
        if start_offset is None:
            self._filename, self._start_offset = self._parse_offset_from_filename(filename)

        # Parse header and load TOC. Standard header contains 12 bytes: PYZ magic pattern, python bytecode magic
        # pattern, and offset to TOC (32-bit integer). It might be followed by additional fields, depending on
        # implementation version.
        with open(self._filename, "rb") as fp:
            # Read PYZ magic pattern, located at the start of the file
            fp.seek(self._start_offset, os.SEEK_SET)

            magic = fp.read(len(self._PYZ_MAGIC_PATTERN))
            if magic != self._PYZ_MAGIC_PATTERN:
                raise ArchiveReadError("PYZ magic pattern mismatch!")

            # Read python magic/version number
            pymagic = fp.read(len(PYTHON_MAGIC_NUMBER))
            if check_pymagic and pymagic != PYTHON_MAGIC_NUMBER:
                raise ArchiveReadError("Python magic pattern mismatch!")

            # Read TOC offset
            toc_offset, *_ = struct.unpack('!i', fp.read(4))

            # Load TOC
            fp.seek(self._start_offset + toc_offset, os.SEEK_SET)
            self.toc = dict(marshal.load(fp))

    @staticmethod
    def _parse_offset_from_filename(filename):
        """
        Parse the numeric offset from filename, stored as: `/path/to/file?offset`.
        """
        offset = 0

        idx = filename.rfind('?')
        if idx == -1:
            return filename, offset

        try:
            offset = int(filename[idx + 1:])
            filename = filename[:idx]  # Remove the offset from filename
        except ValueError:
            # Ignore spurious "?" in the path (for example, like in Windows UNC \\?\<path>).
            pass

        return filename, offset

    def is_package(self, name):
        """
        Check if the given name refers to a package entry. Used by FrozenImporter at runtime.
        """
        entry = self.toc.get(name)
        if entry is None:
            return False
        typecode, entry_offset, entry_length = entry
        return typecode in (PYZ_ITEM_PKG, PYZ_ITEM_NSPKG)

    def is_pep420_namespace_package(self, name):
        """
        Check if the given name refers to a namespace package entry. Used by FrozenImporter at runtime.
        """
        entry = self.toc.get(name)
        if entry is None:
            return False
        typecode, entry_offset, entry_length = entry
        return typecode == PYZ_ITEM_NSPKG

    def extract(self, name, raw=False):
        """
        Extract data from entry with the given name.

        If the entry belongs to a module or a package, the data is loaded (unmarshaled) into code object. To retrieve
        raw data, set `raw` flag to True.
        """
        # Look up entry
        entry = self.toc.get(name)
        if entry is None:
            return None
        typecode, entry_offset, entry_length = entry

        # Read data blob
        try:
            with open(self._filename, "rb") as fp:
                fp.seek(self._start_offset + entry_offset)
                obj = fp.read(entry_length)
        except FileNotFoundError:
            # We open the archive file each time we need to read from it, to avoid locking the file by keeping it open.
            # This allows executable to be deleted or moved (renamed) while it is running, which is useful in certain
            # scenarios (e.g., automatic update that replaces the executable). The caveat is that once the executable is
            # renamed, we cannot read from its embedded PYZ archive anymore. In such case, exit with informative
            # message.
            raise SystemExit(
                f"{self._filename} appears to have been moved or deleted since this application was launched. "
                "Continouation from this state is impossible. Exiting now."
            )

        try:
            if self.cipher:
                obj = self.cipher.decrypt(obj)
            obj = zlib.decompress(obj)
            if typecode in (PYZ_ITEM_MODULE, PYZ_ITEM_PKG, PYZ_ITEM_NSPKG) and not raw:
                obj = marshal.loads(obj)
        except EOFError as e:
            raise ImportError(f"Failed to unmarshal PYZ entry {name!r}!") from e

        return obj


class NotAnArchiveError(TypeError):
    pass


# Type codes for CArchive TOC entries
PKG_ITEM_BINARY = 'b'  # binary
PKG_ITEM_DEPENDENCY = 'd'  # runtime option
PKG_ITEM_PYZ = 'z'  # zlib (pyz) - frozen Python code
PKG_ITEM_ZIPFILE = 'Z'  # zlib (pyz) - frozen Python code
PKG_ITEM_PYPACKAGE = 'M'  # Python package (__init__.py)
PKG_ITEM_PYMODULE = 'm'  # Python module
PKG_ITEM_PYSOURCE = 's'  # Python script (v3)
PKG_ITEM_DATA = 'x'  # data
PKG_ITEM_RUNTIME_OPTION = 'o'  # runtime option
PKG_ITEM_SPLASH = 'l'  # splash resources


class CArchiveReader:
    """
    Reader for PyInstaller's CArchive (PKG) archive.
    """

    # Cookie - holds some information for the bootloader. C struct format definition. '!' at the beginning means network
    # byte order. C struct looks like:
    #
    #   typedef struct _cookie {
    #       char magic[8]; /* 'MEI\014\013\012\013\016' */
    #       uint32_t len;  /* len of entire package */
    #       uint32_t TOC;  /* pos (rel to start) of TableOfContents */
    #       int  TOClen;   /* length of TableOfContents */
    #       int  pyvers;   /* new in v4 */
    #       char pylibname[64];    /* Filename of Python dynamic library. */
    #   } COOKIE;
    #
    _COOKIE_MAGIC_PATTERN = b'MEI\014\013\012\013\016'

    _COOKIE_FORMAT = '!8sIIii64s'
    _COOKIE_LENGTH = struct.calcsize(_COOKIE_FORMAT)

    # TOC entry:
    #
    #   typedef struct _toc {
    #       int  structlen;  /* len of this one - including full len of name */
    #       uint32_t pos;    /* pos rel to start of concatenation */
    #       uint32_t len;    /* len of the data (compressed) */
    #       uint32_t ulen;   /* len of data (uncompressed) */
    #       char cflag;      /* is it compressed (really a byte) */
    #       char typcd;      /* type code -'b' binary, 'z' zlib, 'm' module,
    #                         * 's' script (v3),'x' data, 'o' runtime option  */
    #       char name[1];    /* the name to save it as */
    #                        /* starting in v5, we stretch this out to a mult of 16 */
    #   } TOC;
    #
    _TOC_ENTRY_FORMAT = '!iIIIBB'
    _TOC_ENTRY_LENGTH = struct.calcsize(_TOC_ENTRY_FORMAT)

    def __init__(self, filename):
        self._filename = filename
        self._start_offset = 0
        self._toc_offset = 0
        self._toc_length = 0

        self.toc = {}

        # Load TOC
        with open(self._filename, "rb") as fp:
            # Find cookie MAGIC pattern
            cookie_start_offset = self._find_magic_pattern(fp, self._COOKIE_MAGIC_PATTERN)
            if cookie_start_offset == -1:
                raise ArchiveReadError("Could not find COOKIE magic pattern!")

            # Read the whole cookie
            fp.seek(cookie_start_offset, os.SEEK_SET)
            cookie_data = fp.read(self._COOKIE_LENGTH)

            magic, archive_length, toc_offset, toc_length, pyvers, pylib_name = \
                struct.unpack(self._COOKIE_FORMAT, cookie_data)

            # Compute start of the the archive
            self._start_offset = (cookie_start_offset + self._COOKIE_LENGTH) - archive_length

            # Verify that Python shared library name is set
            if not pylib_name:
                raise ArchiveReadError("Python shared library name not set in the archive!")

            # Read whole toc
            fp.seek(self._start_offset + toc_offset)
            toc_data = fp.read(toc_length)

            self.toc = self._parse_toc(toc_data)

    @staticmethod
    def _find_magic_pattern(fp, magic_pattern):
        # Start at the end of file, and scan back-to-start
        fp.seek(0, os.SEEK_END)
        end_pos = fp.tell()

        # Scan from back
        SEARCH_CHUNK_SIZE = 8192
        magic_offset = -1
        while end_pos >= len(magic_pattern):
            start_pos = max(end_pos - SEARCH_CHUNK_SIZE, 0)
            chunk_size = end_pos - start_pos
            # Is the remaining chunk large enough to hold the pattern?
            if chunk_size < len(magic_pattern):
                break
            # Read and scan the chunk
            fp.seek(start_pos, os.SEEK_SET)
            buf = fp.read(chunk_size)
            pos = buf.rfind(magic_pattern)
            if pos != -1:
                magic_offset = start_pos + pos
                break
            # Adjust search location for next chunk; ensure proper overlap
            end_pos = start_pos + len(magic_pattern) - 1

        return magic_offset

    @classmethod
    def _parse_toc(cls, data):
        toc = {}
        cur_pos = 0
        while cur_pos < len(data):
            # Read and parse the fixed-size TOC entry header
            entry_length, entry_offset, data_length, uncompressed_length, compression_flag, typecode = \
                struct.unpack(cls._TOC_ENTRY_FORMAT, data[cur_pos:(cur_pos + cls._TOC_ENTRY_LENGTH)])
            cur_pos += cls._TOC_ENTRY_LENGTH
            # Read variable-length name
            name_length = entry_length - cls._TOC_ENTRY_LENGTH
            name, *_ = struct.unpack(f'{name_length}s', data[cur_pos:(cur_pos + name_length)])
            cur_pos += name_length
            # Name string may contain up to 15 bytes of padding
            name = name.rstrip(b'\0').decode('utf-8')

            typecode = chr(typecode)

            # TODO: handle duplicates
            toc[name] = (entry_offset, data_length, uncompressed_length, compression_flag, typecode)

        return toc

    def extract(self, name):
        """
        Extract data for the given entry name.
        """

        entry = self.toc.get(name)
        if entry is None:
            raise KeyError(f"No entry named {name} found in the archive!")

        entry_offset, data_length, uncompressed_length, compression_flag, typecode = entry
        with open(self._filename, "rb") as fp:
            fp.seek(self._start_offset + entry_offset, os.SEEK_SET)
            data = fp.read(data_length)

        if compression_flag:
            import zlib
            data = zlib.decompress(data)

        return data

    def open_embedded_archive(self, name):
        """
        Open new archive reader for the embedded archive.
        """

        entry = self.toc.get(name)
        if entry is None:
            raise KeyError(f"No entry named {name} found in the archive!")

        entry_offset, data_length, uncompressed_length, compression_flag, typecode = entry

        if typecode == PKG_ITEM_PYZ:
            # Open as embedded archive, without extraction.
            return ZlibArchiveReader(self._filename, self._start_offset + entry_offset)
        elif typecode == PKG_ITEM_ZIPFILE:
            raise NotAnArchiveError("Zipfile archives not supported yet!")
        else:
            raise NotAnArchiveError(f"Entry {name} is not a supported embedded archive!")
