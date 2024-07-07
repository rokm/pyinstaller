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

#include "pyi_global.h"
#include "pyi_python.h"
#include "pyi_utils.h"

/* Load python shared library and bind all required functions. */
struct PYTHON_DLL *
pyi_dylib_python_load(const char *filename, int python_version)
{
    struct PYTHON_DLL *dll;
#ifdef _WIN32
    wchar_t filename_w[PYI_PATH_MAX];
#endif

    /* Allocate */
    dll = (struct PYTHON_DLL *)calloc(1, sizeof(struct PYTHON_DLL));
    if (dll == NULL) {
        PYI_PERROR("calloc", "Could not allocate memory for PYTHON_DLL.\n");
        return NULL;
    }

    /* Store version info */
    dll->version = python_version;

    /* Load shared library */
#ifdef _WIN32
    /* Convert filename from UTF-8 to wide-char */
    if (!pyi_win32_utf8_to_wcs(filename, wchar_t, PYI_PATH_MAX)) {
        goto cleanup;
    }

    /* Load */
    dll->handle = pyi_utils_dlopen(filename_w); /* Wrapper for LoadLibrary() */
    if (!dll->handle) {
        PYI_WINERROR_W(L"LoadLibrary", L"Failed to load Python shared library '%ls'.\n", filename_w);
        goto cleanup;
    }

    /* Extend PYI_EXT_FUNC_BIND() macro with error handling.
     *
     * Function names always contain ASCII characters, so we can safely
     * format ANSI string (obtained via stringification) into wide-char
     * message string. */
    #define _IMPORT_FUNCTION(name) \
        PYI_EXT_FUNC_BIND(dll, name); \
        if (!dll->name) { \
            PYI_WINERROR_W(L"GetProcAddress", L"Failed to import symbol %hs from Python shared library.\n", #name); \
            goto cleanup; \
        }
#else
    /* Load shared library */
    dll->handle = pyi_utils_dlopen(filename); /* Wrapper for dlopen() */
    if (dll->handle == NULL) {
        PYI_ERROR("Failed to load Python shared library '%s'. dlopen: %s\n", filename, dlerror());
        goto cleanup;
    }

    /* Extend PYI_EXT_FUNC_BIND() with error handling. */
    #define _IMPORT_FUNCTION(name) \
        PYI_EXT_FUNC_BIND(dll, name); \
        if (!dll->name) { \
            PYI_ERROR("dlsym", "Failed to import symbol %s from Python shared library. dlsym: %s\n", #name, dlerror()); \
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

    if (pyi_utils_dlclose(dll->handle) < 0) {
        PYI_DEBUG("LOADER: failed to unload Python shared library!\n");
    } else {
        PYI_DEBUG("LOADER: unloaded Python shared library.\n");
    }

    free(dll);
}
