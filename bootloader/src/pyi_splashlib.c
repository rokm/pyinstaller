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
 * tcl.h and tk.h replacement
 */

#ifdef _WIN32
    #include <windows.h>  /* HMODULE */
#else
    #include <dlfcn.h>  /* dlsym */
#endif
#include <stdlib.h>

/* PyInstaller headers */
#include "pyi_global.h"
#include "pyi_splashlib.h"
#include "pyi_utils.h"


/* Load Tcl shared library and bind all required functions. */
struct TCL_DLL *
pyi_dylib_tcl_load(const char *filename)
{
    struct TCL_DLL *dll;

    /* Allocate */
    dll = (struct TCL_DLL *)calloc(1, sizeof(struct TCL_DLL));
    if (dll == NULL) {
        PYI_PERROR("calloc", "Could not allocate memory for TCL_DLL.\n");
        return NULL;
    }

    /* Load shared library */
    dll->handle = pyi_utils_dlopen(filename);
    if (dll->handle == NULL) {
#ifdef _WIN32
        wchar_t filename_w[PYI_PATH_MAX];
        pyi_win32_utf8_to_wcs(filename, filename_w, PYI_PATH_MAX);
        PYI_WINERROR_W(L"LoadLibrary", L"Failed to load Tcl shared library '%ls'.\n", filename_w);
#else
        PYI_ERROR("Failed to load Tcl shared library '%s': dlopen: %s\n", filename, dlerror());
#endif
        goto cleanup;
    }

#if defined(_WIN32)
    /* Note: since function names are always in ASCII, we can safely use %hs
     * to format ANSI string (obtained via stringification) into wide-char
     * message string. This alleviates the need for set of macros that would
     * achieve wide-char stringification of the function name. */
    #define _IMPORT_FUNCTION(name) \
        *(FARPROC *)(&(dll->name)) = GetProcAddress(dll->handle, #name); \
        if (!dll->name) { \
            PYI_WINERROR_W(L"GetProcAddress", L"Failed to import symbol %hs from Tcl shared library\n", #name); \
            goto cleanup; \
        }
#else
    #define _IMPORT_FUNCTION(name) \
        *(void **)(&(dll->name)) = dlsym(dll->handle, #name); \
        if (!dll->name) { \
            PYI_PERROR("dlsym", "Failed to import symbol %s from Tcl shared library: dlsym: %s\n", #name, dlerror()); \
            goto cleanup; \
        }
#endif

    /* Load functions */
    _IMPORT_FUNCTION(Tcl_Init)
    _IMPORT_FUNCTION(Tcl_CreateInterp)
    _IMPORT_FUNCTION(Tcl_FindExecutable)
    _IMPORT_FUNCTION(Tcl_DoOneEvent)
    _IMPORT_FUNCTION(Tcl_Finalize)
    _IMPORT_FUNCTION(Tcl_FinalizeThread)
    _IMPORT_FUNCTION(Tcl_DeleteInterp)

    _IMPORT_FUNCTION(Tcl_CreateThread)
    _IMPORT_FUNCTION(Tcl_GetCurrentThread)
    _IMPORT_FUNCTION(Tcl_JoinThread)
    _IMPORT_FUNCTION(Tcl_MutexLock)
    _IMPORT_FUNCTION(Tcl_MutexUnlock)
    _IMPORT_FUNCTION(Tcl_MutexFinalize)
    _IMPORT_FUNCTION(Tcl_ConditionFinalize)
    _IMPORT_FUNCTION(Tcl_ConditionNotify)
    _IMPORT_FUNCTION(Tcl_ConditionWait)
    _IMPORT_FUNCTION(Tcl_ThreadQueueEvent)
    _IMPORT_FUNCTION(Tcl_ThreadAlert)

    _IMPORT_FUNCTION(Tcl_GetVar2)
    _IMPORT_FUNCTION(Tcl_SetVar2)
    _IMPORT_FUNCTION(Tcl_CreateObjCommand)
    _IMPORT_FUNCTION(Tcl_GetString)
    _IMPORT_FUNCTION(Tcl_NewStringObj)
    _IMPORT_FUNCTION(Tcl_NewByteArrayObj)
    _IMPORT_FUNCTION(Tcl_SetVar2Ex)
    _IMPORT_FUNCTION(Tcl_GetObjResult)

    _IMPORT_FUNCTION(Tcl_EvalFile)
    _IMPORT_FUNCTION(Tcl_EvalEx)
    _IMPORT_FUNCTION(Tcl_EvalObjv)
    _IMPORT_FUNCTION(Tcl_Alloc)
    _IMPORT_FUNCTION(Tcl_Free)

#undef _IMPORT_FUNCTION

    PYI_DEBUG("LOADER: loaded functions from Tcl shared library.\n");

    return dll;

cleanup:
    if (dll->handle) {
        pyi_utils_dlclose(dll->handle);
    }
    free(dll);
    return NULL;
}

void pyi_dylib_tcl_cleanup(struct TCL_DLL **dll_ref)
{
    struct TCL_DLL *dll = *dll_ref;

    *dll_ref = NULL;

    if (dll == NULL) {
        return;
    }

    if (pyi_utils_dlclose(dll->handle) < 0) {
        PYI_DEBUG("LOADER: failed to unload Tcl shared library!\n");
    } else {
        PYI_DEBUG("LOADER: unloaded Tcl shared library.\n");
    }

    free(dll);
}

/* Load Tk shared library and bind all required functions. */
struct TK_DLL *
pyi_dylib_tk_load(const char *filename)
{
    struct TK_DLL *dll;

    /* Allocate */
    dll = (struct TK_DLL *)calloc(1, sizeof(struct TK_DLL));
    if (dll == NULL) {
        PYI_PERROR("calloc", "Could not allocate memory for TK_DLL.\n");
        return NULL;
    }

    /* Load shared library */
    dll->handle = pyi_utils_dlopen(filename);
    if (dll->handle == NULL) {
#ifdef _WIN32
        wchar_t filename_w[PYI_PATH_MAX];
        pyi_win32_utf8_to_wcs(filename, filename_w, PYI_PATH_MAX);
        PYI_WINERROR_W(L"LoadLibrary", L"Failed to load Tk shared library '%ls'.\n", filename_w);
#else
        PYI_ERROR("Failed to load Tk shared library '%s': dlopen: %s\n", filename, dlerror());
#endif
        goto cleanup;
    }

#if defined(_WIN32)
    /* Note: since function names are always in ASCII, we can safely use %hs
     * to format ANSI string (obtained via stringification) into wide-char
     * message string. This alleviates the need for set of macros that would
     * achieve wide-char stringification of the function name. */
    #define _IMPORT_FUNCTION(name) \
        *(FARPROC *)(&(dll->name)) = GetProcAddress(dll->handle, #name); \
        if (!dll->name) { \
            PYI_WINERROR_W(L"GetProcAddress", L"Failed to import symbol %hs from Tk shared library\n", #name); \
            goto cleanup; \
        }
#else
    #define _IMPORT_FUNCTION(name) \
        *(void **)(&(dll->name)) = dlsym(dll->handle, #name); \
        if (!dll->name) { \
            PYI_PERROR("dlsym", "Failed to import symbol %s from Tk shared library: dlsym: %s\n", #name, dlerror()); \
            goto cleanup; \
        }
#endif

    /* Load functions */
    _IMPORT_FUNCTION(Tk_Init)
    _IMPORT_FUNCTION(Tk_GetNumMainWindows)

#undef _IMPORT_FUNCTION

    PYI_DEBUG("LOADER: loaded functions from Tk shared library.\n");

    return dll;

cleanup:
    if (dll->handle) {
        pyi_utils_dlclose(dll->handle);
    }
    free(dll);
    return NULL;
}

void pyi_dylib_tk_cleanup(struct TK_DLL **dll_ref)
{
    struct TK_DLL *dll = *dll_ref;

    *dll_ref = NULL;

    if (dll == NULL) {
        return;
    }

    if (pyi_utils_dlclose(dll->handle) < 0) {
        PYI_DEBUG("LOADER: failed to unload Tk shared library!\n");
    } else {
        PYI_DEBUG("LOADER: unloaded Tk shared library.\n");
    }

    free(dll);
}
