/*++

Copyright (c) 1998 Microsoft Corporation

Module Name:

    migutil.h

Abstract:

    Includes all header files necessary to use the lib generated by
    common\migutil.  Declares a bunch of utility functions and macros.
    Declares project-wide MAX constants.

Author:

    Jim Schmidt (jimschm) 23-Aug-1996

Revision History:

    Many changes, see SLM log for details.

--*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

//
// MAX constants
//

#define MAX_PATH_PLUS_NUL           (MAX_PATH+1)
#define MAX_MBCHAR_PATH             (MAX_PATH_PLUS_NUL*2)
#define MAX_WCHAR_PATH              MAX_PATH_PLUS_NUL
#define MAX_MBCHAR_PRINTABLE_PATH   (MAX_PATH*2)
#define MAX_WCHAR_PRINTABLE_PATH    MAX_PATH

#define MAX_SERVER_NAMEA            (64*2)
#define MAX_USER_NAMEA              (MAX_PATH)
#define MAX_REGISTRY_KEYA           (1024 * 2)
#define MAX_REGISTRY_VALUE_NAMEA    (260 * 2)
#define MAX_COMPONENT_NAMEA         (256 * 2)
#define MAX_COMPUTER_NAMEA          (64 * 2)
#define MAX_CMDLINEA                (1024 * 2)     // maximum number of chars in a Win95 command line
#define MAX_TRANSLATION             32
#define MAX_KEYBOARDLAYOUT          64
#define MAX_INF_SECTION_NAME        128
#define MAX_INF_KEY_NAME            128

#define MAX_SERVER_NAMEW            64
#define MAX_USER_NAMEW              (MAX_PATH)
#define MAX_REGISTRY_KEYW           1024
#define MAX_REGISTRY_VALUE_NAMEW    260
#define MAX_COMPONENT_NAMEW         256
#define MAX_COMPUTER_NAMEW          64

#define MAX_CMDLINEW	            1024            // maximum number of chars in a Win95 command line

#ifdef UNICODE

#define MAX_SERVER_NAME             MAX_SERVER_NAMEW
#define MAX_USER_NAME               MAX_USER_NAMEW
#define MAX_REGISTRY_KEY            MAX_REGISTRY_KEYW
#define MAX_REGISTRY_VALUE_NAME     MAX_REGISTRY_VALUE_NAMEW
#define MAX_COMPONENT_NAME          MAX_COMPONENT_NAMEW
#define MAX_COMPUTER_NAME           MAX_COMPUTER_NAMEW
#define MAX_CMDLINE	                MAX_CMDLINEW

#define MAX_TCHAR_PATH              MAX_WCHAR_PATH
#define MAX_TCHAR_PRINTABLE_PATH    MAX_WCHAR_PRINTABLE_PATH

#else

#define MAX_SERVER_NAME             MAX_SERVER_NAMEA
#define MAX_USER_NAME               MAX_USER_NAMEA
#define MAX_REGISTRY_KEY            MAX_REGISTRY_KEYA
#define MAX_REGISTRY_VALUE_NAME     MAX_REGISTRY_VALUE_NAMEA
#define MAX_COMPONENT_NAME          MAX_COMPONENT_NAMEA
#define MAX_COMPUTER_NAME           MAX_COMPUTER_NAMEA
#define MAX_CMDLINE	                MAX_CMDLINEA

#define MAX_TCHAR_PATH              MAX_MBCHAR_PATH
#define MAX_TCHAR_PRINTABLE_PATH    MAX_MBCHAR_PRINTABLE_PATH

#endif



//
// Critical Section APIs, implemented because TryEnterCriticalSection is
// supported only on NT, and we need it on Win9x.
//

typedef struct {
    HANDLE EventHandle;
} OUR_CRITICAL_SECTION, *POUR_CRITICAL_SECTION;

BOOL
InitializeOurCriticalSection (
    OUR_CRITICAL_SECTION *pcs
    );

VOID
DeleteOurCriticalSection (
    OUR_CRITICAL_SECTION *pcs
    );

BOOL
EnterOurCriticalSection (
    OUR_CRITICAL_SECTION *pcs
    );

VOID
LeaveOurCriticalSection (
    OUR_CRITICAL_SECTION *pcs
    );

BOOL
TryEnterOurCriticalSection (
    OUR_CRITICAL_SECTION *pcs
    );

PSTR
UnicodeToCcs (
    PCWSTR Source
    );

//
// Fail-proof memory allocators
//

PVOID
SafeHeapAlloc (
    HANDLE g_hHeap,
    DWORD Flags,
    DWORD Size
    );

PVOID
SafeHeapReAlloc (
    HANDLE g_hHeap,
    DWORD Flags,
    PVOID OldBlock,
    DWORD Size
    );

//
// Includes of util modules
//

#include "chartype.h"
#include "debug.h"
#include "log.h"
#include "staticsz.h"
#include "growbuf.h"        // must appear before strings.h and file.h
#include "strings.h"
#include "poolmem.h"
#include "growlist.h"
#include "file.h"
#include "reg.h"
#include "ipc.h"
#include "fileenum.h"
#include "inf.h"
#include "unicode.h"
#include "expandit.h"
#include "hash.h"
#include "uninstall.h"
#include "persist.h"

extern HINSTANCE g_hInst;
extern HANDLE g_hHeap;
extern OSVERSIONINFOA g_OsInfo;


VOID
CenterWindow (
    HWND Wnd,
    HWND Parent     OPTIONAL
    );

VOID
TurnOnWaitCursor (
    VOID
    );

VOID
TurnOffWaitCursor (
    VOID
    );


//
// Version APIs
//

typedef struct {
    WORD CodePage;
    WORD Language;
} TRANSLATION, *PTRANSLATION;

typedef struct {
    GROWBUFFER GrowBuf;
    PBYTE VersionBuffer;
    PTRANSLATION Translations;
    PBYTE StringBuffer;
    UINT Size;
    DWORD Handle;
    VS_FIXEDFILEINFO *FixedInfo;
    UINT FixedInfoSize;
    CHAR TranslationStr[MAX_TRANSLATION];
    UINT MaxTranslations;
    UINT CurrentTranslation;
    UINT CurrentDefaultTranslation;
    PCSTR FileSpec;
    PCSTR VersionField;
} VERSION_STRUCTA, *PVERSION_STRUCTA;

typedef struct {
    GROWBUFFER GrowBuf;
    PBYTE VersionBuffer;
    PTRANSLATION Translations;
    PBYTE StringBuffer;
    UINT Size;
    DWORD Handle;
    VS_FIXEDFILEINFO *FixedInfo;
    UINT FixedInfoSize;
    WCHAR TranslationStr[MAX_TRANSLATION];
    UINT MaxTranslations;
    UINT CurrentTranslation;
    UINT CurrentDefaultTranslation;
    PCWSTR FileSpec;
    PCWSTR VersionField;
} VERSION_STRUCTW, *PVERSION_STRUCTW;

BOOL
CreateVersionStructA (
    OUT     PVERSION_STRUCTA VersionStruct,
    IN      PCSTR FileSpec
    );

VOID
DestroyVersionStructA (
    IN      PVERSION_STRUCTA VersionStruct
    );

PCSTR
EnumFirstVersionTranslationA (
    IN OUT  PVERSION_STRUCTA VersionStruct
    );

PCSTR
EnumNextVersionTranslationA (
    IN OUT  PVERSION_STRUCTA VersionStruct
    );

PCSTR
EnumFirstVersionValueA (
    IN OUT  PVERSION_STRUCTA VersionStruct,
    IN      PCSTR VersionField
    );

PCSTR
EnumNextVersionValueA (
    IN OUT  PVERSION_STRUCTA VersionStruct
    );

BOOL
CreateVersionStructW (
    OUT     PVERSION_STRUCTW VersionStruct,
    IN      PCWSTR FileSpec
    );

VOID
DestroyVersionStructW (
    IN      PVERSION_STRUCTW VersionStruct
    );

PCWSTR
EnumFirstVersionTranslationW (
    IN OUT  PVERSION_STRUCTW VersionStruct
    );

PCWSTR
EnumNextVersionTranslationW (
    IN OUT  PVERSION_STRUCTW VersionStruct
    );

PCWSTR
EnumFirstVersionValueW (
    IN OUT  PVERSION_STRUCTW VersionStruct,
    IN      PCWSTR VersionField
    );

PCWSTR
EnumNextVersionValueW (
    IN OUT  PVERSION_STRUCTW VersionStruct
    );

ULONGLONG
VerGetFileVer (
    IN      PVERSION_STRUCTA VersionStruct
    );

ULONGLONG
VerGetProductVer (
    IN      PVERSION_STRUCTA VersionStruct
    );

DWORD
VerGetFileDateLo (
    IN      PVERSION_STRUCTA VersionStruct
    );

DWORD
VerGetFileDateHi (
    IN      PVERSION_STRUCTA VersionStruct
    );

DWORD
VerGetFileVerOs (
    IN      PVERSION_STRUCTA VersionStruct
    );

DWORD
VerGetFileVerType (
    IN      PVERSION_STRUCTA VersionStruct
    );

//
// Reusable memory alloc, kind of like a GROWBUFFER but more simple
//

PVOID
ReuseAlloc (
    HANDLE Heap,
    PVOID OldPtr,
    DWORD SizeNeeded
    );

VOID
ReuseFree (
    HANDLE Heap,
    PVOID Ptr
    );


VOID
OutOfMemory_Terminate (
    VOID
    );


VOID
SetOutOfMemoryParent (
    HWND hwnd
    );




//
// Error condition tags.
//
// These tags should be used for all error conditions.
//

#define ERROR_CRITICAL
#define ERROR_NONCRITICAL
#define ERROR_TRIVIAL
#define ERROR_ABNORMAL_CONDITION

//
// OSVERSION macros...
//
#define BUILDNUMBER()       (LOWORD(g_OsInfo.dwBuildNumber))
#define ISNT()              (g_OsInfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
#define ISWIN9X()           (g_OsInfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
#define ISWIN95_GOLDEN()    (ISWIN95() && BUILDNUMBER() <= 1000)
#define ISWIN95_OSR2()      (ISWIN95() && BUILDNUMBER() > 1000)
#define ISWIN95()           (ISWIN9X() && !ISATLEASTWIN98())
#define ISMEMPHIS()         (ISWIN9X() && g_OsInfo.dwMajorVersion==4 && g_OsInfo.dwMinorVersion==10)
#define ISMILLENNIUM()      (ISWIN9X() && g_OsInfo.dwMajorVersion==4 && g_OsInfo.dwMinorVersion==90)
#define ISATLEASTOSR2()     (ISWIN9X() && g_OsInfo.dwMajorVersion>=4 && BUILDNUMBER()>1000)
#define ISATLEASTWIN98()    (ISWIN9X() && g_OsInfo.dwMajorVersion>=4 && g_OsInfo.dwMinorVersion>=10)

//
// Platform macros...
//

extern BOOL g_IsPc98;
#define ISPC98()            (g_IsPc98)

//
// Boot drive letter
//

extern PCSTR g_BootDrivePathA;
extern PCWSTR g_BootDrivePathW;
extern PCSTR g_BootDriveA;
extern PCWSTR g_BootDriveW;
extern CHAR g_BootDriveLetterA;
extern WCHAR g_BootDriveLetterW;

//
// String Mapping
//

#define STRMAP_ANY_MATCH                            0
#define STRMAP_COMPLETE_MATCH_ONLY                  0x0001
#define STRMAP_FIRST_CHAR_MUST_MATCH                0x0002
#define STRMAP_RETURN_AFTER_FIRST_REPLACE           0x0004
#define STRMAP_REQUIRE_WACK_OR_NUL                  0x0008

typedef struct {
    BOOL UnicodeData;

    //
    // The filter can replace NewSubString.  (The filter must also
    // set NewSubStringSizeInBytes when replacing NewSubString.)
    //

    union {
        struct {
            PCWSTR OriginalString;
            PCWSTR BeginningOfMatch;
            PCWSTR CurrentString;
            PCWSTR OldSubString;
            PCWSTR NewSubString;
            INT NewSubStringSizeInBytes;
        } Unicode;

        struct {
            PCSTR OriginalString;
            PCSTR BeginningOfMatch;
            PCSTR CurrentString;
            PCSTR OldSubString;
            PCSTR NewSubString;
            INT NewSubStringSizeInBytes;
        } Ansi;
    };
} REG_REPLACE_DATA, *PREG_REPLACE_DATA;

typedef BOOL(REG_REPLACE_FILTER_PROTOTYPE)(PREG_REPLACE_DATA Data);
typedef REG_REPLACE_FILTER_PROTOTYPE * REG_REPLACE_FILTER;

typedef struct TAG_CHARNODE {
    WORD Char;
    WORD Flags;
    PVOID OriginalStr;
    PVOID ReplacementStr;
    INT ReplacementBytes;

    struct TAG_CHARNODE *NextLevel;
    struct TAG_CHARNODE *NextPeer;

} CHARNODE, *PCHARNODE;

typedef struct {
    CHARNODE Node;
    REG_REPLACE_FILTER Filter;
    ULONG_PTR ExtraData;
} CHARNODEEX, *PCHARNODEEX;


typedef struct {
    POOLHANDLE Pool;
    PCHARNODE FirstLevelRoot;
    BOOL UsesExNode;
    BOOL UsesFilter;
    BOOL UsesExtraData;
} MAPSTRUCT, *PMAPSTRUCT;

//
// APIs
//

PMAPSTRUCT
CreateStringMappingEx (
    IN      BOOL UsesFilter,
    IN      BOOL UsesExtraData
    );

#define CreateStringMapping()   CreateStringMappingEx(FALSE,FALSE)

VOID
DestroyStringMapping (
    IN      PMAPSTRUCT Map
    );

VOID
AddStringMappingPairExA (
    IN OUT  PMAPSTRUCT Map,
    IN      PCSTR Old,
    IN      PCSTR New,
    IN      REG_REPLACE_FILTER Filter,      OPTIONAL
    IN      ULONG_PTR ExtraData,            OPTIONAL
    IN      DWORD Flags
    );

#define AddStringMappingPairA(Map,Old,New) AddStringMappingPairExA(Map,Old,New,NULL,0,0)

VOID
AddStringMappingPairExW (
    IN OUT  PMAPSTRUCT Map,
    IN      PCWSTR Old,
    IN      PCWSTR New,
    IN      REG_REPLACE_FILTER Filter,      OPTIONAL
    IN      ULONG_PTR ExtraData,            OPTIONAL
    IN      DWORD Flags
    );

#define AddStringMappingPairW(Map,Old,New) AddStringMappingPairExW(Map,Old,New,NULL,0,0)

BOOL
MappingSearchAndReplaceExA (
    IN      PMAPSTRUCT Map,
    IN      PCSTR SrcBuffer,
    OUT     PSTR Buffer,                    // can be the same as SrcBuffer
    IN      INT InboundBytes,               OPTIONAL
    OUT     PINT OutboundBytesPtr,          OPTIONAL
    IN      INT MaxSizeInBytes,
    IN      DWORD Flags,
    OUT     ULONG_PTR *ExtraDataValue,      OPTIONAL
    OUT     PCSTR *EndOfString              OPTIONAL
    );

#define MappingSearchAndReplaceA(map,buffer,maxbytes)   MappingSearchAndReplaceExA(map,buffer,buffer,0,NULL,maxbytes,0,NULL,NULL)

BOOL
MappingSearchAndReplaceExW (
    IN      PMAPSTRUCT Map,
    IN      PCWSTR SrcBuffer,
    OUT     PWSTR Buffer,                   // can be the same as SrcBuffer
    IN      INT InboundBytes,               OPTIONAL
    OUT     PINT OutboundBytes,             OPTIONAL
    IN      INT MaxSizeInBytes,
    IN      DWORD Flags,
    OUT     ULONG_PTR *ExtraDataValue,      OPTIONAL
    OUT     PCWSTR *EndOfString             OPTIONAL
    );

#define MappingSearchAndReplaceW(map,buffer,maxbytes)   MappingSearchAndReplaceExW(map,buffer,buffer,0,NULL,maxbytes,0,NULL,NULL)

BOOL
MappingMultiTableSearchAndReplaceExA (
    IN      PMAPSTRUCT *MapArray,
    IN      UINT MapArrayCount,
    IN      PCSTR SrcBuffer,
    OUT     PSTR Buffer,                    // can be the same as SrcBuffer
    IN      INT InboundBytes,               OPTIONAL
    OUT     PINT OutboundBytesPtr,          OPTIONAL
    IN      INT MaxSizeInBytes,
    IN      DWORD Flags,
    OUT     ULONG_PTR *ExtraDataValue,      OPTIONAL
    OUT     PCSTR *EndOfString              OPTIONAL
    );

#define MappingMultiTableSearchAndReplaceA(array,count,buffer,maxbytes)   \
        MappingMultiTableSearchAndReplaceExA(array,count,buffer,buffer,0,NULL,maxbytes,0,NULL,NULL)

BOOL
MappingMultiTableSearchAndReplaceExW (
    IN      PMAPSTRUCT *MapArray,
    IN      UINT MapArrayCount,
    IN      PCWSTR SrcBuffer,
    OUT     PWSTR Buffer,                   // can be the same as SrcBuffer
    IN      INT InboundBytes,               OPTIONAL
    OUT     PINT OutboundBytes,             OPTIONAL
    IN      INT MaxSizeInBytes,
    IN      DWORD Flags,
    OUT     ULONG_PTR *ExtraDataValue,      OPTIONAL
    OUT     PCWSTR *EndOfString             OPTIONAL
    );

#define MappingMultiTableSearchAndReplaceW(array,count,buffer,maxbytes)   \
        MappingMultiTableSearchAndReplaceExW(array,count,buffer,buffer,0,NULL,maxbytes,0,NULL,NULL)

HANDLE
StartThread (
    IN      PTHREAD_START_ROUTINE Address,
    IN      PVOID Arg
    );



typedef struct {
    WCHAR DisplayName[80];
    ULONGLONG Checksum;

    WCHAR Version[80];
    WCHAR Publisher[80];
    WCHAR ProductID[80];
    WCHAR RegisteredOwner[80];
    WCHAR RegisteredCompany[80];
    WCHAR Language[80];
    WCHAR SupportUrl[80];
    WCHAR SupportTelephone[80];
    WCHAR HelpLink[80];
    WCHAR InstallLocation[80];
    WCHAR InstallSource[80];
    WCHAR InstallDate[80];
    WCHAR Contact[80];
    WCHAR Comments[80];
    WCHAR Image[80];
    WCHAR ReadmeUrl[80];
    WCHAR UpdateInfoUrl[80];
} INSTALLEDAPPW, *PINSTALLEDAPPW;

PINSTALLEDAPPW
GetInstalledAppsW (
    IN OUT  PGROWBUFFER Buffer,
    OUT     PUINT Count             OPTIONAL
    );

typedef PVOID MOVELISTW;


MOVELISTW
AllocateMoveListW (
    IN      POOLHANDLE PoolHandle
    );

//
// Free move list by destrying the pool it was allocated from
//

BOOL
InsertMoveIntoListW (
    IN      MOVELISTW List,
    IN      PCWSTR Source,
    IN      PCWSTR Destination
    );

MOVELISTW
RemoveMoveListOverlapW (
    IN      MOVELISTW List
    );

BOOL
OutputMoveListW (
    IN      HANDLE File,
    IN      MOVELISTW List,         OPTIONAL
    IN      BOOL AddNestedMoves
    );



#ifdef UNICODE

#define g_BootDrivePath     g_BootDrivePathW
#define g_BootDrive         g_BootDriveW
#define g_BootDriveLetter   g_BootDriveLetterW

#define MOVELIST                        MOVELISTW
#define AllocateMoveList                AllocateMoveListW
#define InsertMoveIntoList              InsertMoveIntoListW
#define RemoveMoveListOverlap           RemoveMoveListOverlapW
#define OutputMoveList                  OutputMoveListW

#define VERSION_STRUCT                  VERSION_STRUCTW
#define PVERSION_STRUCT                 PVERSION_STRUCTW
#define CreateVersionStruct             CreateVersionStructW
#define DestroyVersionStruct            DestroyVersionStructW
#define EnumFirstVersionTranslation     EnumFirstVersionTranslationW
#define EnumNextVersionTranslation      EnumNextVersionTranslationW
#define EnumFirstVersionValue           EnumFirstVersionValueW
#define EnumNextVersionValue            EnumNextVersionValueW

#define PINSTALLEDAPP                   PINSTALLEDAPPW
#define GetInstalledApps                GetInstalledAppsW

#define AddStringMappingPairEx                  AddStringMappingPairExW
#define AddStringMappingPair                    AddStringMappingPairW
#define MappingSearchAndReplaceEx               MappingSearchAndReplaceExW
#define MappingSearchAndReplace                 MappingSearchAndReplaceW
#define MappingMultiTableSearchAndReplaceEx     MappingMultiTableSearchAndReplaceExW
#define MappingMultiTableSearchAndReplace       MappingMultiTableSearchAndReplaceW

#else

#define g_BootDrivePath     g_BootDrivePathA
#define g_BootDrive         g_BootDriveA
#define g_BootDriveLetter   g_BootDriveLetterA

#define VERSION_STRUCT                  VERSION_STRUCTA
#define PVERSION_STRUCT                 PVERSION_STRUCTA
#define CreateVersionStruct             CreateVersionStructA
#define DestroyVersionStruct            DestroyVersionStructA
#define EnumFirstVersionTranslation     EnumFirstVersionTranslationA
#define EnumNextVersionTranslation      EnumNextVersionTranslationA
#define EnumFirstVersionValue           EnumFirstVersionValueA
#define EnumNextVersionValue            EnumNextVersionValueA

#define AddStringMappingPairEx                  AddStringMappingPairExA
#define AddStringMappingPair                    AddStringMappingPairA
#define MappingSearchAndReplaceEx               MappingSearchAndReplaceExA
#define MappingSearchAndReplace                 MappingSearchAndReplaceA
#define MappingMultiTableSearchAndReplaceEx     MappingMultiTableSearchAndReplaceExA
#define MappingMultiTableSearchAndReplace       MappingMultiTableSearchAndReplaceA

#endif

#ifdef __cplusplus
}
#endif
