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

#ifdef _WIN32
    #include <windows.h> /* HMODULE */
#else
    #include <dlfcn.h> /* dlsym */
#endif
#include <stddef.h> /* ptrdiff_t */
#include <stdlib.h>

#include "pyi_global.h"
#include "pyi_python.h"
#include "pyi_utils.h"

/* Load python shared library and bind all required functions. */
struct PYTHON_DLL *
pyi_dylib_python_load(const char *filename, int python_version)
{
    struct PYTHON_DLL *dll;

    /* Allocate */
    dll = (struct PYTHON_DLL *)calloc(1, sizeof(struct PYTHON_DLL));
    if (dll == NULL) {
        PYI_PERROR("calloc", "Could not allocate memory for PYTHON_DLL.\n");
        return NULL;
    }

    /* Load shared library */
    dll->handle = pyi_utils_dlopen(filename);
    if (dll->handle == NULL) {
#ifdef _WIN32
        wchar_t filename_w[PYI_PATH_MAX];
        pyi_win32_utf8_to_wcs(filename, filename_w, PYI_PATH_MAX);
        PYI_WINERROR_W(L"LoadLibrary", L"Failed to load Python shared library '%ls'.\n", filename_w);
#else
        PYI_ERROR("Failed to load Python shared library '%s': dlopen: %s\n", filename, dlerror());
#endif
        goto cleanup;
    }

    /* Store version info */
    dll->version = python_version;

#if defined(_WIN32)
    /* Note: since function names are always in ASCII, we can safely use %hs
     * to format ANSI string (obtained via stringification) into wide-char
     * message string. This alleviates the need for set of macros that would
     * achieve wide-char stringification of the function name. */
    #define _IMPORT_FUNCTION(name) \
        *(FARPROC *)(&(dll->name)) = GetProcAddress(dll->handle, #name); \
        if (!dll->name) { \
            PYI_WINERROR_W(L"GetProcAddress", L"Failed to import function %hs from Python shared library\n", #name); \
            goto cleanup; \
        }
#else
    #define _IMPORT_FUNCTION(name) \
        *(void **)(&(dll->name)) = dlsym(dll->handle, #name); \
        if (!dll->name) { \
            PYI_PERROR("dlsym", "Failed to import function " #name " from Python shared library\n"); \
            goto cleanup; \
        }
#endif

    /* Load functions */
    _IMPORT_FUNCTION(Py_DecRef)
    _IMPORT_FUNCTION(Py_DecodeLocale)
    _IMPORT_FUNCTION(Py_ExitStatusException)
    _IMPORT_FUNCTION(Py_Finalize)
    _IMPORT_FUNCTION(Py_InitializeFromConfig)
    _IMPORT_FUNCTION(Py_IsInitialized)
    _IMPORT_FUNCTION(Py_PreInitialize)

    _IMPORT_FUNCTION(PyConfig_Clear)
    _IMPORT_FUNCTION(PyConfig_InitIsolatedConfig)
    _IMPORT_FUNCTION(PyConfig_Read)
    _IMPORT_FUNCTION(PyConfig_SetBytesString)
    _IMPORT_FUNCTION(PyConfig_SetString)
    _IMPORT_FUNCTION(PyConfig_SetWideStringList)

    _IMPORT_FUNCTION(PyErr_Clear)
    _IMPORT_FUNCTION(PyErr_Fetch)
    _IMPORT_FUNCTION(PyErr_NormalizeException)
    _IMPORT_FUNCTION(PyErr_Occurred)
    _IMPORT_FUNCTION(PyErr_Print)
    _IMPORT_FUNCTION(PyErr_Restore)

    _IMPORT_FUNCTION(PyEval_EvalCode)

    _IMPORT_FUNCTION(PyImport_AddModule)
    _IMPORT_FUNCTION(PyImport_ExecCodeModule)
    _IMPORT_FUNCTION(PyImport_ImportModule)

    _IMPORT_FUNCTION(PyList_Append)

    _IMPORT_FUNCTION(PyMarshal_ReadObjectFromString)

    _IMPORT_FUNCTION(PyMem_RawFree)

    _IMPORT_FUNCTION(PyModule_GetDict)

    _IMPORT_FUNCTION(PyObject_CallFunction)
    _IMPORT_FUNCTION(PyObject_CallFunctionObjArgs)
    _IMPORT_FUNCTION(PyObject_GetAttrString)
    _IMPORT_FUNCTION(PyObject_SetAttrString)
    _IMPORT_FUNCTION(PyObject_Str)

    _IMPORT_FUNCTION(PyPreConfig_InitIsolatedConfig)

    _IMPORT_FUNCTION(PyRun_SimpleStringFlags)

    _IMPORT_FUNCTION(PyStatus_Exception)

    _IMPORT_FUNCTION(PySys_GetObject)
    _IMPORT_FUNCTION(PySys_SetObject)

    _IMPORT_FUNCTION(PyUnicode_AsUTF8)
    _IMPORT_FUNCTION(PyUnicode_Decode)
    _IMPORT_FUNCTION(PyUnicode_DecodeFSDefault)
    _IMPORT_FUNCTION(PyUnicode_FromFormat)
    _IMPORT_FUNCTION(PyUnicode_FromString)
    _IMPORT_FUNCTION(PyUnicode_Join)
    _IMPORT_FUNCTION(PyUnicode_Replace)

#undef _IMPORT_FUNCTION

    PYI_DEBUG("LOADER: loaded functions from Python shared library.\n");

    return dll;

cleanup:
    if (dll->handle) {
        pyi_utils_dlclose(dll->handle);
    }
    free(dll);
    return NULL;
}

void pyi_dylib_python_cleanup(struct PYTHON_DLL **dll_ref)
{
    struct PYTHON_DLL *dll = *dll_ref;

    *dll_ref = NULL;

    if (dll == NULL) {
        return;
    }

    pyi_utils_dlclose(dll->handle);

    free(dll);
}
