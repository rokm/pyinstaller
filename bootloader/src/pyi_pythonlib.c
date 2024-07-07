/*
 * ****************************************************************************
 * Copyright (c) 2013-2023, PyInstaller Development Team.
 *
 * Distributed under the terms of the GNU General Public License (version 2
 * or later) with exception for distributing the bootloader.
 *
 * The full license is in the file COPYING.txt, distributed with this software.
 *
 * SPDX-License-Identifier: (GPL-2.0-or-later WITH Bootloader-exception)
 * ****************************************************************************
 */

/*
 * Functions to load, initialize and launch Python.
 */
/* size of buffer to store the name of the Python shared library */
#define MAX_DLL_NAME_LEN 64

#ifdef _WIN32
    #include <windows.h> /* HMODULE */
#else
    #include <dlfcn.h>  /* dlerror */
    #include <stdlib.h>  /* mbstowcs */
#endif /* ifdef _WIN32 */
#include <stddef.h>  /* ptrdiff_t */
#include <stdio.h>
#include <string.h>

/* PyInstaller headers. */
#include "pyi_pythonlib.h"
#include "pyi_global.h"
#include "pyi_path.h"
#include "pyi_archive.h"
#include "pyi_main.h"
#include "pyi_utils.h"
#include "pyi_python.h"
#include "pyi_pyconfig.h"

/*
 * Load the Python shared library, and bind all required symbols from it.
 */
int
pyi_pylib_load(struct PYI_CONTEXT *pyi_ctx)
{
    const struct ARCHIVE *archive = pyi_ctx->archive;
    char dll_name[MAX_DLL_NAME_LEN];
    size_t dll_name_len;
    char dll_fullpath[PYI_PATH_MAX];

    /* On AIX, the name of shared library path might be an archive, because
     * the 'ar' archives can be used for both static and shared objects.
     * A shared library can be loaded from such an archive like this:
     *   dlopen("libpythonX.Y.a(libpythonX.Y.so)", RTLD_MEMBER)
     * This means that if python library name ends with ˙.a˙ suffix, we
     * need to change it into:
     *   libpython3.6.a(libpython3.6.so)
     * Shared libraries whose names end with .so may be used as-is. */
#ifdef AIX
    /* Determine if shared lib is in `libpython?.?.so` or
     * `libpython?.?.a(libpython?.?.so)` format. */
    char *p;
    if ((p = strrchr(archive->python_libname, '.')) != NULL && strcmp(p, ".a") == 0) {
        uint32_t pyver_major;
        uint32_t pyver_minor;

        pyver_major = archive->python_version / 100;
        pyver_minor = archive->python_version % 100;

        dll_name_len = snprintf(
            dllname,
            DLLNAME_LEN,
            "libpython%d.%d.a(libpython%d.%d.so)",
            pyver_major,
            pyver_minor,
            pyver_major,
            pyver_minor
        );
    } else {
        dll_name_len = snprintf(dll_name, MAX_DLL_NAME_LEN, "%s", archive->python_libname);
    }
#else
    dll_name_len = snprintf(dll_name, MAX_DLL_NAME_LEN, "%s", archive->python_libname);
#endif

    if (dll_name_len >= MAX_DLL_NAME_LEN) {
        PYI_ERROR(
            "Reported length (%d) of Python shared library name (%s) exceeds buffer size (%d)\n",
            dll_name_len,
            archive->python_libname,
            MAX_DLL_NAME_LEN
        );
        return -1;
    }

#ifdef _WIN32
    /* If ucrtbase.dll exists in top-level application directory, load
     * it proactively before Python library loading to avoid Python library
     * loading failure (unresolved symbol errors) on systems with Universal
     * CRT update not installed. */
    if (1) {
        char ucrtpath[PYI_PATH_MAX];
        if (pyi_path_join(ucrtpath, pyi_ctx->application_home_dir, "ucrtbase.dll") != NULL && pyi_path_exists(ucrtpath)) {
            wchar_t ucrtpath_w[PYI_PATH_MAX];
            if (pyi_win32_utf8_to_wcs(ucrtpath, ucrtpath_w, PYI_PATH_MAX) != NULL) {
                PYI_DEBUG_W(L"LOADER: ucrtbase.dll found: %ls\n", ucrtpath_w);
                pyi_utils_dlopen(ucrtpath_w);
            }
        }
    }
#endif

    /* Look for python shared library in top-level application directory */
    if (pyi_path_join(dll_fullpath, pyi_ctx->application_home_dir, dll_name) == NULL) {
        PYI_ERROR("Path of Python shared library (%s) and its name (%s) exceed buffer size (%d)\n", pyi_ctx->application_home_dir, PYI_PATH_MAX);
        return -1;
    }

    PYI_DEBUG("LOADER: loading Python shared library: %s\n", dll_fullpath);

    /* Load the shared libary */
    pyi_ctx->python_dll = pyi_dylib_python_load(dll_fullpath, archive->python_version);
    if (!pyi_ctx->python_dll) {
        return -1;
    }

    return 0;
}

/*
 * Initialize and start python interpreter.
 */
int
pyi_pylib_start_python(const struct PYI_CONTEXT *pyi_ctx)
{
    struct PyiRuntimeOptions *runtime_options = NULL;
    PyConfig *config = NULL;
    PyStatus status;
    int ret = -1;

    /* Read run-time options */
    runtime_options = pyi_runtime_options_read(pyi_ctx);
    if (runtime_options == NULL) {
        PYI_ERROR("Failed to parse run-time options!\n");
        goto end;
    }

    /* Pre-initialize python. This ensures that PEP 540 UTF-8 mode is enabled
     * if necessary. */
    PYI_DEBUG("LOADER: pre-initializing embedded python interpreter...\n");
    if (pyi_pyconfig_preinit_python(runtime_options, pyi_ctx) < 0) {
        PYI_ERROR("Failed to pre-initialize embedded python interpreter!\n");
        goto end;
    }

    /* Allocate the config structure. Since underlying layout is specific to
     * python version, this also verifies that python version is supported. */
    PYI_DEBUG("LOADER: creating PyConfig structure...\n");
    config = pyi_pyconfig_create(pyi_ctx);
    if (config == NULL) {
        PYI_ERROR("Failed to allocate PyConfig structure! Unsupported python version?\n");
        goto end;
    }

    /* Initialize isolated configuration */
    PYI_DEBUG("LOADER: initializing interpreter configuration...\n");
    pyi_ctx->python_dll->PyConfig_InitIsolatedConfig(config);

    /* Set program name */
    PYI_DEBUG("LOADER: setting program name...\n");
    if (pyi_pyconfig_set_program_name(config, pyi_ctx) < 0) {
        PYI_ERROR("Failed to set program name!\n");
        goto end;
    }

    /* Set python home */
    PYI_DEBUG("LOADER: setting python home path...\n");
    if (pyi_pyconfig_set_python_home(config, pyi_ctx) < 0) {
        PYI_ERROR("Failed to set python home path!\n");
        goto end;
    }

    /* Set module search paths */
    PYI_DEBUG("LOADER: setting module search paths...\n");
    if (pyi_pyconfig_set_module_search_paths(config, pyi_ctx) < 0) {
        PYI_ERROR("Failed to set module search paths!\n");
        goto end;
    }

    /* Set arguments (sys.argv) */
    PYI_DEBUG("LOADER: setting sys.argv...\n");
    if (pyi_pyconfig_set_argv(config, pyi_ctx) < 0) {
        PYI_ERROR("Failed to set sys.argv!\n");
        goto end;
    }

    /* Apply run-time options */
    PYI_DEBUG("LOADER: applying run-time options...\n");
    if (pyi_pyconfig_set_runtime_options(config, runtime_options, pyi_ctx) < 0) {
        PYI_ERROR("Failed to set run-time options!\n");
        goto end;
    }

    /* Start the interpreter */
    PYI_DEBUG("LOADER: starting embedded python interpreter...\n");

    /* In unbuffered mode, flush stdout/stderr before python configuration
     * removes the buffer (changing the buffer should probably flush the
     * old buffer, but just in case do it manually...) */
    if (runtime_options->unbuffered) {
        fflush(stdout);
        fflush(stderr);
    }

    /*
     * Py_Initialize() may rudely call abort(), and on Windows this triggers the error
     * reporting service, which results in a dialog box that says "Close program", "Check
     * for a solution", and also "Debug" if Visual Studio is installed. The dialog box
     * makes it frustrating to run the test suite.
     *
     * For debug builds of the bootloader, disable the error reporting before calling
     * Py_Initialize and enable it afterward.
     */

#if defined(_WIN32) && defined(LAUNCH_DEBUG)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif

    status = pyi_ctx->python_dll->Py_InitializeFromConfig(config);

#if defined(_WIN32) && defined(LAUNCH_DEBUG)
    SetErrorMode(0);
#endif

    if (pyi_ctx->python_dll->PyStatus_Exception(status)) {
        PYI_ERROR("Failed to start embedded python interpreter!\n");
        /* Dump exception information to stderr and exit the process with error code. */
        pyi_ctx->python_dll->Py_ExitStatusException(status);
    } else {
        ret = 0; /* Succeeded */
    }

end:
    pyi_pyconfig_free(config, pyi_ctx);
    pyi_runtime_options_free(runtime_options);
    return ret;
}

/*
 * Import (bootstrap) modules embedded in the PKG archive.
 */
int
pyi_pylib_import_modules(const struct PYI_CONTEXT *pyi_ctx)
{
    const struct PYTHON_DLL *python_dll = pyi_ctx->python_dll;
    const struct ARCHIVE *archive = pyi_ctx->archive;
    const struct TOC_ENTRY *toc_entry;
    unsigned char *data;
    PyObject *co;
    PyObject *mod;
    PyObject *meipass_obj;

    PYI_DEBUG("LOADER: setting sys._MEIPASS\n");

    /* TODO extract function pyi_char_to_pyobject */
#ifdef _WIN32
    meipass_obj = python_dll->PyUnicode_Decode(pyi_ctx->application_home_dir, strlen(pyi_ctx->application_home_dir), "utf-8", "strict");
#else
    meipass_obj = python_dll->PyUnicode_DecodeFSDefault(pyi_ctx->application_home_dir);
#endif

    if (!meipass_obj) {
        PYI_ERROR("Failed to get _MEIPASS as PyObject.\n");
        return -1;
    }

    python_dll->PySys_SetObject("_MEIPASS", meipass_obj);

    PYI_DEBUG("LOADER: importing modules from PKG/CArchive\n");

    /* Iterate through toc looking for module entries (type 'm')
     * this is normally just bootstrap stuff (archive and iu) */
    for (toc_entry = archive->toc; toc_entry < archive->toc_end; toc_entry = pyi_archive_next_toc_entry(archive, toc_entry)) {
        if (toc_entry->typecode != ARCHIVE_ITEM_PYMODULE && toc_entry->typecode != ARCHIVE_ITEM_PYPACKAGE) {
            continue;
        }

        data = pyi_archive_extract(archive, toc_entry);
        PYI_DEBUG("LOADER: extracted %s\n", toc_entry->name);

        /* Unmarshal the stored code object */
        co = python_dll->PyMarshal_ReadObjectFromString((const char *)data, toc_entry->uncompressed_length);
        free(data);

        if (co == NULL) {
            PYI_ERROR("Failed to unmarshal code object for module %s!\n", toc_entry->name);
            mod = NULL;
        } else {
            PYI_DEBUG("LOADER: running unmarshalled code object for module %s...\n", toc_entry->name);
            mod = python_dll->PyImport_ExecCodeModule(toc_entry->name, co);
            if (mod == NULL) {
                PYI_ERROR("Module object for %s is NULL!\n", toc_entry->name);
            }
        }

        if (python_dll->PyErr_Occurred()) {
            python_dll->PyErr_Print();
            python_dll->PyErr_Clear();
        }

        /* Exit on error */
        if (mod == NULL) {
            return -1;
        }
    }

    return 0;
}

/*
 * Install a PYZ from a TOC entry, by adding it to sys.path.
 *
 * Must be called after Py_Initialize (i.e. after pyi_pylib_start_python)
 *
 * The installation is done by adding an entry like
 *    absolute_path/dist/hello_world/hello_world?123456
 * to sys.path. The end number is the offset where the
 * Python-side bootstrap code should read the PYZ data.
 * Return non zero on failure.
 * NB: This entry is removed from sys.path by the Python-side bootstrap scripts.
 */
int
_pyi_pylib_install_pyz_entry(const struct PYI_CONTEXT *pyi_ctx, const struct TOC_ENTRY *toc_entry)
{
    const struct PYTHON_DLL *python_dll = pyi_ctx->python_dll;
    unsigned long long zlib_offset;
    PyObject *sys_path;
    PyObject *zlib_entry;
    PyObject *archivename_obj;
    int rc = 0;

    /* Retrieve sys.path object; this returns borrowed reference! */
    sys_path = python_dll->PySys_GetObject("path");
    if (sys_path == NULL) {
        PYI_ERROR("Installing PYZ: could not get sys.path object!\n");
        return -1;
    }

#ifdef _WIN32
    /* Decode UTF-8 to PyUnicode */
    archivename_obj = python_dll->PyUnicode_Decode(pyi_ctx->archive_filename, strlen(pyi_ctx->archive_filename), "utf-8", "strict");
#else
    /* Decode locale-encoded filename to PyUnicode object using Python's
     * preferred decoding method for filenames. */
    archivename_obj = python_dll->PyUnicode_DecodeFSDefault(pyi_ctx->archive_filename);
#endif

    zlib_offset = pyi_ctx->archive->pkg_offset + toc_entry->offset;
    zlib_entry = python_dll->PyUnicode_FromFormat("%U?%llu", archivename_obj, zlib_offset);
    python_dll->Py_DecRef(archivename_obj);

    rc = python_dll->PyList_Append(sys_path, zlib_entry);

    python_dll->Py_DecRef(zlib_entry);

    if (rc != 0) {
        PYI_ERROR("Failed to append PYZ entry to sys.path!\n");
    }

    return rc;
}

/*
 * Install PYZ archive(s) to sys.path.
 * Return non zero on failure.
 */
int
pyi_pylib_install_pyz(const struct PYI_CONTEXT *pyi_ctx)
{
    const struct ARCHIVE *archive = pyi_ctx->archive;
    const struct TOC_ENTRY *toc_entry;

    PYI_DEBUG("LOADER: installing PYZ archive with Python modules.\n");

    /* Iterate through TOC looking for PYZ (type 'z') */
    for (toc_entry = archive->toc; toc_entry < archive->toc_end; toc_entry = pyi_archive_next_toc_entry(archive, toc_entry)) {
        if (toc_entry->typecode != ARCHIVE_ITEM_PYZ) {
            continue;
        }

        PYI_DEBUG("LOADER: PYZ archive: %s\n", toc_entry->name);
        if (_pyi_pylib_install_pyz_entry(pyi_ctx, toc_entry) < 0) {
            return -1;
        }
    }

    return 0;
}

void
pyi_pylib_finalize(const struct PYI_CONTEXT *pyi_ctx)
{
    const struct PYTHON_DLL *python_dll = pyi_ctx->python_dll;

    /* Ensure python library was loaded and its function pointers are
     * valid; otherwise, we have nothing to do here. */
    if (!python_dll) {
        return;
    }

    /* Nothing to do if python interpreter was not initialized. Attempting
     * to flush streams using PyRun_SimpleStringFlags requires a valid
     * interpreter instance. */
    if (python_dll->Py_IsInitialized() == 0) {
        return;
    }

#ifndef WINDOWED
    /* We need to manually flush the buffers because otherwise there can be errors.
     * The native python interpreter flushes buffers before calling Py_Finalize,
     * so we need to manually do the same. See isse #4908. */
    PYI_DEBUG("LOADER: manually flushing stdout and stderr...\n");

    /* sys.stdout.flush() */
    python_dll->PyRun_SimpleStringFlags(
        "import sys; sys.stdout.flush(); \
        (sys.__stdout__.flush if sys.__stdout__ \
        is not sys.stdout else (lambda: None))()", NULL);

    /* sys.stderr.flush() */
    python_dll->PyRun_SimpleStringFlags(
        "import sys; sys.stderr.flush(); \
        (sys.__stderr__.flush if sys.__stderr__ \
        is not sys.stderr else (lambda: None))()", NULL);

#endif

    /* Finalize the interpreter. This calls all of the atexit functions. */
    PYI_DEBUG("LOADER: cleaning up Python interpreter...\n");
    python_dll->Py_Finalize();
}
