//---------------------------------------------------------------------------
//
// Copyright (c) Microsoft Corporation 1993-1994
//
// File: init.c
//
//  This file contains the library entry points
//
// Usage and assumptions used in this DLL.
//
//  1) Message crackers are used.  See windowsx.h and windowsx.txt.
//
//  2) Many functions are considered "member functions" of a
//     particular class.  Because this is not C++, the function
//     names follow a special naming convention: "Class_Name".
//     In addition, it is common practice that the first
//     argument for these type of functions is a "this" pointer
//     to the particular object.
//
// History:
//  08-06-93 ScottH     Transferred from twin code
//
//---------------------------------------------------------------------------

/////////////////////////////////////////////////////  INCLUDES

#include "brfprv.h"         // common headers
#include <brfcasep.h>

#define INITGUID            // Initialize GUIDs
#include <initguid.h>
#include <oleguid.h>
#include <coguid.h>
#include <shlguid.h>
#include <shguidp.h>        // Contains CLSID_Briefcase

#include "res.h"
#include "recact.h"

#ifdef DEBUG
#include <debugstr.h>
#endif


//---------------------------------------------------------------------------
// Per instance data
//---------------------------------------------------------------------------


HINSTANCE   g_hinst = 0;
TWINRESULT  g_tr = TR_SUCCESS;

// Debugging variables
UINT g_uBreakFlags = 0;         // Controls when to int 3
UINT g_uTraceFlags = 0;         // Controls what trace messages are spewed
UINT g_uDumpFlags = 0;          // Controls what structs get dumped

// The delay mutex and the cs that protects the cRef is per-instance
HANDLE g_hMutexDelay = NULL;
static UINT g_cRefMutex = 0;
static CRITICAL_SECTION s_csDelay = { 0 };



//---------------------------------------------------------------------------
// Global data
//---------------------------------------------------------------------------

CRITICAL_SECTION g_csSyncUI = { 0 };
DEBUG_CODE( UINT g_cRefSyncUI = 0; )
UINT g_cfBriefObj = 0;
BOOL g_bMirroredOS = FALSE;
// Use the helper macros in brfprv.h
UINT g_cBusyRef = 0;            // Semaphore
UINT g_cBriefRef = 0;           // Semaphore

// Metrics
int g_cxIconSpacing = 0;
int g_cyIconSpacing = 0;
int g_cxBorder = 0;
int g_cyBorder = 0;
int g_cxIcon = 0;
int g_cyIcon = 0;
int g_cxIconMargin = 0;
int g_cyIconMargin = 0;
int g_cxLabelMargin = 0;
int g_cyLabelSpace = 0;
int g_cxMargin = 0;

// System colors
COLORREF g_clrHighlightText = 0;
COLORREF g_clrHighlight = 0;
COLORREF g_clrWindowText = 0;
COLORREF g_clrWindow = 0;

HBRUSH g_hbrHighlight = 0;
HBRUSH g_hbrWindow = 0;

// Strings
TCHAR g_szDBName[MAXPATHLEN];
TCHAR g_szDBNameShort[MAXPATHLEN];

// Get the system metrics we need

void PRIVATE GetMetrics(WPARAM wParam)      // wParam from WM_WININICHANGE
{
    if ((wParam == 0) || (wParam == SPI_SETNONCLIENTMETRICS))
    {
        g_cxIconSpacing = GetSystemMetrics( SM_CXICONSPACING );
        g_cyIconSpacing = GetSystemMetrics( SM_CYICONSPACING );
        
        g_cxBorder = GetSystemMetrics(SM_CXBORDER);
        g_cyBorder = GetSystemMetrics(SM_CYBORDER);
        
        g_cxIcon = GetSystemMetrics(SM_CXICON);
        g_cyIcon = GetSystemMetrics(SM_CYICON);
        
        g_cxIconMargin = g_cxBorder * 8;
        g_cyIconMargin = g_cyBorder * 2;
        g_cyLabelSpace = g_cyIconMargin + (g_cyBorder * 2);
        g_cxLabelMargin = (g_cxBorder * 2);
        g_cxMargin = g_cxBorder * 5;
    }
}

// Initializes colors

void PRIVATE InitGlobalColors()
{
    g_clrWindowText = GetSysColor(COLOR_WINDOWTEXT);
    g_clrWindow = GetSysColor(COLOR_WINDOW);
    g_clrHighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    g_clrHighlight = GetSysColor(COLOR_HIGHLIGHT);
    
    g_hbrWindow = GetSysColorBrush(COLOR_WINDOW);
    g_hbrHighlight = GetSysColorBrush(COLOR_HIGHLIGHT);
}


// Initialize global strings

void PRIVATE InitGlobalStrings()
{
    SzFromIDS(IDS_BC_DATABASE, g_szDBName, ARRAYSIZE(g_szDBName));
    SzFromIDS(IDS_BC_DATABASE_SHORT, g_szDBNameShort, ARRAYSIZE(g_szDBNameShort));
}


// Initialize the DLL on the first PROCESS_ATTACH

BOOL PRIVATE InitializeFirstTime(void)
{
    BOOL bRet = FALSE;
    
    InitCommonControls();
    
    GetMetrics(0);
    
    CPATH_InitCS();
    CBS_InitCS();
    CRL_InitCS();
    
    if (!Atom_Init())
        goto Init_Cleanup;
    
    if (!CPATH_Init())
        goto Init_Cleanup;
    
    // We do not load the engine DLL until we really need it.
    
    // Initialize our global imagelist
    //
    g_cfBriefObj = RegisterClipboardFormat(CFSTR_BRIEFOBJECT);
    if (g_cfBriefObj == 0)
        goto Init_Cleanup;
    
    bRet = TRUE;
    
Init_Cleanup:
    if (bRet == FALSE)
    {
        Atom_Term();
    }
    
    return bRet;
}

// Register window classes per process

BOOL PRIVATE InitWindowClasses(HINSTANCE hinst)
{
    return RecAct_Init(hinst);
}

// Terminate DLL on the last PROCESS_DETACH

void PRIVATE FinalTerminate(HINSTANCE hinst)
{
    CPATH_Term();
    Atom_Term();
    
    CRL_DeleteCS();
    CBS_DeleteCS();
    CPATH_DeleteCS();
    
    Mem_Terminate();
}


// Unregister window classes per process
void PRIVATE TermWindowClasses(HINSTANCE hinst)
{
    RecAct_Term(hinst);
}

// Purpose: Obtain ownership of the delay-calculation mutex
// Returns: reference count

UINT PUBLIC Delay_Own(void)
{
    UINT cRef;
    
    EnterCriticalSection(&s_csDelay);
    {
        if (0 == g_cRefMutex++)
        {
            // Obtain ownership of the mutex.  This will get released
            // when Delay_Release is called.
            LeaveCriticalSection(&s_csDelay);
            {
                MsgWaitObjectsSendMessage(1, &g_hMutexDelay, INFINITE);
            }
            EnterCriticalSection(&s_csDelay);
            
            TRACE_MSG(TF_GENERAL, TEXT("Set delay mutex"));
        }
        cRef = g_cRefMutex;
    }
    LeaveCriticalSection(&s_csDelay);
    
    return cRef;
}


/*----------------------------------------------------------
Purpose: Release ownership of the delay-calculation mutex

  Returns: reference count
  Cond:    --
*/
UINT PUBLIC Delay_Release(void)
{
    UINT cRef;
    
    EnterCriticalSection(&s_csDelay);
    {
        ASSERT(0 < g_cRefMutex);
        
        if (0 < g_cRefMutex)
        {
            if (0 == --g_cRefMutex)
            {
                ReleaseMutex(g_hMutexDelay);
                
                TRACE_MSG(TF_GENERAL, TEXT("Release delay mutex"));
            }
        }
        cRef = g_cRefMutex;
    }
    LeaveCriticalSection(&s_csDelay);
    
    return cRef;
}

void PUBLIC Brief_EnterExclusive(void)
{
    EnterCriticalSection(&g_csSyncUI);
#ifdef DEBUG
    g_cRefSyncUI++;
#endif
}


void PUBLIC Brief_LeaveExclusive(void)
{
#ifdef DEBUG
    g_cRefSyncUI--;
#endif
    LeaveCriticalSection(&g_csSyncUI);
}

BOOL ProcessAttach(HINSTANCE hDll)
{
    DWORD dwLayout = 0;
    BOOL bSuccess = TRUE;
    
    SHFusionInitializeFromModule(hDll);

    InitializeCriticalSection(&g_csSyncUI);
    
    InitializeCriticalSection(&s_csDelay);
    
    g_hinst = hDll;
    
#ifdef DEBUG
    // We do this simply to load the debug .ini flags
    ProcessIniFile();
    DEBUG_BREAK(BF_ONPROCESSATT);
#endif
    
    // Under NT, we need to initialize on every process attach, not just the first
    
    bSuccess = InitializeFirstTime();
    
    if (bSuccess)
    {
        g_hMutexDelay = CreateMutex(NULL, FALSE, TEXT("SYNCUI"));
        bSuccess = (NULL != g_hMutexDelay);
    }
    
    if (bSuccess)
    {
        // (Only do this if we succeeded above)
        //
        // Do the following for every process
        bSuccess = InitWindowClasses(hDll);
    }
    
    InitGlobalColors();
    InitGlobalStrings();
    
    //Bug 199701, 199647, 199699
    //Apparently this is either broke or never worked: g_bMirroredOS = IS_MIRRORING_ENABLED();  
    GetProcessDefaultLayout(&dwLayout);
    if (dwLayout == LAYOUT_RTL)
    {
        g_bMirroredOS = TRUE;
    }
    //End bug 199701, 199647, 199699

    return bSuccess;
}

BOOL ProcessDetach(HINSTANCE hDll)
{
    BOOL bSuccess = TRUE;
    
    ASSERT(hDll == g_hinst);
    
    DEBUG_CODE( DEBUG_BREAK(BF_ONPROCESSDET); )
    
    ASSERT(0 == g_cRefMutex);
    
    if (g_hMutexDelay)
    {
        CloseHandle(g_hMutexDelay);
        g_hMutexDelay = NULL;
    }
    
    FinalTerminate(g_hinst);
    
    Sync_ReleaseVTable();

    DeleteCriticalSection(&g_csSyncUI);
    
    DeleteCriticalSection(&s_csDelay);
    
    TermWindowClasses(hDll);

    SHFusionUninitialize();
    
    return bSuccess;
}

BOOL APIENTRY LibMain(HANDLE hDll, DWORD dwReason, void *lpReserved)
{
    switch(dwReason)
    {
    case DLL_PROCESS_ATTACH:
        ProcessAttach(hDll);
#ifndef DEBUG
        DisableThreadLibraryCalls(hDll);
#endif
        break;
        
    case DLL_PROCESS_DETACH:
        ProcessDetach(hDll);
        break;
        
    case DLL_THREAD_ATTACH:
        
#ifdef DEBUG
        // We do this simply to load the debug .ini flags
        ProcessIniFile();
#endif
        break;
        
    case DLL_THREAD_DETACH:
        
#ifdef DEBUG
        DEBUG_BREAK(BF_ONTHREADDET);
#endif
        break;
        
    default:
        break;
    }
    
    return TRUE;
}


/*----------------------------------------------------------
Purpose: Registers property sheet and context menu extensions
for the briefcase.

  Returns: TRUE on success
  Cond:    --
*/
BOOL PRIVATE RegisterShellExtension(void)
{
    const static TCHAR c_szPage[] = STRREG_SHEX_PROPSHEET TEXT("\\BriefcasePage");
    const static TCHAR c_szCM[] = STRREG_SHEX_MENUHANDLER TEXT("\\BriefcaseMenu");
    const static TCHAR c_szFolder[] = TEXT("Folder");
    const static TCHAR c_szStar[] = TEXT("*");
    const static TCHAR c_szFmt[] = TEXT("SOFTWARE\\Classes\\%s\\%s");
    // This must be per instance, else it will cause a fixup in
    // shared data segment.
    const static LPCTSTR rgpsz[2] = { c_szFolder, c_szStar };
    TCHAR sz[MAXBUFLEN];
    int i;
    
    for (i = 0; i < ARRAYSIZE(rgpsz); i++)
    {
        // Add briefcase page extension
        wsprintf(sz, c_szFmt, (LPCTSTR)rgpsz[i], (LPCTSTR)c_szPage);
        RegSetValue(HKEY_LOCAL_MACHINE, sz, REG_SZ, c_szCLSID, lstrlen(c_szCLSID));
        
        // Add briefcase context menu extension
        wsprintf(sz, c_szFmt, (LPCTSTR)rgpsz[i], (LPCTSTR)c_szCM);
        RegSetValue(HKEY_LOCAL_MACHINE, sz, REG_SZ, c_szCLSID, lstrlen(c_szCLSID));
    }
    return TRUE;
}


/*----------------------------------------------------------
Purpose: Create a briefcase at the specified location.

  Returns: TRUE on success
  Cond:    --
*/
BOOL PRIVATE CreateTheBriefcase(HWND hwnd, LPCTSTR pszNewPath)
{
    BOOL bRet = FALSE;
    TCHAR szParent[MAX_PATH];
    TCHAR szTmp[MAX_PATH];
    
    DEBUG_CODE( TRACE_MSG(TF_ALWAYS, TEXT("Creating %s"), (LPCTSTR)pszNewPath); )
        
        // We do not allow briefcases to be created inside other briefcases.
        
        lstrcpy(szParent, pszNewPath);
    PathRemoveFileSpec(szParent);
    
    // Is this inside another briefcase?
    if (PL_FALSE != PathGetLocality(szParent, szTmp))
    {
        // Yes; don't do it!
        MsgBox(hwnd,
            MAKEINTRESOURCE(IDS_ERR_CREATE_INANOTHER),
            MAKEINTRESOURCE(IDS_CAP_CREATE),
            NULL,
            MB_WARNING);
    }
    else if (CreateDirectory(pszNewPath, NULL))
    {
        // Mark the briefcase as a system directory
        //
        if (!SetFileAttributes(pszNewPath, FILE_ATTRIBUTE_READONLY))
        {
            TRACE_MSG(TF_ALWAYS, TEXT("Cannot make %s a system directory"), (LPCTSTR)pszNewPath);
            RemoveDirectory(pszNewPath);
            
            MsgBox(hwnd,
                MAKEINTRESOURCE(IDS_ERR_CANTCREATEBC),
                MAKEINTRESOURCE(IDS_CAP_CREATE),
                NULL,
                MB_ERROR,
                pszNewPath);
        }
        else
        {
            const static TCHAR c_szConfirmFileOp[] = TEXT("ConfirmFileOp");
            HBRFCASE hbrf;
            TWINRESULT tr;
            LPCTSTR pszDBName;
            DECLAREHOURGLASS;
            
            if (PathsTooLong(pszNewPath, c_szDesktopIni) ||
                PathsTooLong(pszNewPath, g_szDBName) ||
                PathsTooLong(pszNewPath, g_szDBNameShort))
            {
                MsgBox(hwnd,
                    MAKEINTRESOURCE(IDS_ERR_CREATE_TOOLONG),
                    MAKEINTRESOURCE(IDS_CAP_CREATE),
                    NULL,
                    MB_ERROR);
            }
            else
            {
                // Write in the desktop.ini the briefcase class ID
                PathCombine(szTmp, pszNewPath, c_szDesktopIni);
                // (First, flush the cache to make sure the desktop.ini
                // file is really created.)
                WritePrivateProfileString(NULL, NULL, NULL, szTmp);
                WritePrivateProfileString(STRINI_CLASSINFO, c_szIniKeyCLSID, c_szCLSID, szTmp);
                WritePrivateProfileString(STRINI_CLASSINFO, c_szConfirmFileOp, TEXT("0"), szTmp);
                
                // Make wizard run the first time it is opened.
                WritePrivateProfileString(STRINI_CLASSINFO, c_szRunWizard, TEXT("1"), szTmp);
                
                // Hide the desktop.ini since the shell does not selectively hide it.
                // Also make it readonly so that the user can't customize the briefcase.
                if (!SetFileAttributes(szTmp, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
                {
                    TRACE_MSG(TF_ALWAYS, TEXT("Cannot hide and/or make read-only %s"), (LPCTSTR)szTmp);
                }
                
                RegisterShellExtension();
                
                PathNotifyShell(pszNewPath, NSE_MKDIR, TRUE);
                
                // Create the database file
                SetHourglass();
                
                if (IsLFNDrive(pszNewPath))
                    pszDBName = g_szDBName;
                else
                    pszDBName = g_szDBNameShort;
                
                PathCombine(szTmp, pszNewPath, pszDBName);
                if (Sync_QueryVTable())
                {
                    tr = Sync_OpenBriefcase(szTmp,
                        OB_FL_OPEN_DATABASE | OB_FL_TRANSLATE_DB_FOLDER | OB_FL_ALLOW_UI,
                        hwnd,
                        &hbrf);
                    if (TR_SUCCESS == tr)
                    {
                        // (Don't really care about errors here)
                        Sync_SaveBriefcase(hbrf);
                        Sync_CloseBriefcase(hbrf);
                    }
                }
                ResetHourglass();
                
                bRet = TRUE;
            }
        }
    }
    else
    {
        // Could not create the directory.  Is it because a briefcase
        // already exists at this location?
        if (PathExists(pszNewPath))
        {
            // Yes
            TRACE_MSG(TF_ALWAYS, TEXT("Briefcase already exists at this location: %s"), (LPCTSTR)pszNewPath);
        }
        else
        {
            // No
            MsgBox(hwnd,
                MAKEINTRESOURCE(IDS_ERR_CANTCREATEBC),
                MAKEINTRESOURCE(IDS_CAP_CREATE),
                NULL,
                MB_ERROR,
                pszNewPath);
        }
    }
    
    return bRet;
}


/*----------------------------------------------------------
Purpose: Adds the briefcase at pszPath to the SendTo folder

  Returns: standard result
  Cond:    --
*/
HRESULT PRIVATE AddBriefcaseToSendToFolder(HWND hwnd, LPCTSTR pszPath)
{
    HRESULT hres = E_OUTOFMEMORY;
    TCHAR szSendTo[MAX_PATH];
    
    Shell_GetImageLists( NULL, NULL ); // make sure icon cache is around
    
    if (SHGetSpecialFolderPath(hwnd, szSendTo, CSIDL_SENDTO, FALSE))
    {
        LPITEMIDLIST pidl = ILCreateFromPath(pszPath);
        if (pidl)
        {
            LPITEMIDLIST pidlParent = ILClone(pidl);
            if (pidlParent)
            {
                IShellFolder * psf;
                IShellFolder * psfDesktop;
                
                ILRemoveLastID(pidlParent);
                
                hres = SHGetDesktopFolder(&psfDesktop);
                if (SUCCEEDED(hres))
                {
                    hres = psfDesktop->lpVtbl->BindToObject(psfDesktop, pidlParent,
                        NULL, &IID_IShellFolder, &psf);
                
                    if (SUCCEEDED(hres))
                    {
                        IDataObject *pdtobj;
                        LPCITEMIDLIST pidlName = ILFindLastID(pidl);
                    
                        hres = psf->lpVtbl->GetUIObjectOf(psf, hwnd, 1, &pidlName, &IID_IDataObject, NULL, &pdtobj);
                        if (SUCCEEDED(hres))
                        {
                            SHCreateLinks(hwnd, szSendTo, pdtobj, 0, NULL);
                            pdtobj->lpVtbl->Release(pdtobj);
                        }
                        psf->lpVtbl->Release(psf);
                    }
                    psfDesktop->lpVtbl->Release(psfDesktop);
                }
                ILFree(pidlParent);
            }
            ILFree(pidl);
        }
    }
    return hres;
}


/*----------------------------------------------------------
Purpose: Creates a briefcase in the specified directory.

  Returns: --
  Cond:    --
*/
void WINAPI Briefcase_CreateInDirectory(HWND hwnd, HWND hwndCabinet, LPCTSTR pszPath)
{
    if (CreateTheBriefcase(hwnd, pszPath))
    {
        // Select the newly created item to edit it
        LPITEMIDLIST pidl = ILCreateFromPath(pszPath);
        if (pidl)
        {
            SelectItemInCabinet(hwndCabinet, ILFindLastID(pidl), TRUE);
            ILFree(pidl);
        }
    }
}


/*----------------------------------------------------------
Purpose: Creates a briefcase on the desktop.

  Returns: --
  Cond:    --
*/
void WINAPI Briefcase_CreateOnDesktop(HWND hwnd)
{
    // Place it on the desktop
    TCHAR szPath[MAX_PATH];
    
    if (SHGetSpecialFolderPath(hwnd, szPath, CSIDL_DESKTOPDIRECTORY, FALSE))
    {
        int cch;
        UINT ids;
        
        if (IsLFNDrive(szPath))
            ids = IDS_BC_NAME;
        else
            ids = IDS_BC_NAME_SHORT;
        
        lstrcat(szPath, TEXT("\\"));
        cch = lstrlen(szPath);
        LoadString(g_hinst, ids, &szPath[cch], ARRAYSIZE(szPath)-cch);
        if (CreateTheBriefcase(hwnd, szPath))
        {
            // Add a shortcut of this briefcase to the SendTo folder
            AddBriefcaseToSendToFolder(hwnd, szPath);
        }
    }
}



/*----------------------------------------------------------
Purpose: Create a briefcase folder in the specified 
directory or on the desktop.
Returns: --
Cond:    --
*/
void WINAPI _export Briefcase_Create_Common(HWND hwnd, HINSTANCE hAppInstance, LPTSTR pszCmdLine, int nCmdShow)
{
    DEBUG_CODE( DEBUG_BREAK(BF_ONRUNONCE); )
        
    // Command line should be of format "xxxx path" where <path>
    // is the fully qualified pathname of the briefcase to create,
    // and <xxxx> is the explorer hwnd.
    
    if (pszCmdLine && *pszCmdLine)
    {
        LPTSTR psz;
        HWND hwndCabinet;
        
        // Get hwnd
        hwndCabinet = IntToPtr(AnsiToInt(pszCmdLine));
        psz = StrChr(pszCmdLine, TEXT(' '));
        if (NULL != hwndCabinet && NULL != psz)
        {
            Briefcase_CreateInDirectory(hwnd, hwndCabinet, CharNext(psz));
            return;
        }
    }
    Briefcase_CreateOnDesktop(hwnd);
}

void WINAPI _export Briefcase_Create(HWND hwnd, HINSTANCE hAppInstance, LPSTR pszCmdLine, int nCmdShow)
{
    UINT iLength = lstrlenA(pszCmdLine)+1;
    LPWSTR lpwszCmdLine;
    
    lpwszCmdLine = (LPWSTR)LocalAlloc(LPTR, iLength *SIZEOF(TCHAR));
    if (lpwszCmdLine)
    {
        MultiByteToWideChar(CP_ACP, 0,
            pszCmdLine, -1,
            lpwszCmdLine, iLength);
        
        Briefcase_Create_Common(hwnd, hAppInstance, lpwszCmdLine, nCmdShow);
        LocalFree((HANDLE)lpwszCmdLine);
    }
}

void WINAPI _export Briefcase_CreateW(HWND hwnd, HINSTANCE hAppInstance, LPWSTR pwszCmdLine, int nCmdShow)
{
    Briefcase_Create_Common(hwnd, hAppInstance, pwszCmdLine, nCmdShow);
}




/*----------------------------------------------------------
Purpose: Display the introduction "wizard".  (It's really not
a wizard since it is not making anything for us.)

  NOTE: This function serves double duty for both the ansi and unicode
  versions. It never uses the command line.
  
    Returns: --
    Cond:    --
*/
void WINAPI _export Briefcase_Intro(
                                    HWND hwnd,
                                    HINSTANCE hAppInstance,
                                    LPTSTR lpszCmdLine,
                                    int nCmdShow)
{
    Intro_DoModal(hwnd);
}


//---------------------------------------------------------------------------
// DLL entry-points
//---------------------------------------------------------------------------


/*----------------------------------------------------------
Purpose: This function is called back from within
IClassFactory::CreateInstance() of the default class
factory object, which is created by SHCreateClassObject.

  Returns: standard
  Cond:    --
*/
HRESULT CALLBACK DllFactoryCallback(IUnknown *punkOuter, REFIID riid, void **ppvOut)
{
    HRESULT hres;
    
    if (IsEqualIID(riid, &IID_IShellExtInit))
    {
        hres = BriefExt_CreateInstance(punkOuter, riid, ppvOut);
    }
    else if (IsEqualIID(riid, &IID_IBriefcaseStg))
    {
        hres = BriefStg_CreateInstance(punkOuter, riid, ppvOut);
    }
    else
    {
        hres = E_NOINTERFACE;
        *ppvOut = NULL;
    }
    return hres;
}


/*----------------------------------------------------------
Purpose: Standard OLE 2.0 entry-point

  Returns: standard
  Cond:    --
*/
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppvOut)
{
    HRESULT hres;
    
    if (IsEqualIID(rclsid, &CLSID_Briefcase))
    {
        // We are supposed return the class object for this class. Instead
        // of fully implementing it in this DLL, we just call a helper
        // function in the shell DLL which creates a default class factory
        // object for us. When its CreateInstance member is called, it
        // will call back our create instance function.
        hres = SHCreateDefClassObject(
            riid,                   // Interface ID
            ppvOut,                 // Non-null to aggregate
            DllFactoryCallback,     // callback function
            &g_cBusyRef,            // reference count of this DLL
            NULL);                  // init interface
    }
    else
    {
        hres = REGDB_E_CLASSNOTREG;
        *ppvOut = NULL;
    }
    
    return hres;
}




/*----------------------------------------------------------
Purpose: "Can Unload Now" entry point.  Called by the shell DLL
task handler list.
Returns: S_OK to unload
Cond:    --
*/
STDAPI DllCanUnloadNow(void)
{
    HRESULT hr;
    
    // We only unload when:
    //  2) We are not busy processing anything else *and*
    //  3) No briefcases are currently open
    //
    ENTEREXCLUSIVE();
    {
        if (!IsBusySemaphore() &&
            !IsOpenBriefSemaphore())
        {
            DEBUG_CODE( TRACE_MSG(TF_GENERAL, TEXT("DllCanUnloadNow says OK (Busy=%d, Brief=%d)"),
                g_cBusyRef, g_cBriefRef); )
                
            hr = S_OK;
        }
        else
        {
            DEBUG_CODE( TRACE_MSG(TF_GENERAL, TEXT("DllCanUnloadNow says FALSE (Busy=%d, Brief=%d)"),
                g_cBusyRef, g_cBriefRef); )
                
            hr = S_FALSE;
        }
    }
    LEAVEEXCLUSIVE();
    
    return hr;
}
