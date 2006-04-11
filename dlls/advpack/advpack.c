/*
 * Advpack main
 *
 * Copyright 2004 Huw D M Davies
 * Copyright 2005 Sami Aario
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "winver.h"
#include "winternl.h"
#include "winnls.h"
#include "setupapi.h"
#include "advpub.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(advpack);

typedef HRESULT (WINAPI *DLLREGISTER) (void);

#define MAX_FIELD_LENGTH    512
#define PREFIX_LEN          5

/* parses the destination directory parameters from pszSection
 * the parameters are of the form: root,key,value,unknown,fallback
 * we first read the reg value root\\key\\value and if that fails,
 * use fallback as the destination directory
 */
static void get_dest_dir(HINF hInf, PCWSTR pszSection, PWSTR pszBuffer, DWORD dwSize)
{
    INFCONTEXT context;
    WCHAR key[MAX_PATH], value[MAX_PATH];
    WCHAR prefix[PREFIX_LEN];
    HKEY root, subkey;
    DWORD size;

    static const WCHAR hklm[] = {'H','K','L','M',0};
    static const WCHAR hkcu[] = {'H','K','C','U',0};

    /* load the destination parameters */
    SetupFindFirstLineW(hInf, pszSection, NULL, &context);
    SetupGetStringFieldW(&context, 1, prefix, PREFIX_LEN, &size);
    SetupGetStringFieldW(&context, 2, key, MAX_PATH, &size);
    SetupGetStringFieldW(&context, 3, value, MAX_PATH, &size);

    if (!lstrcmpW(prefix, hklm))
        root = HKEY_LOCAL_MACHINE;
    else if (!lstrcmpW(prefix, hkcu))
        root = HKEY_CURRENT_USER;
    else
        root = NULL;

    size = dwSize * sizeof(WCHAR);

    /* fallback to the default destination dir if reg fails */
    if (RegOpenKeyW(root, key, &subkey) ||
        RegQueryValueExW(subkey, value, NULL, NULL, (LPBYTE)pszBuffer, &size))
    {
        SetupGetStringFieldW(&context, 6, pszBuffer, dwSize, NULL);
    }

    RegCloseKey(subkey);
}

/* loads the LDIDs specified in the install section of an INF */
static void set_ldids(HINF hInf, LPCWSTR pszInstallSection)
{
    WCHAR field[MAX_FIELD_LENGTH];
    WCHAR key[MAX_FIELD_LENGTH];
    WCHAR dest[MAX_PATH];
    INFCONTEXT context;
    DWORD size;
    int ldid;

    static const WCHAR custDestW[] = {
        'C','u','s','t','o','m','D','e','s','t','i','n','a','t','i','o','n',0
    };

    if (!SetupGetLineTextW(NULL, hInf, pszInstallSection, custDestW,
                           field, MAX_FIELD_LENGTH, &size))
        return;

    if (!SetupFindFirstLineW(hInf, field, NULL, &context))
        return;

    do
    {
        SetupGetIntField(&context, 0, &ldid);
        SetupGetLineTextW(&context, NULL, NULL, NULL,
                          key, MAX_FIELD_LENGTH, &size);

        get_dest_dir(hInf, key, dest, MAX_PATH);

        SetupSetDirectoryIdW(hInf, ldid, dest);
    } while (SetupFindNextLine(&context, &context));
}

/***********************************************************************
 *           CloseINFEngine (ADVPACK.@)
 *
 * Closes a handle to an INF file opened with OpenINFEngine.
 *
 * PARAMS
 *   hInf [I] Handle to the INF file to close.
 *
 * RETURNS
 *   Success: S_OK.
 *   Failure: E_FAIL.
 */
HRESULT WINAPI CloseINFEngine(HINF hInf)
{
    TRACE("(%p)\n", hInf);

    if (!hInf)
        return E_INVALIDARG;

    SetupCloseInfFile(hInf);
    return S_OK;
}

/***********************************************************************
 *           DllMain (ADVPACK.@)
 */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("(%p, %ld, %p)\n", hinstDLL, fdwReason, lpvReserved);

    if (fdwReason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hinstDLL);

    return TRUE;
}

/***********************************************************************
 *              IsNTAdmin	(ADVPACK.@)
 *
 * Checks if the user has admin privileges.
 *
 * PARAMS
 *   reserved  [I] Reserved.  Must be 0.
 *   pReserved [I] Reserved.  Must be NULL.
 *
 * RETURNS
 *   TRUE if user has admin rights, FALSE otherwise.
 */
BOOL WINAPI IsNTAdmin(DWORD reserved, LPDWORD pReserved)
{
    SID_IDENTIFIER_AUTHORITY SidAuthority = {SECURITY_NT_AUTHORITY};
    PTOKEN_GROUPS pTokenGroups;
    BOOL bSidFound = FALSE;
    DWORD dwSize, i;
    HANDLE hToken;
    PSID pSid;

    TRACE("(%ld, %p)\n", reserved, pReserved);

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;

    if (!GetTokenInformation(hToken, TokenGroups, NULL, 0, &dwSize))
    {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            CloseHandle(hToken);
            return FALSE;
        }
    }

    pTokenGroups = HeapAlloc(GetProcessHeap(), 0, dwSize);
    if (!pTokenGroups)
    {
        CloseHandle(hToken);
        return FALSE;
    }

    if (!GetTokenInformation(hToken, TokenGroups, pTokenGroups, dwSize, &dwSize))
    {
        HeapFree(GetProcessHeap(), 0, pTokenGroups);
        CloseHandle(hToken);
        return FALSE;
    }

    CloseHandle(hToken);

    if (!AllocateAndInitializeSid(&SidAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pSid))
    {
        HeapFree(GetProcessHeap(), 0, pTokenGroups);
        return FALSE;
    }

    for (i = 0; i < pTokenGroups->GroupCount; i++)
    {
        if (EqualSid(pSid, pTokenGroups->Groups[i].Sid))
        {
            bSidFound = TRUE;
            break;
        }
    }

    HeapFree(GetProcessHeap(), 0, pTokenGroups);
    FreeSid(pSid);

    return bSidFound;
}

/***********************************************************************
 *             NeedRebootInit  (ADVPACK.@)
 *
 * Sets up conditions for reboot checking.
 *
 * RETURNS
 *   Value required by NeedReboot.
 */
DWORD WINAPI NeedRebootInit(VOID)
{
    FIXME("(VOID): stub\n");
    return 0;
}

/***********************************************************************
 *             NeedReboot      (ADVPACK.@)
 *
 * Determines whether a reboot is required.
 *
 * PARAMS
 *   dwRebootCheck [I] Value from NeedRebootInit.
 *
 * RETURNS
 *   TRUE if a reboot is needed, FALSE otherwise.
 *
 * BUGS
 *   Unimplemented.
 */
BOOL WINAPI NeedReboot(DWORD dwRebootCheck)
{
    FIXME("(%ld): stub\n", dwRebootCheck);
    return FALSE;
}

/***********************************************************************
 *             OpenINFEngineA   (ADVPACK.@)
 *
 * See OpenINFEngineW.
 */
HRESULT WINAPI OpenINFEngineA(LPCSTR pszInfFilename, LPCSTR pszInstallSection,
                              DWORD dwFlags, HINF *phInf, PVOID pvReserved)
{
    UNICODE_STRING filenameW, installW;
    HRESULT res;

    TRACE("(%s, %s, %ld, %p, %p)\n", debugstr_a(pszInfFilename),
          debugstr_a(pszInstallSection), dwFlags, phInf, pvReserved);

    if (!pszInfFilename || !phInf)
        return E_INVALIDARG;

    RtlCreateUnicodeStringFromAsciiz(&filenameW, pszInfFilename);
    RtlCreateUnicodeStringFromAsciiz(&installW, pszInstallSection);

    res = OpenINFEngineW(filenameW.Buffer, installW.Buffer,
                         dwFlags, phInf, pvReserved);

    RtlFreeUnicodeString(&filenameW);
    RtlFreeUnicodeString(&installW);

    return res;
}

/***********************************************************************
 *             OpenINFEngineW   (ADVPACK.@)
 *
 * Opens and returns a handle to an INF file to be used by
 * TranslateInfStringEx to continuously translate the INF file.
 *
 * PARAMS
 *   pszInfFilename    [I] Filename of the INF to open.
 *   pszInstallSection [I] Name of the Install section in the INF.
 *   dwFlags           [I] See advpub.h.
 *   phInf             [O] Handle to the loaded INF file.
 *   pvReserved        [I] Reserved.  Must be NULL.
 *
 * RETURNS
 *   Success: S_OK.
 *   Failure: E_FAIL.
 */
HRESULT WINAPI OpenINFEngineW(LPCWSTR pszInfFilename, LPCWSTR pszInstallSection,
                              DWORD dwFlags, HINF *phInf, PVOID pvReserved)
{
    TRACE("(%s, %s, %ld, %p, %p)\n", debugstr_w(pszInfFilename),
          debugstr_w(pszInstallSection), dwFlags, phInf, pvReserved);

    if (!pszInfFilename || !phInf)
        return E_INVALIDARG;

    *phInf = SetupOpenInfFileW(pszInfFilename, NULL, INF_STYLE_WIN4, NULL);
    if (*phInf == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    set_ldids(*phInf, pszInstallSection);
    
    return S_OK;
}

/***********************************************************************
 *             RebootCheckOnInstallA   (ADVPACK.@)
 *
 * See RebootCheckOnInstallW.
 */
HRESULT WINAPI RebootCheckOnInstallA(HWND hWnd, LPCSTR pszINF,
                                     LPSTR pszSec, DWORD dwReserved)
{
    UNICODE_STRING infW, secW;
    HRESULT res;

    TRACE("(%p, %s, %s, %ld)\n", hWnd, debugstr_a(pszINF),
          debugstr_a(pszSec), dwReserved);

    if (!pszINF || !pszSec)
        return E_INVALIDARG;

    RtlCreateUnicodeStringFromAsciiz(&infW, pszINF);
    RtlCreateUnicodeStringFromAsciiz(&secW, pszSec);

    res = RebootCheckOnInstallW(hWnd, infW.Buffer, secW.Buffer, dwReserved);

    RtlFreeUnicodeString(&infW);
    RtlFreeUnicodeString(&secW);

    return res;
}

/***********************************************************************
 *             RebootCheckOnInstallW   (ADVPACK.@)
 *
 * Checks if a reboot is required for an installed INF section.
 *
 * PARAMS
 *   hWnd       [I] Handle to the window used for messages.
 *   pszINF     [I] Filename of the INF file.
 *   pszSec     [I] INF section to check.
 *   dwReserved [I] Reserved.  Must be 0.
 *
 * RETURNS
 *   Success: S_OK - Reboot is needed if the INF section is installed.
 *            S_FALSE - Reboot is not needed.
 *   Failure: HRESULT of GetLastError().
 *
 * NOTES
 *   if pszSec is NULL, RebootCheckOnInstall checks the DefaultInstall
 *   or DefaultInstall.NT section.
 *
 * BUGS
 *   Unimplemented.
 */
HRESULT WINAPI RebootCheckOnInstallW(HWND hWnd, LPCWSTR pszINF,
                                     LPWSTR pszSec, DWORD dwReserved)
{
    FIXME("(%p, %s, %s, %ld): stub\n", hWnd, debugstr_w(pszINF),
          debugstr_w(pszSec), dwReserved);

    return E_FAIL;
}

/***********************************************************************
 *             RegisterOCX    (ADVPACK.@)
 */
void WINAPI RegisterOCX(HWND hWnd, HINSTANCE hInst, LPCSTR cmdline, INT show)
{
    WCHAR wszBuff[MAX_PATH];
    WCHAR* pwcComma;
    HMODULE hm;
    DLLREGISTER pfnRegister;
    HRESULT hr;

    TRACE("(%s)\n", debugstr_a(cmdline));

    MultiByteToWideChar(CP_ACP, 0, cmdline, strlen(cmdline), wszBuff, MAX_PATH);
    if ((pwcComma = strchrW( wszBuff, ',' ))) *pwcComma = 0;

    TRACE("Parsed DLL name (%s)\n", debugstr_w(wszBuff));

    hm = LoadLibraryExW(wszBuff, 0, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hm)
    {
        ERR("Couldn't load DLL: %s\n", debugstr_w(wszBuff));
        return;
    }

    pfnRegister = (DLLREGISTER)GetProcAddress(hm, "DllRegisterServer");
    if (pfnRegister == NULL)
    {
        ERR("DllRegisterServer entry point not found\n");
    }
    else
    {
        hr = pfnRegister();
        if (hr != S_OK)
        {
            ERR("DllRegisterServer entry point returned %08lx\n", hr);
        }
    }

    TRACE("Successfully registered OCX\n");

    FreeLibrary(hm);
}

/***********************************************************************
 *             SetPerUserSecValuesA   (ADVPACK.@)
 *
 * See SetPerUserSecValuesW.
 */
HRESULT WINAPI SetPerUserSecValuesA(PERUSERSECTIONA* pPerUser)
{
    PERUSERSECTIONW perUserW;

    TRACE("(%p)\n", pPerUser);

    if (!pPerUser)
        return E_INVALIDARG;

    MultiByteToWideChar(CP_ACP, 0, pPerUser->szGUID, -1, perUserW.szGUID,
                        sizeof(perUserW.szGUID) / sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, pPerUser->szDispName, -1, perUserW.szDispName,
                        sizeof(perUserW.szDispName) / sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, pPerUser->szLocale, -1, perUserW.szLocale,
                        sizeof(perUserW.szLocale) / sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, pPerUser->szStub, -1, perUserW.szStub,
                        sizeof(perUserW.szStub) / sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, pPerUser->szVersion, -1, perUserW.szVersion,
                        sizeof(perUserW.szVersion) / sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, pPerUser->szCompID, -1, perUserW.szCompID,
                        sizeof(perUserW.szCompID) / sizeof(WCHAR));
    perUserW.dwIsInstalled = pPerUser->dwIsInstalled;
    perUserW.bRollback = pPerUser->bRollback;

    return SetPerUserSecValuesW(&perUserW);
}

/***********************************************************************
 *             SetPerUserSecValuesW   (ADVPACK.@)
 *
 * Prepares the per-user stub values under IsInstalled\{GUID} that
 * control the per-user installation.
 *
 * PARAMS
 *   pPerUser [I] Per-user stub values.
 *
 * RETURNS
 *   Success: S_OK.
 *   Failure: E_FAIL.
 */
HRESULT WINAPI SetPerUserSecValuesW(PERUSERSECTIONW* pPerUser)
{
    HKEY setup, guid;

    static const WCHAR setup_key[] = {
        'S','O','F','T','W','A','R','E','\\',
        'M','i','c','r','o','s','o','f','t','\\',
        'A','c','t','i','v','e',' ','S','e','t','u','p','\\',
        'I','n','s','t','a','l','l','e','d',' ',
        'C','o','m','p','o','n','e','n','t','s',0
    };

    static const WCHAR stub_path[] = {'S','t','u','b','P','a','t','h',0};
    static const WCHAR version[] = {'V','e','r','s','i','o','n',0};
    static const WCHAR locale[] = {'L','o','c','a','l','e',0};
    static const WCHAR compid[] = {'C','o','m','p','o','n','e','n','t','I','D',0};
    static const WCHAR isinstalled[] = {'I','s','I','n','s','t','a','l','l','e','d',0};

    TRACE("(%p)\n", pPerUser);

    if (!pPerUser || !*pPerUser->szGUID)
        return S_OK;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, setup_key, 0, NULL, 0, KEY_WRITE,
                        NULL, &setup, NULL))
    {
        return E_FAIL;
    }

    if (RegCreateKeyExW(setup, pPerUser->szGUID, 0, NULL, 0, KEY_ALL_ACCESS,
                        NULL, &guid, NULL))
    {
        RegCloseKey(setup);
        return E_FAIL;
    }

    if (*pPerUser->szStub)
    {
        RegSetValueExW(guid, stub_path, 0, REG_SZ, (LPBYTE)pPerUser->szStub,
                       (lstrlenW(pPerUser->szStub) + 1) * sizeof(WCHAR));
    }

    if (*pPerUser->szVersion)
    {
        RegSetValueExW(guid, version, 0, REG_SZ, (LPBYTE)pPerUser->szVersion,
                       (lstrlenW(pPerUser->szVersion) + 1) * sizeof(WCHAR));
    }

    if (*pPerUser->szLocale)
    {
        RegSetValueExW(guid, locale, 0, REG_SZ, (LPBYTE)pPerUser->szLocale,
                       (lstrlenW(pPerUser->szLocale) + 1) * sizeof(WCHAR));
    }

    if (*pPerUser->szCompID)
    {
        RegSetValueExW(guid, compid, 0, REG_SZ, (LPBYTE)pPerUser->szCompID,
                       (lstrlenW(pPerUser->szCompID) + 1) * sizeof(WCHAR));
    }

    if (*pPerUser->szDispName)
    {
        RegSetValueExW(guid, NULL, 0, REG_SZ, (LPBYTE)pPerUser->szDispName,
                       (lstrlenW(pPerUser->szDispName) + 1) * sizeof(WCHAR));
    }

    RegSetValueExW(guid, isinstalled, 0, REG_DWORD,
                   (LPBYTE)&pPerUser->dwIsInstalled, sizeof(DWORD));

    RegCloseKey(guid);
    RegCloseKey(setup);

    return S_OK;
}

/***********************************************************************
 *             TranslateInfStringA   (ADVPACK.@)
 *
 * See TranslateInfStringW.
 */
HRESULT WINAPI TranslateInfStringA(LPCSTR pszInfFilename, LPCSTR pszInstallSection,
                LPCSTR pszTranslateSection, LPCSTR pszTranslateKey, LPSTR pszBuffer,
                DWORD dwBufferSize, PDWORD pdwRequiredSize, PVOID pvReserved)
{
    UNICODE_STRING filenameW, installW;
    UNICODE_STRING translateW, keyW;
    LPWSTR bufferW;
    HRESULT res;
    DWORD len = 0;

    TRACE("(%s, %s, %s, %s, %p, %ld, %p, %p)\n",
          debugstr_a(pszInfFilename), debugstr_a(pszInstallSection),
          debugstr_a(pszTranslateSection), debugstr_a(pszTranslateKey),
          pszBuffer, dwBufferSize,pdwRequiredSize, pvReserved);

    if (!pszInfFilename || !pszTranslateSection ||
        !pszTranslateKey || !pdwRequiredSize)
        return E_INVALIDARG;

    RtlCreateUnicodeStringFromAsciiz(&filenameW, pszInfFilename);
    RtlCreateUnicodeStringFromAsciiz(&installW, pszInstallSection);
    RtlCreateUnicodeStringFromAsciiz(&translateW, pszTranslateSection);
    RtlCreateUnicodeStringFromAsciiz(&keyW, pszTranslateKey);

    res = TranslateInfStringW(filenameW.Buffer, installW.Buffer,
                              translateW.Buffer, keyW.Buffer, NULL,
                              dwBufferSize, &len, NULL);

    if (res == S_OK)
    {
        bufferW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

        res = TranslateInfStringW(filenameW.Buffer, installW.Buffer,
                                  translateW.Buffer, keyW.Buffer, bufferW,
                                  len, &len, NULL);
        if (res == S_OK)
        {
            *pdwRequiredSize = WideCharToMultiByte(CP_ACP, 0, bufferW, -1,
                                                   NULL, 0, NULL, NULL);

            if (dwBufferSize >= *pdwRequiredSize)
            {
                WideCharToMultiByte(CP_ACP, 0, bufferW, -1, pszBuffer,
                                    dwBufferSize, NULL, NULL);
            }
            else
                res = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        
        HeapFree(GetProcessHeap(), 0, bufferW);
    }

    RtlFreeUnicodeString(&filenameW);
    RtlFreeUnicodeString(&installW);
    RtlFreeUnicodeString(&translateW);
    RtlFreeUnicodeString(&keyW);

    return res;
}

/***********************************************************************
 *             TranslateInfStringW   (ADVPACK.@)
 *
 * Translates the value of a specified key in an inf file into the
 * current locale by expanding string macros.
 *
 * PARAMS
 *   pszInfFilename      [I] Filename of the inf file.
 *   pszInstallSection   [I]
 *   pszTranslateSection [I] Inf section where the key exists.
 *   pszTranslateKey     [I] Key to translate.
 *   pszBuffer           [O] Contains the translated string on exit.
 *   dwBufferSize        [I] Size on input of pszBuffer.
 *   pdwRequiredSize     [O] Length of the translated key.
 *   pvReserved          [I] Reserved, must be NULL.
 *
 * RETURNS
 *   Success: S_OK.
 *   Failure: An hresult error code.
 */
HRESULT WINAPI TranslateInfStringW(LPCWSTR pszInfFilename, LPCWSTR pszInstallSection,
                LPCWSTR pszTranslateSection, LPCWSTR pszTranslateKey, LPWSTR pszBuffer,
                DWORD dwBufferSize, PDWORD pdwRequiredSize, PVOID pvReserved)
{
    HINF hInf;

    TRACE("(%s, %s, %s, %s, %p, %ld, %p, %p)\n",
          debugstr_w(pszInfFilename), debugstr_w(pszInstallSection),
          debugstr_w(pszTranslateSection), debugstr_w(pszTranslateKey),
          pszBuffer, dwBufferSize,pdwRequiredSize, pvReserved);

    if (!pszInfFilename || !pszTranslateSection ||
        !pszTranslateKey || !pdwRequiredSize)
        return E_INVALIDARG;

    hInf = SetupOpenInfFileW(pszInfFilename, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

    set_ldids(hInf, pszInstallSection);

    if (!SetupGetLineTextW(NULL, hInf, pszTranslateSection, pszTranslateKey,
                           pszBuffer, dwBufferSize, pdwRequiredSize))
    {
        if (dwBufferSize < *pdwRequiredSize)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

        return SPAPI_E_LINE_NOT_FOUND;
    }

    SetupCloseInfFile(hInf);
    return S_OK;
}

/***********************************************************************
 *             TranslateInfStringExA   (ADVPACK.@)
 *
 * See TranslateInfStringExW.
 */
HRESULT WINAPI TranslateInfStringExA(HINF hInf, LPCSTR pszInfFilename,
                                    LPCSTR pszTranslateSection, LPCSTR pszTranslateKey,
                                    LPSTR pszBuffer, DWORD dwBufferSize,
                                    PDWORD pdwRequiredSize, PVOID pvReserved)
{
    UNICODE_STRING filenameW, sectionW, keyW;
    LPWSTR bufferW;
    HRESULT res;
    DWORD len = 0;

    TRACE("(%p, %s, %s, %s, %s, %ld, %p, %p)\n", hInf, debugstr_a(pszInfFilename),
          debugstr_a(pszTranslateSection), debugstr_a(pszTranslateKey),
          debugstr_a(pszBuffer), dwBufferSize, pdwRequiredSize, pvReserved);

    if (!pszInfFilename || !pszTranslateSection ||
        !pszTranslateKey || !pdwRequiredSize)
        return E_INVALIDARG;

    RtlCreateUnicodeStringFromAsciiz(&filenameW, pszInfFilename);
    RtlCreateUnicodeStringFromAsciiz(&sectionW, pszTranslateSection);
    RtlCreateUnicodeStringFromAsciiz(&keyW, pszTranslateKey);

    res = TranslateInfStringExW(hInf, filenameW.Buffer, sectionW.Buffer,
                                keyW.Buffer, NULL, 0, &len, NULL);

    if (res == S_OK)
    {
        bufferW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

        res = TranslateInfStringExW(hInf, filenameW.Buffer, sectionW.Buffer,
                                keyW.Buffer, bufferW, len, &len, NULL);

        if (res == S_OK)
        {
            *pdwRequiredSize = WideCharToMultiByte(CP_ACP, 0, bufferW, -1,
                                                   NULL, 0, NULL, NULL);

            if (dwBufferSize >= *pdwRequiredSize)
            {
                WideCharToMultiByte(CP_ACP, 0, bufferW, -1, pszBuffer,
                                    dwBufferSize, NULL, NULL);
            }
            else
                res = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        
        HeapFree(GetProcessHeap(), 0, bufferW);
    }

    RtlFreeUnicodeString(&filenameW);
    RtlFreeUnicodeString(&sectionW);
    RtlFreeUnicodeString(&keyW);

    return res;
}

/***********************************************************************
 *             TranslateInfStringExW   (ADVPACK.@)
 *
 * Using a handle to an INF file opened with OpenINFEngine, translates
 * the value of a specified key in an inf file into the current locale
 * by expanding string macros.
 *
 * PARAMS
 *   hInf                [I] Handle to the INF file.
 *   pszInfFilename      [I] Filename of the INF file.
 *   pszTranslateSection [I] Inf section where the key exists.
 *   pszTranslateKey     [I] Key to translate.
 *   pszBuffer           [O] Contains the translated string on exit.
 *   dwBufferSize        [I] Size on input of pszBuffer.
 *   pdwRequiredSize     [O] Length of the translated key.
 *   pvReserved          [I] Reserved.  Must be NULL.
 *
 * RETURNS
 *   Success: S_OK.
 *   Failure: E_FAIL.
 *
 * NOTES
 *   To use TranslateInfStringEx to translate an INF file continuously,
 *   open the INF file with OpenINFEngine, call TranslateInfStringEx as
 *   many times as needed, then release the handle with CloseINFEngine.
 *   When translating more than one keys, this method is more efficient
 *   than calling TranslateInfString, because the INF file is only
 *   opened once.
 */
HRESULT WINAPI TranslateInfStringExW(HINF hInf, LPCWSTR pszInfFilename,
                                     LPCWSTR pszTranslateSection, LPCWSTR pszTranslateKey,
                                     LPWSTR pszBuffer, DWORD dwBufferSize,
                                     PDWORD pdwRequiredSize, PVOID pvReserved)
{
    TRACE("(%p, %s, %s, %s, %s, %ld, %p, %p)\n", hInf, debugstr_w(pszInfFilename),
          debugstr_w(pszTranslateSection), debugstr_w(pszTranslateKey),
          debugstr_w(pszBuffer), dwBufferSize, pdwRequiredSize, pvReserved);

    if (!hInf || !pszInfFilename || !pszTranslateSection || !pszTranslateKey)
        return E_INVALIDARG;

    if (!SetupGetLineTextW(NULL, hInf, pszTranslateSection, pszTranslateKey,
                           pszBuffer, dwBufferSize, pdwRequiredSize))
    {
        if (dwBufferSize < *pdwRequiredSize)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

        return SPAPI_E_LINE_NOT_FOUND;
    }

    return S_OK;   
}

/***********************************************************************
 *             UserInstStubWrapperA   (ADVPACK.@)
 *
 * See UserInstStubWrapperW.
 */
HRESULT WINAPI UserInstStubWrapperA(HWND hWnd, HINSTANCE hInstance,
                                   LPSTR pszParms, INT nShow)
{
    UNICODE_STRING parmsW;
    HRESULT res;

    TRACE("(%p, %p, %s, %i)\n", hWnd, hInstance, debugstr_a(pszParms), nShow);

    if (!pszParms)
        return E_INVALIDARG;

    RtlCreateUnicodeStringFromAsciiz(&parmsW, pszParms);

    res = UserInstStubWrapperW(hWnd, hInstance, parmsW.Buffer, nShow);

    RtlFreeUnicodeString(&parmsW);

    return res;
}

/***********************************************************************
 *             UserInstStubWrapperW   (ADVPACK.@)
 */
HRESULT WINAPI UserInstStubWrapperW(HWND hWnd, HINSTANCE hInstance,
                                    LPWSTR pszParms, INT nShow)
{
    FIXME("(%p, %p, %s, %i): stub\n", hWnd, hInstance, debugstr_w(pszParms), nShow);

    return E_FAIL;
}

/***********************************************************************
 *             UserUnInstStubWrapperA   (ADVPACK.@)
 *
 * See UserUnInstStubWrapperW.
 */
HRESULT WINAPI UserUnInstStubWrapperA(HWND hWnd, HINSTANCE hInstance,
                                      LPSTR pszParms, INT nShow)
{
    UNICODE_STRING parmsW;
    HRESULT res;

    TRACE("(%p, %p, %s, %i)\n", hWnd, hInstance, debugstr_a(pszParms), nShow);

    if (!pszParms)
        return E_INVALIDARG;

    RtlCreateUnicodeStringFromAsciiz(&parmsW, pszParms);

    res = UserUnInstStubWrapperW(hWnd, hInstance, parmsW.Buffer, nShow);

    RtlFreeUnicodeString(&parmsW);

    return res;
}

/***********************************************************************
 *             UserUnInstStubWrapperW   (ADVPACK.@)
 */
HRESULT WINAPI UserUnInstStubWrapperW(HWND hWnd, HINSTANCE hInstance,
                                      LPWSTR pszParms, INT nShow)
{
    FIXME("(%p, %p, %s, %i): stub\n", hWnd, hInstance, debugstr_w(pszParms), nShow);

    return E_FAIL;
}
