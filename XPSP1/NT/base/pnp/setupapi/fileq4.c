/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

Module Name:

    fileq4.c

Abstract:

    Setup file queue routines for commit (ie, performing enqueued actions).

Author:

    Ted Miller (tedm) 15-Feb-1995

Revision History:

    Jamie Hunter (jamiehun) 28-Jan-1997

    Added backup queue capabilities
    backup on demand capabilities
    and unwind capabilities

--*/

#include "precomp.h"
#pragma hdrstop


typedef struct _Q_CAB_CB_DATA {

    PSP_FILE_QUEUE     Queue;
    PSOURCE_MEDIA_INFO SourceMedia;

    PSP_FILE_QUEUE_NODE CurrentFirstNode;

    PVOID              MsgHandler;
    PVOID              Context;
    BOOL               IsMsgHandlerNativeCharWidth;
    PSETUP_LOG_CONTEXT LogContext;

} Q_CAB_CB_DATA, *PQ_CAB_CB_DATA;

typedef struct _CERT_PROMPT {
    LPCTSTR lpszDescription;
    LPCTSTR lpszFile;
    SetupapiVerifyProblem ProblemType;
    ULONG DriverSigningPolicy;
} CERT_PROMPT, *PCERT_PROMPT;

typedef struct _DRIVERBLOCK_PROMPT {
    LPCTSTR lpszFile;
    SDBENTRYINFO entryinfo;
} DRIVERBLOCK_PROMPT, *PDRIVERBLOCK_PROMPT;


DWORD
pSetupCommitSingleBackup(
    IN PSP_FILE_QUEUE    Queue,
    IN PCTSTR            FullTargetPath,
    IN LONG              TargetRootPath,
    IN LONG              TargetSubDir,
    IN LONG              TargetFilename,
    IN PVOID             MsgHandler,
    IN PVOID             Context,
    IN BOOL              IsMsgHandlerNativeCharWidth,
    IN BOOL              RenameExisting,
    OUT PBOOL            InUse
    );

DWORD
pCommitCopyQueue(
    IN PSP_FILE_QUEUE Queue,
    IN PVOID          MsgHandler,
    IN PVOID          Context,
    IN BOOL           IsMsgHandlerNativeCharWidth
    );

DWORD
pCommitBackupQueue(
    IN PSP_FILE_QUEUE Queue,
    IN PVOID          MsgHandler,
    IN PVOID          Context,
    IN BOOL           IsMsgHandlerNativeCharWidth
    );

DWORD
pCommitDeleteQueue(
    IN PSP_FILE_QUEUE Queue,
    IN PVOID          MsgHandler,
    IN PVOID          Context,
    IN BOOL           IsMsgHandlerNativeCharWidth
    );

DWORD
pCommitRenameQueue(
    IN PSP_FILE_QUEUE Queue,
    IN PVOID          MsgHandler,
    IN PVOID          Context,
    IN BOOL           IsMsgHandlerNativeCharWidth
    );

UINT
pSetupCabinetQueueCallback(
    IN PVOID Context,
    IN UINT  Notification,
    IN UINT_PTR  Param1,
    IN UINT_PTR  Param2
    );


DWORD
pSetupCopySingleQueuedFile(
    IN  PSP_FILE_QUEUE      Queue,
    IN  PSP_FILE_QUEUE_NODE QueueNode,
    IN  PCTSTR              FullSourceName,
    IN  PVOID               MsgHandler,
    IN  PVOID               Context,
    OUT PTSTR               NewSourcePath,
    IN  BOOL                IsMsgHandlerNativeCharWidth,
    IN  DWORD               CopyStyleFlags
    );

DWORD
pSetupCopySingleQueuedFileCabCase(
    IN  PSP_FILE_QUEUE      Queue,
    IN  PSP_FILE_QUEUE_NODE QueueNode,
    IN  PCTSTR              CabinetName,
    IN  PCTSTR              FullSourceName,
    IN  PVOID               MsgHandler,
    IN  PVOID               Context,
    IN  BOOL                IsMsgHandlerNativeCharWidth
    );

VOID
pSetupSetPathOverrides(
    IN     PVOID StringTable,
    IN OUT PTSTR RootPath,
    IN OUT PTSTR SubPath,
    IN     LONG  RootPathId,
    IN     LONG  SubPathId,
    IN     PTSTR NewPath
    );

VOID
pSetupBuildSourceForCopy(
    IN  PCTSTR              UserRoot,
    IN  PCTSTR              UserPath,
    IN  LONG                MediaRoot,
    IN  PSP_FILE_QUEUE      Queue,
    IN  PSP_FILE_QUEUE_NODE QueueNode,
    OUT PTSTR               FullPath
    );

INT_PTR
CALLBACK
CertifyDlgProc(
    IN HWND hwnd,
    IN UINT msg,
    IN WPARAM wParam,
    IN LPARAM lParam
    );

INT_PTR
CALLBACK
DriverBlockDlgProc(
    IN HWND hwnd,
    IN UINT msg,
    IN WPARAM wParam,
    IN LPARAM lParam
    );

VOID
RestoreBootReplacedFile(
    IN PSP_FILE_QUEUE      Queue,
    IN PSP_FILE_QUEUE_NODE QueueNode
    );

VOID
pSetupExemptFileFromProtection(
    IN  PCTSTR             FileName,
    IN  DWORD              FileChangeFlags,
    IN  PSETUP_LOG_CONTEXT LogContext,      OPTIONAL
    OUT PDWORD             QueueNodeFlags   OPTIONAL
    );

VOID
pSetupUninstallNewCatalogNodes(
    IN PSP_FILE_QUEUE     Queue,
    IN PSETUP_LOG_CONTEXT LogContext OPTIONAL
    );


BOOL
_SetupCommitFileQueue(
    IN HWND     Owner,         OPTIONAL
    IN HSPFILEQ QueueHandle,
    IN PVOID    MsgHandler,
    IN PVOID    Context,
    IN BOOL     IsMsgHandlerNativeCharWidth
    )

/*++

Routine Description:

    Implementation for SetupCommitFileQueue; handles ANSI and Unicode
    callback routines.

Arguments:

    Same as for SetupCommitFileQueue().

    IsMsgHandlerNativeCharWidth - indicates whether the MsgHandler callback
        expects native char width args (or ansi ones, in the unicode build
        of this dll).

Return Value:

    Boolean value indicating outcome.  If FALSE, the GetLastError() indicates
    cause of failure.

--*/

{
    PSP_FILE_QUEUE Queue;
    DWORD rc;
    BOOL Success = TRUE;
    BOOL ChangedThreadLogContext = FALSE;
    PSETUP_LOG_CONTEXT SavedLogContext = NULL;
    PSETUP_LOG_CONTEXT LogContext = NULL;

    //
    // Queue handle is actually a pointer to the queue structure.
    //
    Queue = (PSP_FILE_QUEUE)QueueHandle;

    //
    // do a quick handle validation before anything else
    //
    try {
        Success = ((Queue != NULL) && (Queue != INVALID_HANDLE_VALUE) && (Queue->Signature == SP_FILE_QUEUE_SIG));
        if (Success) {
            LogContext = Queue->LogContext;
        }
    } except (EXCEPTION_EXECUTE_HANDLER) {
        Success = FALSE;
    }
    if (!Success) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    //
    // If there's nothing to do, bail now. This prevents an empty
    // progress dialog from flashing on the screen. Don't return out
    // of the body of the try -- that is bad news performance-wise.
    //
    try {
        Success = (!Queue->DeleteNodeCount && !Queue->RenameNodeCount && !Queue->CopyNodeCount && !Queue->BackupNodeCount);
    } except (EXCEPTION_EXECUTE_HANDLER) {
        SetLastError(ERROR_INVALID_HANDLE);
        return(FALSE);
    }
    if(Success) {

        //
        // We are successful in that we had no file operations to do.  However,
        // we still need to validate the queued catalogs at this time, because
        // we always do validation in the context of file copying.  If we don't
        // do this, we have a hole where a device INF that doesn't copy files
        // (e.g., a modem INF) can circumvent driver signing checking.
        //
        WriteLogEntry(
            LogContext,
            SETUP_LOG_TIME,
            MSG_LOG_BEGIN_VERIFY3_CAT_TIME,
            NULL);       // text message

        rc = _SetupVerifyQueuedCatalogs(Owner,
                                        Queue,
                                        VERCAT_INSTALL_INF_AND_CAT,
                                        NULL,
                                        NULL
                                       );
        WriteLogEntry(
            LogContext,
            SETUP_LOG_TIME,
            MSG_LOG_END_VERIFY3_CAT_TIME,
            NULL);       // text message

        if (rc == NO_ERROR) {

            //
            // If we performed a backup and this is a device install then call
            // the pSetupCompleteBackup API to create the Reinstall instance
            // subkey and do other device rollback cleanup.
            //
            if (Queue->Flags & FQF_DEVICE_BACKUP) {

                pSetupCompleteBackup(Queue);
            }

            Queue->Flags |= FQF_QUEUE_ALREADY_COMMITTED;

        } else {
            //
            // Go uninstall any newly-copied INFs/PNFs/CATs.
            //
            pSetupUninstallNewCatalogNodes(Queue, LogContext);
        }

        SetLastError(rc);
        return(rc == NO_ERROR);
    }

    ASSERT_HEAP_IS_VALID();

    //
    // make a note of default logging context for duration of queue processing
    // this will catch, eg, INF being opened as part of a callback
    //
    MYASSERT(!ChangedThreadLogContext);
    ChangedThreadLogContext = SetThreadLogContext(LogContext,&SavedLogContext);
    if (ChangedThreadLogContext) {
        //
        // add one more ref to protext log context
        //
        RefLogContext(LogContext);
    }

    Success = pSetupCallMsgHandler(
            LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_STARTQUEUE,
            (UINT_PTR)Owner,
            0
            );
    if(!Success) {
        rc = GetLastError();
        if(!rc) {
            rc = ERROR_OPERATION_ABORTED;
        }
        goto final;
    }

    try {
        //
        // Verify catalogs/infs.
        //
        WriteLogEntry(
            LogContext,
            SETUP_LOG_TIME,
            MSG_LOG_BEGIN_VERIFY2_CAT_TIME,
            NULL);       // text message

        rc = _SetupVerifyQueuedCatalogs(Owner,
                                        Queue,
                                        VERCAT_INSTALL_INF_AND_CAT,
                                        NULL,
                                        NULL
                                       );
        WriteLogEntry(
            LogContext,
            SETUP_LOG_TIME,
            MSG_LOG_END_VERIFY2_CAT_TIME,
            NULL);       // text message

        Success = (rc == NO_ERROR);

        if(rc != NO_ERROR) {
            goto Bail;
        }

        ASSERT_HEAP_IS_VALID();

        //
        // Handle backup first
        // don't commit if there's nothing to do
        //

        rc = Queue->BackupNodeCount
           ? pCommitBackupQueue(Queue,MsgHandler,Context,IsMsgHandlerNativeCharWidth)
           : NO_ERROR;

        Success = (rc == NO_ERROR);

        ASSERT_HEAP_IS_VALID();

        if (!Success) {
            goto Bail;
        }

        //
        // Handle deletes
        // now done after backups, but may incorporate a per-delete backup
        // don't commit if there's nothing to do
        //

        rc = Queue->DeleteNodeCount
           ? pCommitDeleteQueue(Queue,MsgHandler,Context,IsMsgHandlerNativeCharWidth)
           : NO_ERROR;

        Success = (rc == NO_ERROR);

        ASSERT_HEAP_IS_VALID();

        if (!Success) {
            goto Bail;
        }

        //
        // Handle renames next.
        // don't commit if there's nothing to do
        //

        rc = Queue->RenameNodeCount
           ? pCommitRenameQueue(Queue,MsgHandler,Context,IsMsgHandlerNativeCharWidth)
           : NO_ERROR;

        Success = (rc == NO_ERROR);

        ASSERT_HEAP_IS_VALID();

        if (!Success) {
            goto Bail;
        }

        //
        // Handle copies last. Don't bother calling the copy commit routine
        // if there are no files to copy.
        //
        rc = Queue->CopyNodeCount
           ? pCommitCopyQueue(Queue,MsgHandler,Context,IsMsgHandlerNativeCharWidth)
           : NO_ERROR;

        Success = (rc == NO_ERROR);

        ASSERT_HEAP_IS_VALID();

        if (!Success) {
            goto Bail;
        }

        rc = DoAllDelayedMoves(Queue);

        Success = (rc == NO_ERROR);

        if(Success) {
            //
            // Set a flag indicating we've committed the file queue (used to keep
            // us from attempting to prune the queue after having committed it).
            //
            Queue->Flags |= FQF_QUEUE_ALREADY_COMMITTED;
        }

        //
        // If we performed a backup and this is a device install then call
        // the pSetupCompleteBackup API to create the Reinstall instance
        // subkey and do other device rollback cleanup.
        //
        if (Queue->Flags & FQF_DEVICE_BACKUP) {

            pSetupCompleteBackup(Queue);
        }

    Bail:
        ;

    } except(EXCEPTION_EXECUTE_HANDLER) {
        Success = FALSE;
        rc = ERROR_INVALID_DATA;
    }

    pSetupCallMsgHandler(
        LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDQUEUE,
        Success,
        0
        );

    pSetupUnwindAll(Queue, Success);

final:

    //
    // If we didn't succeed, then uninstall any new INFs/PNFs/CATs we may have
    // installed.
    //
    if(!Success) {
        pSetupUninstallNewCatalogNodes(Queue, LogContext);
    }

    if (ChangedThreadLogContext) {
        //
        // restore thread log context
        //
        SetThreadLogContext(SavedLogContext,NULL);
        DeleteLogContext(LogContext); // counter RefLogContext
    }

    SetLastError(rc);

    return(Success);
}

#ifdef UNICODE
//
// ANSI version. Also need undecorated (Unicode) version for compatibility
// with apps that were linked before we had A and W versions.
//
BOOL
SetupCommitFileQueueA(
    IN HWND                Owner,         OPTIONAL
    IN HSPFILEQ            QueueHandle,
    IN PSP_FILE_CALLBACK_A MsgHandler,
    IN PVOID               Context
    )
{
    return(_SetupCommitFileQueue(Owner,QueueHandle,MsgHandler,Context,FALSE));
}

#undef SetupCommitFileQueue
SetupCommitFileQueue(
    IN HWND                Owner,         OPTIONAL
    IN HSPFILEQ            QueueHandle,
    IN PSP_FILE_CALLBACK_W MsgHandler,
    IN PVOID               Context
    )
{
    return(_SetupCommitFileQueue(Owner,QueueHandle,MsgHandler,Context,TRUE));
}
#else
//
// Unicode stub. Also need undecorated (ANSI) version for compatibility
// with apps that were linked before we had A and W versions.
//
BOOL
SetupCommitFileQueueW(
    IN HWND                Owner,         OPTIONAL
    IN HSPFILEQ            QueueHandle,
    IN PSP_FILE_CALLBACK_W MsgHandler,
    IN PVOID               Context
    )
{
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(QueueHandle);
    UNREFERENCED_PARAMETER(MsgHandler);
    UNREFERENCED_PARAMETER(Context);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return(FALSE);
}

#undef SetupCommitFileQueue
SetupCommitFileQueue(
    IN HWND                Owner,         OPTIONAL
    IN HSPFILEQ            QueueHandle,
    IN PSP_FILE_CALLBACK_A MsgHandler,
    IN PVOID               Context
    )
{
    return(_SetupCommitFileQueue(Owner,QueueHandle,MsgHandler,Context,TRUE));
}
#endif

BOOL
#ifdef UNICODE
SetupCommitFileQueueW(
#else
SetupCommitFileQueueA(
#endif
    IN HWND              Owner,         OPTIONAL
    IN HSPFILEQ          QueueHandle,
    IN PSP_FILE_CALLBACK MsgHandler,
    IN PVOID             Context
    )

/*++

Routine Description:

    Perform file operations enqueued on a setup file queue.

Arguments:

    OwnerWindow - if specified, supplies the window handle of a window
        that is to be used as the parent of any progress dialogs.

    QueueHandle - supplies a handle to a setup file queue, as returned
        by SetupOpenFileQueue.

    MsgHandler - Supplies a callback routine to be notified
        of various significant events in the queue processing.

    Context - Supplies a value that is passed to the MsgHandler
        callback function.

Return Value:

    Boolean value indicating outcome.

--*/

{
    return(_SetupCommitFileQueue(Owner,QueueHandle,MsgHandler,Context,TRUE));
}


DWORD
pCommitBackupQueue(
    IN PSP_FILE_QUEUE    Queue,
    IN PVOID             MsgHandler,
    IN PVOID             Context,
    IN BOOL              IsMsgHandlerNativeCharWidth
    )
/*++

Routine Description:

    Process the backup Queue
    Backup each file specified in the queue if it exists
    File is marked as backup
    Location of backup is recorded
    Files are not added to unwind queue here
    They get added to unwind queue the first time they are potentially modified

    See also pCommitDeleteQueue, pCommitRenameQueue and pCommitCopyQueue

Arguments:

    Queue - queue that contains the backup sub-queue

    MsgHandler - Supplies a callback routine to be notified
        of various significant events in the queue processing.

    Context - Supplies a value that is passed to the MsgHandler
        callback function.

    IsMsgHandlerNativeCharWidth - For Unicode/Ansi support

Return Value:

    DWORD indicating status or success

--*/
{
    PSP_FILE_QUEUE_NODE QueueNode,queueNode;
    UINT u;
    BOOL b;
    DWORD rc;
    PCTSTR FullTargetPath,FullBackupPath;
    FILEPATHS FilePaths;
    BOOL Skipped = FALSE;
    DWORD BackupFlags = SP_BACKUP_BACKUPPASS;

    MYASSERT(Queue->BackupNodeCount);

    b = pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_STARTSUBQUEUE,
            FILEOP_BACKUP,
            Queue->BackupNodeCount
            );

    if(!b) {
        rc = GetLastError();
        if(!rc) {
            rc = ERROR_OPERATION_ABORTED;
        }
        goto clean0;
    }
    for(QueueNode=Queue->BackupQueue; QueueNode; QueueNode=QueueNode->Next) {

        //
        // Form the full path of the file to be backed up
        //
        FullBackupPath = pSetupFormFullPath(
                            Queue->StringTable,
                            QueueNode->SourceRootPath,
                            QueueNode->SourcePath,
                            QueueNode->SourceFilename
                            );

        if(!FullBackupPath) {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto clean0;
        }

        FullTargetPath = pSetupFormFullPath(
                            Queue->StringTable,
                            QueueNode->TargetDirectory,
                            QueueNode->TargetFilename,
                            -1
                            );

        if(!FullTargetPath) {
            MyFree(FullBackupPath);
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto clean0;
        }

        FilePaths.Source = FullTargetPath; // copying from
        FilePaths.Target = FullBackupPath; // copying to (backup)
        FilePaths.Win32Error = NO_ERROR;
        FilePaths.Flags = BackupFlags;

        Skipped = FALSE;

        //
        // Inform the callback that we are about to start a backup operation.
        //
        u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_STARTBACKUP,
                (UINT_PTR)&FilePaths,
                FILEOP_BACKUP
                );

        if(u == FILEOP_ABORT) {
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            MyFree(FullTargetPath);
            MyFree(FullBackupPath);
            goto clean0;
        }
        if(u == FILEOP_DOIT) {
            //
            // Attempt the backup. If it fails inform the callback,
            // which may decide to abort, retry. or skip the file.
            //
            //SetFileAttributes(FullTargetPath,FILE_ATTRIBUTE_NORMAL);

            do {
                rc = pSetupBackupFile((HSPFILEQ)Queue,
                    FullTargetPath,
                    FullBackupPath,
                    -1, // TargetID not known
                    QueueNode->TargetDirectory, // what to backup
                    -1, // Queue Node's don't maintain this intermediate path
                    QueueNode->TargetFilename,
                    QueueNode->SourceRootPath, // backup as...
                    QueueNode->SourcePath,
                    QueueNode->SourceFilename,
                    &b
                    );
                if (rc == NO_ERROR) {
                    if (b) {
                        // delayed (in use)

                        QueueNode->InternalFlags |= INUSE_IN_USE;
                        //
                        // Tell the callback.
                        //
                        FilePaths.Win32Error = NO_ERROR;
                        FilePaths.Flags = FILEOP_BACKUP;

                        pSetupCallMsgHandler(
                            Queue->LogContext,
                            MsgHandler,
                            IsMsgHandlerNativeCharWidth,
                            Context,
                            SPFILENOTIFY_FILEOPDELAYED,
                            (UINT_PTR)&FilePaths,
                            0
                            );
                    }
                } else {
                    FilePaths.Win32Error = rc;
                    FilePaths.Flags = BackupFlags;

                    u = pSetupCallMsgHandler(
                            Queue->LogContext,
                            MsgHandler,
                            IsMsgHandlerNativeCharWidth,
                            Context,
                            SPFILENOTIFY_BACKUPERROR,
                            (UINT_PTR)&FilePaths,
                            0
                            );

                    if(u == FILEOP_ABORT) {
                        rc = GetLastError();
                        if(!rc) {
                            rc = ERROR_OPERATION_ABORTED;
                        }
                        MyFree(FullTargetPath);
                        MyFree(FullBackupPath);
                        goto clean0;
                    }
                    if(u == FILEOP_SKIP) {
                        // we skipped the backup
                        Skipped = TRUE;
                        break;
                    }
                }
            } while(rc != NO_ERROR);
        } else {
            // we skipped the backup
            Skipped = TRUE;
            rc = NO_ERROR;
        }

        FilePaths.Win32Error = rc;
        FilePaths.Flags = BackupFlags;

        pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_ENDBACKUP,
            (UINT_PTR)&FilePaths,
            0
            );

        MyFree(FullTargetPath);
        MyFree(FullBackupPath);
    }

    pSetupCallMsgHandler(
        Queue->LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDSUBQUEUE,
        FILEOP_BACKUP,
        0
        );

    rc = NO_ERROR;

clean0:

    SetLastError(rc);

    return rc;
}

DWORD
pSetupCommitSingleBackup(
    IN PSP_FILE_QUEUE    Queue,
    IN PCTSTR            FullTargetPath,
    IN LONG              TargetRootPath,
    IN LONG              TargetSubDir,
    IN LONG              TargetFilename,
    IN PVOID             MsgHandler,
    IN PVOID             Context,
    IN BOOL              IsMsgHandlerNativeCharWidth,
    IN BOOL              RenameExisting,
    OUT PBOOL            InUse
    )
/*++

Routine Description:

    Check a single file that is potentially about to be modified

    If the target file doesn't exist, then this routine does nothing
    If the target file hasn't been backed up, back it up
    If the target file has been backed up, but is not on unwind queue,
        add to unwind queue

    The default target location of the backup is used, which is either
    into a backup directory tree, or a temporary backup location
    Location of backup is recorded

Arguments:

    Queue - queue that contains the backup sub-queue
    FullTargetPath - String giving target path, or NULL if not formed
    TargetRootPath - String ID giving RootPath, or -1 if not specified
    TargetSubDir   - String ID giving SubDir (relative to RootPath),
                     or -1 if not specified
    TargetFilename - String ID giving Filename, or -1 if not specified
    MsgHandler - Supplies a callback routine to be notified
        of various significant events in the queue processing.
    Context - Supplies a value that is passed to the MsgHandler
        callback function.
    IsMsgHandlerNativeCharWidth - For Unicode/Ansi support
    RenameExisting - Should existing file be renamed?
    InUse - if specified, set to indicate if file is in use or not
            This should never be the case

Return Value:

    DWORD indicating status or success

--*/
{
    UINT u;
    BOOL b;
    DWORD rc;
    DWORD rc2;
    FILEPATHS FilePaths;
    LONG TargetID;
    PTSTR TargetPathLocal = NULL;
    PSP_UNWIND_NODE UnwindNode = NULL;
    SP_TARGET_ENT TargetInfo;
    BOOL FileOfSameNameExists;
    BOOL DoBackup = TRUE;
    BOOL NeedUnwind = FALSE;
    BOOL Skipped = FALSE;
    WIN32_FILE_ATTRIBUTE_DATA FileAttribData;
    UINT OldMode;
    BOOL DoRename;
    DWORD BackupFlags = SP_BACKUP_DEMANDPASS;

    //
    // used in this function to init time field
    //
    static const FILETIME zeroTime = {
         0,0
    };

    OldMode = SetErrorMode(SEM_FAILCRITICALERRORS); // inhibit unexpected dialog boxes

    MYASSERT(Queue);

    if (FullTargetPath == NULL) {
        TargetPathLocal = pSetupFormFullPath(
                            Queue->StringTable,
                            TargetRootPath,
                            TargetSubDir,
                            TargetFilename);

        if(!TargetPathLocal) {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto clean0;
        }

        FullTargetPath = TargetPathLocal;
    }

    FileOfSameNameExists = GetFileAttributesEx(FullTargetPath, GetFileExInfoStandard, &FileAttribData);

    if (!FileOfSameNameExists) {
        // file doesn't exist, so no need to backup
        rc = NO_ERROR;
        goto clean0;
    }

    rc = pSetupBackupGetTargetByPath((HSPFILEQ)Queue,
                                     NULL, // use Queue's string table
                                     FullTargetPath,
                                     TargetRootPath,
                                     TargetSubDir,
                                     TargetFilename,
                                     &TargetID,
                                     &TargetInfo
                                    );

    if (rc != NO_ERROR) {
        // failed for some strange reason
        goto clean0;

    }

    if (TargetInfo.InternalFlags & SP_TEFLG_INUSE) {
        //
        // was "inuse'd" before
        // we mark as still INUSE
        if (InUse != NULL) {
            *InUse = TRUE;
        }
        //
        // Don't consider this an error, unless we were supposed to rename the
        // existing file.
        //
        rc = RenameExisting ? ERROR_SHARING_VIOLATION : NO_ERROR;
        goto clean0;
    }

    if (TargetInfo.InternalFlags & SP_TEFLG_SKIPPED) {
        //
        // was skipped before
        // we can't rely on it now
        //
        rc = NO_ERROR;
        goto clean0;
    }

    //
    // If we've been asked to backup the existing file, then make sure the
    // SP_TEFLG_RENAMEEXISTING flag is set in the TargetInfo.  Also, figure out
    // if we've already done the rename.
    //
    if(RenameExisting &&
       !(TargetInfo.InternalFlags & SP_TEFLG_RENAMEEXISTING)) {
        //
        // We'd better not think we already renamed this file!
        //
        MYASSERT(!(TargetInfo.InternalFlags & SP_TEFLG_MOVED));

        TargetInfo.InternalFlags |= SP_TEFLG_RENAMEEXISTING;

        //
        // update internal info (this call should never fail)
        //
        pSetupBackupSetTargetByID((HSPFILEQ)Queue,
                                  TargetID,
                                  &TargetInfo
                                 );
    }

    //
    // Figure out whether we've been asked to rename the existing file to a
    // temp name in the same directory, but haven't yet done so.
    //
    DoRename = ((TargetInfo.InternalFlags & (SP_TEFLG_RENAMEEXISTING | SP_TEFLG_MOVED)) == SP_TEFLG_RENAMEEXISTING);

    if(TargetInfo.InternalFlags & SP_TEFLG_SAVED) {
        //
        // already backed up
        //
        DoBackup = FALSE;

        if((TargetInfo.InternalFlags & SP_TEFLG_UNWIND) && !DoRename) {
            //
            // already added to unwind queue, and we don't need to do a rename--
            // don't need to do anything at all
            //
            rc = NO_ERROR;
            goto clean0;
        }
        //
        // we don't need to backup
        // but we still need to add to unwind queue, rename the existing file,
        // or both.
        //
    }

    if(DoBackup) {
        BackupFlags |= SP_BACKUP_DEMANDPASS;
    }
    if(DoRename) {
        BackupFlags |= SP_BACKUP_BOOTFILE | SP_BACKUP_SPECIAL;
    }

    FilePaths.Source = FullTargetPath;  // what we are backing up
    FilePaths.Target = NULL;            // indicates an automatic backup
    FilePaths.Win32Error = NO_ERROR;
    FilePaths.Flags = BackupFlags;

    if (DoRename) {
        pSetupExemptFileFromProtection(
                    FullTargetPath,
                    SFC_ACTION_ADDED | SFC_ACTION_REMOVED | SFC_ACTION_MODIFIED
                    | SFC_ACTION_RENAMED_OLD_NAME |SFC_ACTION_RENAMED_NEW_NAME,
                    Queue->LogContext,
                    NULL
                    );
    }

    if (DoBackup && (Queue->Flags & FQF_BACKUP_AWARE)) {
        //
        // Inform the callback that we are about to start a backup operation.
        //
        u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_STARTBACKUP,
                (UINT_PTR)&FilePaths,
                FILEOP_BACKUP
                );
    } else {
        //
        // no backup, or not backup aware, assume a default
        //
        u = FILEOP_DOIT;
    }

    if(u == FILEOP_ABORT) {
        rc = GetLastError();
        if(!rc) {
            rc = ERROR_OPERATION_ABORTED;
        }
        goto clean0;
    }
    if((u == FILEOP_DOIT) || (BackupFlags & SP_BACKUP_SPECIAL)) {
        //
        // Attempt the backup. If it fails inform the callback,
        // which may decide to abort, retry. or skip the file.
        //
        //SetFileAttributes(FullTargetPath,FILE_ATTRIBUTE_NORMAL);

        //
        // Setup an unwind node, unless we already have one.
        //
        if(!(TargetInfo.InternalFlags & SP_TEFLG_UNWIND)) {

            UnwindNode = MyMalloc(sizeof(SP_UNWIND_NODE));
            if (UnwindNode == NULL) {
                rc = ERROR_NOT_ENOUGH_MEMORY;
                goto clean0;
            }
            UnwindNode->NextNode = Queue->UnwindQueue;
            UnwindNode->TargetID = TargetID;
            if (RetreiveFileSecurity( FullTargetPath, &(UnwindNode->SecurityDesc)) != NO_ERROR) {
                // failed, but not fatal
                UnwindNode->SecurityDesc = NULL;
            }
            if (GetSetFileTimestamp( FullTargetPath, &(UnwindNode->CreateTime),
                                                    &(UnwindNode->AccessTime),
                                                    &(UnwindNode->WriteTime),
                                                    FALSE) != NO_ERROR) {
                // failed, but not fatal
                UnwindNode->CreateTime = zeroTime;
                UnwindNode->AccessTime = zeroTime;
                UnwindNode->WriteTime = zeroTime;
            }
        }

        if (DoBackup || DoRename) {
            do {
                rc = pSetupBackupFile((HSPFILEQ)Queue,
                    FullTargetPath,     // since we know this, pass it
                    NULL,               // automatic destination
                    TargetID,           // we got this earlier
                    TargetRootPath,     // since we know this, pass it
                    TargetSubDir,
                    TargetFilename,
                    -1,                 // use the details from TargetID (or temp)
                    -1,
                    -1,
                    &b                  // in use (should always return FALSE)
                    );
                if (rc == NO_ERROR) {
                    if (InUse != NULL) {
                        *InUse = b;
                    }
                    if (b) {
                        //
                        // if file is in use, callback can decide what to do
                        //
                        if (Queue->Flags & FQF_BACKUP_AWARE) {
                            //
                            // Tell the callback.
                            //
                            FilePaths.Win32Error = ERROR_SHARING_VIOLATION;
                            FilePaths.Flags = BackupFlags;

                            if (Queue->Flags & FQF_BACKUP_AWARE) {
                                u = pSetupCallMsgHandler(
                                    Queue->LogContext,
                                    MsgHandler,
                                    IsMsgHandlerNativeCharWidth,
                                    Context,
                                    SPFILENOTIFY_BACKUPERROR,
                                    (UINT_PTR)&FilePaths,
                                    0
                                    );
                                if(u == FILEOP_ABORT) {
                                    rc = GetLastError();
                                    if(!rc) {
                                        rc = ERROR_OPERATION_ABORTED;
                                    }
                                    goto clean0;
                                }
                            } else {
                                rc = ERROR_OPERATION_ABORTED;
                                goto clean0;
                            }
                        }
                    } else {
                        //
                        // success!!!!!
                        // we would have to unwind this if setup fails
                        //
                        NeedUnwind = TRUE;
                    }
                } else {
                    FilePaths.Win32Error = rc;
                    FilePaths.Flags = BackupFlags;

                    if (Queue->Flags & FQF_BACKUP_AWARE) {
                        //
                        // inform about error
                        //
                        u = pSetupCallMsgHandler(
                                Queue->LogContext,
                                MsgHandler,
                                IsMsgHandlerNativeCharWidth,
                                Context,
                                SPFILENOTIFY_BACKUPERROR,
                                (UINT_PTR)&FilePaths,
                                0
                                );
                        if(u == FILEOP_ABORT) {
                            rc = GetLastError();
                            if(!rc) {
                                rc = ERROR_OPERATION_ABORTED;
                            }
                            goto clean0;
                        }
                    } else {
                        //
                        // if caller is not backup aware, abort
                        //
                        rc = ERROR_OPERATION_ABORTED;
                        goto clean0;
                    }

                    if(u == FILEOP_SKIP) {
                        //
                        // we skipped the backup
                        //
                        Skipped = TRUE;
                        break;
                    }
                }
            } while(rc != NO_ERROR);

        } else {
            //
            // didn't need to backup, only need to add to unwind queue
            //
            NeedUnwind = TRUE;
        }

    } else {
        //
        // we skipped the backup
        //
        Skipped = TRUE;
        rc = NO_ERROR;
    }

    if (DoBackup) {

        FilePaths.Win32Error = rc;

        if (Queue->Flags & FQF_BACKUP_AWARE) {
            //
            // report result only if backup aware
            //
            pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_ENDBACKUP,
                (UINT_PTR)&FilePaths,
                0
                );
        }
    }

    if (Skipped) {
        //
        // once we return, file may get overwritten or deleted
        // we have to save the fact it has been skipped once
        // so we always skip this file
        //
        if (pSetupBackupGetTargetByID((HSPFILEQ)Queue, TargetID, &TargetInfo) == NO_ERROR) {
            //
            // flag the file should always be skipped
            //
            TargetInfo.InternalFlags|=SP_TEFLG_SKIPPED;
            pSetupBackupSetTargetByID((HSPFILEQ)Queue, TargetID, &TargetInfo);
        }
    }
    else if (NeedUnwind) {
        //
        // We only want to add this to unwind queue
        //
        if (pSetupBackupGetTargetByID((HSPFILEQ)Queue, TargetID, &TargetInfo) == NO_ERROR) {
            if ((TargetInfo.InternalFlags&SP_TEFLG_UNWIND)==FALSE) {
                //
                // node needs to be added to unwind queue
                // we only ever do this once
                //
                Queue->UnwindQueue = UnwindNode;
                //
                // set to NULL so we don't clean it up later
                //
                UnwindNode = NULL;

                //
                // flag that we've added it to unwind queue
                // so we don't try and do it again later
                //
                TargetInfo.InternalFlags|=SP_TEFLG_UNWIND;

                pSetupBackupSetTargetByID((HSPFILEQ)Queue, TargetID, &TargetInfo);
            }

        }
    }


    rc = NO_ERROR;

clean0:

    if (UnwindNode != NULL) {
        //
        // we allocated, but didn't use this structure
        //
        if (UnwindNode->SecurityDesc != NULL) {
            MyFree(UnwindNode->SecurityDesc);
        }
        MyFree(UnwindNode);
    }
    if (TargetPathLocal != NULL) {
        MyFree(TargetPathLocal);
    }

    SetErrorMode(OldMode);

    SetLastError(rc);

    return rc;
}

DWORD
pCommitDeleteQueue(
    IN PSP_FILE_QUEUE    Queue,
    IN PVOID             MsgHandler,
    IN PVOID             Context,
    IN BOOL              IsMsgHandlerNativeCharWidth
    )
/*++

Routine Description:

    Process the delete Queue
    Delete each file specified in the queue
    Files are backed up before they are deleted (if not already backed up)

    See also pCommitBackupQueue, pCommitRenameQueue and pCommitCopyQueue

Arguments:

    Queue - queue that contains the delete sub-queue

    MsgHandler - Supplies a callback routine to be notified
        of various significant events in the queue processing.

    Context - Supplies a value that is passed to the MsgHandler
        callback function.

    IsMsgHandlerNativeCharWidth - For Unicode/Ansi support

Return Value:

    DWORD indicating status or success

--*/
{
    PSP_FILE_QUEUE_NODE QueueNode,queueNode;
    UINT u;
    BOOL b;
    DWORD rc;
    PCTSTR FullTargetPath;
    FILEPATHS FilePaths;
    BOOL BackupInUse = FALSE;
    BOOL TargetIsProtected;

    MYASSERT(Queue->DeleteNodeCount);

    b = pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_STARTSUBQUEUE,
            FILEOP_DELETE,
            Queue->DeleteNodeCount
            );

    if(!b) {
        rc = GetLastError();
        if(!rc) {
            rc = ERROR_OPERATION_ABORTED;
        }
        goto clean0;
    }

    for(QueueNode=Queue->DeleteQueue; QueueNode; QueueNode=QueueNode->Next) {

        //
        // Form the full path of the file to be deleted.
        //
        FullTargetPath = pSetupFormFullPath(
                            Queue->StringTable,
                            QueueNode->TargetDirectory,
                            QueueNode->TargetFilename,
                            -1
                            );

        if(!FullTargetPath) {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto clean0;
        }

        //
        // Backup the file we're about to delete
        //
        if((rc=pSetupDoLastKnownGoodBackup(Queue,
                                           FullTargetPath,
                                           LASTGOOD_OPERATION_DELETE,
                                           NULL)) != NO_ERROR) {
            MyFree(FullTargetPath);
            goto clean0;
        }
        rc = pSetupCommitSingleBackup(Queue,
                                      FullTargetPath,
                                      QueueNode->TargetDirectory,
                                      -1,
                                      QueueNode->TargetFilename,
                                      MsgHandler,
                                      Context,
                                      IsMsgHandlerNativeCharWidth,
                                      FALSE,
                                      &BackupInUse
                                     );
        if (rc != NO_ERROR) {
            MyFree(FullTargetPath);
            goto clean0;
        }

        FilePaths.Source = NULL;
        FilePaths.Target = FullTargetPath;
        FilePaths.Win32Error = NO_ERROR;
        FilePaths.Flags = 0;

        //
        // Inform the callback that we are about to start a delete operation.
        //
        u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_STARTDELETE,
                (UINT_PTR)&FilePaths,
                FILEOP_DELETE
                );

        if(u == FILEOP_ABORT) {
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            MyFree(FullTargetPath);
            goto clean0;
        }
        if(u == FILEOP_DOIT) {
            //
            // Attempt the delete. If it fails inform the callback,
            // which may decide to abort, retry. or skip the file.
            //
            SetFileAttributes(FullTargetPath,FILE_ATTRIBUTE_NORMAL);

            do {
                if (BackupInUse) {
                    rc = ERROR_SHARING_VIOLATION;
                } else {
                    rc = DeleteFile(FullTargetPath) ? NO_ERROR : GetLastError();
                }
                if((rc == ERROR_ACCESS_DENIED)
                || (rc == ERROR_SHARING_VIOLATION)
                || (rc == ERROR_USER_MAPPED_FILE)) {
                    //
                    // The file is probably in use.
                    //
                    if(QueueNode->InternalFlags & IQF_DELAYED_DELETE_OK) {
                        //
                        // Inf wanted delete on next reboot.  Check to see if
                        // we're being asked to delete a protected system file.
                        // If so (and all the catalog nodes associated with the
                        // queue were OK), then we'll allow this to happen.
                        // Otherwise, we'll silently skip the deletion (and log
                        // it).
                        //
                        MYASSERT((Queue->Flags & FQF_DID_CATALOGS_OK) ||
                                 (Queue->Flags & FQF_DID_CATALOGS_FAILED));

                        if(Queue->Flags & FQF_DID_CATALOGS_OK) {

                            QueueNode->InternalFlags |= INUSE_IN_USE;

                            TargetIsProtected = IsFileProtected(FullTargetPath,
                                                                Queue->LogContext,
                                                                NULL
                                                               );

                            if(b = PostDelayedMove(Queue,
                                                   FullTargetPath,
                                                   NULL,
                                                   -1,
                                                   TargetIsProtected)) {
                                //
                                // Tell the callback.
                                //
                                FilePaths.Source = NULL;
                                FilePaths.Target = FullTargetPath;
                                FilePaths.Win32Error = NO_ERROR;
                                FilePaths.Flags = FILEOP_DELETE;

                                pSetupCallMsgHandler(
                                    Queue->LogContext,
                                    MsgHandler,
                                    IsMsgHandlerNativeCharWidth,
                                    Context,
                                    SPFILENOTIFY_FILEOPDELAYED,
                                    (UINT_PTR)&FilePaths,
                                    0
                                    );
                            }
                        } else {
                            //
                            // We're installing an unsigned package.  Skip the
                            // delayed delete operation, and generate a log
                            // entry about this.
                            //
                            WriteLogEntry(Queue->LogContext,
                                          SETUP_LOG_ERROR,
                                          MSG_LOG_DELAYED_DELETE_SKIPPED_FOR_SFC,
                                          NULL,
                                          FullTargetPath
                                         );
                        }

                    } else {
                        //
                        // Just skip this file.
                        //
                        b = TRUE;
                    }

                    rc = b ? NO_ERROR : GetLastError();

                    if(rc) {
                        WriteLogEntry(
                            Queue->LogContext,
                            SETUP_LOG_ERROR | SETUP_LOG_BUFFER,
                            MSG_LOG_DELAYDELETE_FILE_ERROR,
                            NULL,
                            FullTargetPath);
                        WriteLogError(Queue->LogContext,SETUP_LOG_ERROR,rc);
                    } else {
                        WriteLogEntry(
                            Queue->LogContext,
                            SETUP_LOG_INFO,
                            MSG_LOG_DELAYDELETED_FILE,
                            NULL,
                            FullTargetPath);
                    }

                } else if(rc) {
                    WriteLogEntry(
                        Queue->LogContext,
                        DEL_ERR_LOG_LEVEL(rc) | SETUP_LOG_BUFFER,
                        MSG_LOG_DELETE_FILE_ERROR,
                        NULL,
                        FullTargetPath);
                    WriteLogError(Queue->LogContext,DEL_ERR_LOG_LEVEL(rc),rc);
                } else {
                    WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_INFO,
                        MSG_LOG_DELETED_FILE,
                        NULL,
                        FullTargetPath);
                }

#ifdef UNICODE
                if( rc == NO_ERROR )
                {
                    rc = pSetupCallSCE(
                            ST_SCE_DELETE,
                            FullTargetPath,
                            NULL,
                            NULL,
                            -1,
                            NULL
                            );
                    SetLastError( rc );
                }
#endif

                if(rc != NO_ERROR) {
                    FilePaths.Win32Error = rc;

                    u = pSetupCallMsgHandler(
                            Queue->LogContext,
                            MsgHandler,
                            IsMsgHandlerNativeCharWidth,
                            Context,
                            SPFILENOTIFY_DELETEERROR,
                            (UINT_PTR)&FilePaths,
                            0
                            );

                    if(u == FILEOP_ABORT) {
                        rc = GetLastError();
                        if(!rc) {
                            rc = ERROR_OPERATION_ABORTED;
                        }
                        MyFree(FullTargetPath);
                        goto clean0;
                    }
                    if(u == FILEOP_SKIP) {
                        break;
                    }
                }
            } while(rc != NO_ERROR);
        } else {
            rc = NO_ERROR;
        }

        FilePaths.Win32Error = rc;

        pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_ENDDELETE,
            (UINT_PTR)&FilePaths,
            0
            );

        MyFree(FullTargetPath);
    }

    pSetupCallMsgHandler(
        Queue->LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDSUBQUEUE,
        FILEOP_DELETE,
        0
        );

    rc = NO_ERROR;

clean0:
    SetLastError(rc);
    return rc;
}

DWORD
pCommitRenameQueue(
    IN PSP_FILE_QUEUE    Queue,
    IN PVOID             MsgHandler,
    IN PVOID             Context,
    IN BOOL              IsMsgHandlerNativeCharWidth
    )
/*++

Routine Description:

    Process the rename Queue
    Rename each file specified in the queue
    Files are backed up before they are renamed (if not already backed up)
    If the target exists, it is also backed up (if not already backed up)

    Performance: this can get optimized by treating the newly named files
                as a backup

    See also pCommitBackupQueue, pCommitDeleteQueue and pCommitCopyQueue

Arguments:

    Queue - queue that contains the rename sub-queue

    MsgHandler - Supplies a callback routine to be notified
        of various significant events in the queue processing.

    Context - Supplies a value that is passed to the MsgHandler
        callback function.

    IsMsgHandlerNativeCharWidth - For Unicode/Ansi support

Return Value:

    DWORD indicating status or success

--*/
{
    PSP_FILE_QUEUE_NODE QueueNode,queueNode;
    UINT u;
    BOOL b;
    DWORD rc;
    PCTSTR FullTargetPath;
    PCTSTR FullSourcePath;
    FILEPATHS FilePaths;
    BOOL BackupInUse = FALSE;
    BOOL TargetIsProtected;

    MYASSERT(Queue->RenameNodeCount);

    b = pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_STARTSUBQUEUE,
            FILEOP_RENAME,
            Queue->RenameNodeCount
            );

    if(!b) {
        rc = GetLastError();
        if(!rc) {
            rc = ERROR_OPERATION_ABORTED;
        }
        goto clean0;
    }
    for(QueueNode=Queue->RenameQueue; QueueNode; QueueNode=QueueNode->Next) {

        //
        // Form the full source path of the file to be renamed.
        //
        FullSourcePath = pSetupFormFullPath(
                            Queue->StringTable,
                            QueueNode->SourcePath,
                            QueueNode->SourceFilename,
                            -1
                            );

        if(!FullSourcePath) {
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto clean0;
        }

        //
        // Form the full target path of the file to be renamed.
        //
        FullTargetPath = pSetupFormFullPath(
                            Queue->StringTable,
                            QueueNode->TargetDirectory == -1 ? QueueNode->SourcePath : QueueNode->TargetDirectory,
                            QueueNode->TargetFilename,
                            -1
                            );

        if(!FullTargetPath) {
            MyFree(FullSourcePath);
            rc = ERROR_NOT_ENOUGH_MEMORY;
            goto clean0;
        }

        //
        // Backup the file we may be overwriting
        //
        if((rc=pSetupDoLastKnownGoodBackup(Queue,
                                           FullTargetPath,
                                           0,
                                           NULL)) != NO_ERROR) {
            MyFree(FullSourcePath);
            MyFree(FullTargetPath);
            goto clean0;
        }
        rc = pSetupCommitSingleBackup(Queue,
                                      FullTargetPath,
                                      QueueNode->TargetDirectory == -1 ? QueueNode->SourcePath : QueueNode->TargetDirectory,
                                      -1, // we don't use this
                                      QueueNode->TargetFilename,
                                      MsgHandler,
                                      Context,
                                      IsMsgHandlerNativeCharWidth,
                                      FALSE,
                                      &BackupInUse
                                     );
        if (rc != NO_ERROR) {
            MyFree(FullSourcePath);
            MyFree(FullTargetPath);
            goto clean0;
        }

        //
        // Backup the file we're about to rename
        //

        if((rc=pSetupDoLastKnownGoodBackup(Queue,
                                           FullSourcePath,
                                           LASTGOOD_OPERATION_DELETE,
                                           NULL)) != NO_ERROR) {
            MyFree(FullSourcePath);
            MyFree(FullTargetPath);
            goto clean0;
        }
        rc = pSetupCommitSingleBackup(Queue,
                                      FullSourcePath,
                                      QueueNode->SourcePath,
                                      -1, // we don't use this????
                                      QueueNode->SourceFilename,
                                      MsgHandler,
                                      Context,
                                      IsMsgHandlerNativeCharWidth,
                                      FALSE,
                                      &b
                                     );
        if (rc != NO_ERROR) {
            MyFree(FullSourcePath);
            MyFree(FullTargetPath);
            goto clean0;
        }
        if (b) {
            //
            // BackupInUse is the "OR" of the two backup In-Use flags
            //
            BackupInUse = TRUE;
        }

        FilePaths.Source = FullSourcePath;
        FilePaths.Target = FullTargetPath;
        FilePaths.Win32Error = NO_ERROR;

        //
        // Inform the callback that we are about to start a rename operation.
        //
        u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_STARTRENAME,
                (UINT_PTR)&FilePaths,
                FILEOP_RENAME
                );

        if(u == FILEOP_ABORT) {
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            MyFree(FullSourcePath);
            MyFree(FullTargetPath);
            goto clean0;
        }
        if(u == FILEOP_DOIT) {
            //
            // Attempt the rename. If it fails inform the callback,
            // which may decide to abort, retry. or skip the file.
            //
            do {
                if (BackupInUse) {
                    //
                    // backup is in use, must delay op.  Check to see if either
                    // the source or target files are protected system files.
                    // If so (and all the catalog nodes associated with the
                    // queue were OK), then we'll allos this to happen.
                    // Otherwise, we'll silently fail the rename (and log it).
                    //
                    MYASSERT((Queue->Flags & FQF_DID_CATALOGS_OK) ||
                             (Queue->Flags & FQF_DID_CATALOGS_FAILED));

                    if(Queue->Flags & FQF_DID_CATALOGS_OK) {

                        TargetIsProtected = IsFileProtected(FullSourcePath,
                                                            Queue->LogContext,
                                                            NULL
                                                           );
                        if(!TargetIsProtected) {
                            TargetIsProtected = IsFileProtected(FullTargetPath,
                                                                Queue->LogContext,
                                                                NULL
                                                               );
                        }

                        if(b = PostDelayedMove(Queue,
                                               FullSourcePath,
                                               FullTargetPath,
                                               -1,
                                               TargetIsProtected)) {
                            rc = NO_ERROR;
                        }
                        else
                        {
                            rc = GetLastError();
                        }
                        if(rc) {
                            WriteLogEntry(
                                Queue->LogContext,
                                DEL_ERR_LOG_LEVEL(rc) | SETUP_LOG_BUFFER,
                                MSG_LOG_DELAYRENAME_FILE_ERROR,
                                NULL,
                                FullSourcePath,
                                FullTargetPath);
                            WriteLogError(Queue->LogContext,DEL_ERR_LOG_LEVEL(rc),rc);
                        } else {
                            WriteLogEntry(
                                Queue->LogContext,
                                SETUP_LOG_INFO,
                                MSG_LOG_DELAYRENAMED_FILE,
                                NULL,
                                FullSourcePath,
                                FullTargetPath);
                        }

                    } else {
                        //
                        // We're installing an unsigned package.  Skip the
                        // delayed rename operation, and generate a log
                        // entry about this.
                        //
                        WriteLogEntry(Queue->LogContext,
                                      SETUP_LOG_ERROR,
                                      MSG_LOG_DELAYED_MOVE_SKIPPED_FOR_SFC,
                                      NULL,
                                      FullTargetPath
                                     );
                        //
                        // act as if no error occurred.
                        //
                        rc = NO_ERROR;
                    }

                } else {
                    rc = MoveFile(FullSourcePath,FullTargetPath) ? NO_ERROR : GetLastError();
                    if(rc) {
                        WriteLogEntry(
                            Queue->LogContext,
                            DEL_ERR_LOG_LEVEL(rc) | SETUP_LOG_BUFFER,
                            MSG_LOG_RENAME_FILE_ERROR,
                            NULL,
                            FullSourcePath,
                            FullTargetPath);
                        WriteLogError(Queue->LogContext,DEL_ERR_LOG_LEVEL(rc),rc);
                    } else {
                        WriteLogEntry(
                            Queue->LogContext,
                            SETUP_LOG_INFO,
                            MSG_LOG_RENAMED_FILE,
                            NULL,
                            FullSourcePath,
                            FullTargetPath);
                    }
                }

#ifdef UNICODE

                if( rc == NO_ERROR )
                {
                    rc = pSetupCallSCE(
                            ST_SCE_RENAME,
                            FullSourcePath,
                            NULL,
                            FullTargetPath,
                            -1,
                            NULL
                            );
                    SetLastError( rc );
                }

#endif

                if((rc == ERROR_FILE_NOT_FOUND) || (rc == ERROR_PATH_NOT_FOUND)) {
                    rc = NO_ERROR;
                }

                if(rc != NO_ERROR) {
                    FilePaths.Win32Error = rc;

                    u = pSetupCallMsgHandler(
                            Queue->LogContext,
                            MsgHandler,
                            IsMsgHandlerNativeCharWidth,
                            Context,
                            SPFILENOTIFY_RENAMEERROR,
                            (UINT_PTR)&FilePaths,
                            0
                            );

                    if(u == FILEOP_ABORT) {
                        rc = GetLastError();
                        if(!rc) {
                            rc = ERROR_OPERATION_ABORTED;
                        }
                        MyFree(FullSourcePath);
                        MyFree(FullTargetPath);
                        goto clean0;
                    }
                    if(u == FILEOP_SKIP) {
                        break;
                    }
                }
            } while(rc != NO_ERROR);
        } else {
            rc = NO_ERROR;
        }

        FilePaths.Win32Error = rc;

        pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_ENDRENAME,
            (UINT_PTR)&FilePaths,
            0
            );

        MyFree(FullSourcePath);
        MyFree(FullTargetPath);
    }

    pSetupCallMsgHandler(
        Queue->LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDSUBQUEUE,
        FILEOP_RENAME,
        0
        );

    rc = NO_ERROR;

clean0:
    SetLastError(rc);
    return rc;
}

DWORD
pCommitCopyQueue(
    IN PSP_FILE_QUEUE    Queue,
    IN PVOID             MsgHandler,
    IN PVOID             Context,
    IN BOOL              IsMsgHandlerNativeCharWidth
    )
/*++

Routine Description:

    Process the copy sub-Queues
    Copy each file specified in the sub-queues
    Files are backed up before they are overwritten (if not already backed up)
    See also pCommitBackupQueue, pCommitDeleteQueue and pCommitRenameQueue

Arguments:

    Queue - queue that contains the copy sub-queues

    MsgHandler - Supplies a callback routine to be notified
        of various significant events in the queue processing.

    Context - Supplies a value that is passed to the MsgHandler
        callback function.

    IsMsgHandlerNativeCharWidth - For Unicode/Ansi support

Return Value:

    DWORD indicating status or success

--*/
{
    PSOURCE_MEDIA_INFO SourceMediaInfo;
    SOURCE_MEDIA SourceMedia;
    PTCHAR p, temp;
    UINT SourcePathLen;
    UINT u;
    DWORD rc;
    Q_CAB_CB_DATA QData;
    BOOL b;
    BOOL FirstIteration;
    PSP_FILE_QUEUE_NODE QueueNode,queueNode;
    TCHAR UserSourceRoot[MAX_PATH];
    TCHAR UserSourcePath[MAX_PATH];
    TCHAR FullSourcePath[MAX_PATH];
    TCHAR UserOverride[MAX_PATH];
    LPCTSTR RestorePath = NULL;
    UINT    DriveType;
    BOOL    IsRemovable, AnyProcessed, AnyNotProcessed, SkipMedia;
    BOOL  SpecialMedia = FALSE;
    BOOL  LocateCab;
    PCTSTR MediaRoot;
    DWORD MediaLogTag;
    LONG Cabfile;
    LONG Tagfile;

    //
    // The caller is supposed to skip calling us if there are no files
    // to be copied.
    //
    MYASSERT(Queue->CopyNodeCount);

    //
    // Inform the callback that we are starting.
    //
    b = pSetupCallMsgHandler(
            Queue->LogContext,
            MsgHandler,
            IsMsgHandlerNativeCharWidth,
            Context,
            SPFILENOTIFY_STARTSUBQUEUE,
            FILEOP_COPY,
            Queue->CopyNodeCount
            );

    if(!b) {
        rc = GetLastError();
        if(!rc) {
            rc = ERROR_OPERATION_ABORTED;
        }
       return(rc);
    }

    if(Queue->RestorePathID != -1) {
        RestorePath = pSetupStringTableStringFromId(Queue->StringTable, Queue->RestorePathID);
        DiskPromptGetDriveType(RestorePath, &DriveType, &IsRemovable);
        if(IsRemovable) {
            //
            // do not allow restore from removable media
            //
            RestorePath = NULL;
        }
    }

    //
    // Initially, no user-specified override path exists.
    //
    UserSourceRoot[0] = TEXT('\0');
    UserSourcePath[0] = TEXT('\0');

    //
    // The outermost loop iterates through all the source media descriptors.
    //
    for(SourceMediaInfo=Queue->SourceMediaList; SourceMediaInfo; SourceMediaInfo=SourceMediaInfo->Next) {

        //
        // If there are no files on this particular media, skip it.
        // Otherwise get pointer to queue node for first file on this media.
        //
        if(!SourceMediaInfo->CopyNodeCount) {
            continue;
        }
        MYASSERT(SourceMediaInfo->CopyQueue);

        //
        // if last media was special media (see long discussion above),
        // then forget about any user override
        //
        if (SpecialMedia) {
            UserSourceRoot[0] = TEXT('\0');
            UserSourcePath[0] = TEXT('\0');
            SpecialMedia = FALSE;
        }

        //
        // see if this media is special media
        //
        if (SourceMediaInfo->Flags & ( SMI_FLAG_USE_SVCPACK_SOURCE_ROOT_PATH |
                                       SMI_FLAG_USE_LOCAL_SOURCE_CAB ) ) {
            SpecialMedia = TRUE;
        }

        //
        // If we're in restore-mode
        // we've been given a directory to restore from
        // ignore the media root, and use restore-point root
        // restore as many files as we can
        //
        // note, we check for file presence via FileExists
        // rather than trying to determine file name
        // since we'll always backup in uncompressed form
        // with same name as listed in [SourceDisksNames]
        //

        if(RestorePath) {
            //
            // Restore Symantics - prior to prompting for media, see
            // if we can restore backup
            //
            QueueNode = NULL;
            for(queueNode = SourceMediaInfo->CopyQueue;
                queueNode;
                queueNode=queueNode->Next) {

                pSetupBuildSourceForCopy(
                    RestorePath,
                    NULL,
                    SourceMediaInfo->SourceRootPath,
                    Queue,
                    queueNode,
                    FullSourcePath
                    );

                //
                // don't allow alternate sourcenames in this case
                //
                if(FileExists(FullSourcePath,NULL)) {
                    //
                    // backup exists, copy it
                    //
                    rc = pSetupCopySingleQueuedFile(
                            Queue,
                            queueNode,
                            FullSourcePath,
                            MsgHandler,
                            Context,
                            UserOverride,
                            IsMsgHandlerNativeCharWidth,
                            SP_COPY_ALREADYDECOMP // backup already decomp'd.
                            );
                    if(rc == NO_ERROR) {
                        //
                        // we restored this file through backup
                        // carry on to next file
                        //
                        queueNode->InternalFlags |= IQF_PROCESSED;
                        continue;
                    }
                    //
                    // we know backup existed so if this failed
                    // consider it major enough to abort restore
                    // (eg, file unsigned, user specified abort)
                    //
                    SetLastError(rc);
                    return(rc);
                }
                if(!QueueNode) {
                    //
                    // first problematic file
                    //
                    QueueNode = queueNode;
                }
            }
            if(!QueueNode) {
                //
                // we copied all files of this media from backup
                // carry on to next media
                //
                continue;
            }
        } else {
            //
            // not restoring, start at first file
            //
            QueueNode = SourceMediaInfo->CopyQueue;
        }


        //
        // We will need to prompt for media, which requires some preparation.
        // We need to get the first file in the queue for this media, because
        // its path is where we will expect to find it or its cabinet or tag
        // file.  If there is no tag file, then we will look for the file
        // itself.
        //

        FirstIteration = TRUE;
        SkipMedia = FALSE;
        LocateCab = FALSE;
        Tagfile = SourceMediaInfo->Tagfile;
        Cabfile = SourceMediaInfo->Cabfile;

RepromptMedia:
        //
        // The case where we have non-removeable media and the path was
        // previously overridden must be handled specially.  For example, we
        // could have files queued on the same source root but different
        // subdirs.  If the user changes the network location, for example,
        // we have to be careful or we'll ignore the change in subdirectories
        // as we move among the media.
        //
        // To work around this, we check on non-removable media to see if the
        // queue node we're presently working with is in a subdirectory.  If it
        // is, then we reset our UserSourcePath string.
        //
        // (andrewr)...I don't get this comment above.  The current code
        // iterates through each source media info structure, which doesn't include
        // subdirectory information, only source root path information.  If it
        // does, then the caller is doing something really wierd, since they
        // should be using the SourcePath to define subdirectories from one master
        // root.
        //
        // It appears that the reasoning behind the code below is as follows:
        //
        // The assumption is that if we have removable media and multiple source
        // paths, then we will have to swap media out of the drive.  We don't
        // override source root paths if we are dealing with removable media.
        // If the source root path is non removable, then all of the source media
        // is "tied together."  If the user overrides the source root path, then
        // we override subsequent fixed media source root paths.
        //
        // In the case of dealing with service pack source media or a local cab-file
        // drivers cache, the source media info for a queue will not be tied together,
        // even though we're dealing with fixed media.
        //
        // To reconcile the comments above and the reasoning it uses with the
        // contradiction that svc pack media imposes, we have 2 options:
        //
        // 1.  If we encounter flags that indicate one of our special cases, then don't
        //     use any user override for the new source media.  (or, put another way,
        //     if we know that the last media was actually one of these special media,
        //     then don't allow an override of the normal media.
        //
        // 2.  Introduce some sort of hueristic that determines if the prior source media
        //     and the current source media are similar.  If they are, then go ahead and
        //     use any user specified override, otherwise use the proper path.
        //
        //
        // For simplicities sake, I use approach 1 above.  This is made a little simpler
        // by following the following rule.  When adding source media to the media list,
        // insert special media (ie, has flags identifying the media as svc pack media)
        // at the head of the list, insert normal media after that.  By following this
        // approach we know that we can just "zero out" the user overrides for the special
        // media and we'll just do the right thing for the regular media.
        //
        // In the case where there is an explicit cab-file to use
        // then we ask the user to point to cab-file instead of source file (first iteration)
        //

        MediaRoot = *UserSourceRoot
                  ? UserSourceRoot
                  : pSetupStringTableStringFromId(Queue->StringTable, SourceMediaInfo->SourceRootPath);

        DiskPromptGetDriveType(MediaRoot, &DriveType, &IsRemovable);
        if(!IsRemovable && (QueueNode->SourcePath != -1)) {
            *UserSourcePath = TEXT('\0');
        }

        pSetupBuildSourceForCopy(
            UserSourceRoot,
            UserSourcePath,
            SourceMediaInfo->SourceRootPath,
            Queue,
            QueueNode,
            FullSourcePath
            );

        if (FirstIteration
            && (Tagfile != Cabfile)
            && (Cabfile != -1)) {

            MYASSERT(!SkipMedia);
            MYASSERT(!(SourceMediaInfo->Flags & SMI_FLAG_USE_LOCAL_SOURCE_CAB));

            //
            // build location of cab file
            //
            temp = _tcsrchr(FullSourcePath,TEXT('\\'));
            MYASSERT( temp );
            if(temp) {
                *(temp+1) = 0;
            } else {
                FullSourcePath[0] = 0;
            }


            //
            // obtain path of (potential) cab file
            //
            pSetupConcatenatePaths( FullSourcePath, pSetupStringTableStringFromId(Queue->StringTable,Cabfile), MAX_PATH, NULL );
            LocateCab = TRUE;

        } else {
            LocateCab = FALSE;
        }

        if((p = _tcsrchr(FullSourcePath,TEXT('\\')))!=NULL) {
            *p++ = TEXT('\0');
        } else {
            //
            // I'm being pedantic here, this should never happen
            //
            MYASSERT(p);
            p = FullSourcePath;
        }

        //
        // Now FullSourcePath has the path part and p has the file part
        // for the first file in the queue for this media (or explicit cab file)
        // Get the media in the drive by calling the callback function.
        //
        // Although it would be nice to not have to
        // call this callback if we know that we don't have to (there is media
        // where the caller said there should be (local media, media already in, etc.)
        // we do need to call this so that we afford the caller the luxury of
        // changing their mind one last time.
        //
        // the only exception to this rule is if we are using the local driver
        // cache cab-file.  In this case, we don't want the user to ever get
        // prompted for this file, so we skip any media prompting.  We know that
        // if we have media added that has this flag set, then the cab already exists
        // and we can just use it (otherwise we wouldn't have initialized it in the
        // first place, we'd just use the os source path!)
        //
        SourceMedia.Tagfile = (Tagfile != -1 && FirstIteration)
                            ?  pSetupStringTableStringFromId(
                                    Queue->StringTable,
                                    Tagfile
                                    )
                            : NULL;

        SourceMedia.Description = (SourceMediaInfo->Description != -1)
                                ? pSetupStringTableStringFromId(
                                        Queue->StringTable,
                                        SourceMediaInfo->DescriptionDisplayName
                                        )
                                : NULL;

        SourceMedia.SourcePath = FullSourcePath;
        SourceMedia.SourceFile = p;
        SourceMedia.Flags = (QueueNode->StyleFlags & (SP_COPY_NOSKIP | SP_COPY_WARNIFSKIP | SP_COPY_NOBROWSE));

        MediaLogTag = AllocLogInfoSlotOrLevel(Queue->LogContext,SETUP_LOG_INFO,FALSE);
        WriteLogEntry(
                    Queue->LogContext,
                    MediaLogTag,
                    MSG_LOG_NEEDMEDIA,
                    NULL,
                    SourceMedia.Tagfile ? SourceMedia.Tagfile : TEXT(""),
                    SourceMedia.Description ? SourceMedia.Description : TEXT(""),
                    SourceMedia.SourcePath ? SourceMedia.SourcePath : TEXT(""),
                    SourceMedia.SourceFile ? SourceMedia.SourceFile : TEXT(""),
                    SourceMedia.Flags
                    );

        if( SkipMedia || (FirstIteration && (SourceMediaInfo->Flags & SMI_FLAG_USE_LOCAL_SOURCE_CAB)) ) {
            u = FILEOP_DOIT;
            WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_VERBOSE,
                        MSG_LOG_NEEDMEDIA_AUTOSKIP,
                        NULL
                        );
        } else {
            u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_NEEDMEDIA,
                (UINT_PTR)&SourceMedia,
                (UINT_PTR)UserOverride
                );
        }


        if(u == FILEOP_ABORT) {
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_ERROR|SETUP_LOG_BUFFER,
                        MSG_LOG_NEEDMEDIA_ABORT,
                        NULL);
            WriteLogError(Queue->LogContext,
                        SETUP_LOG_ERROR,
                        rc
                        );
            ReleaseLogInfoSlot(Queue->LogContext,MediaLogTag);
            MediaLogTag = 0;
            SetLastError(rc);
            return(rc);
        }
        if(u == FILEOP_SKIP) {
            //
            // If this file was a bootfile replacement, then we need to restore
            // the original file that was renamed to a temporary filename.
            //
            WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_WARNING,
                        MSG_LOG_NEEDMEDIA_SKIP,
                        NULL
                        );
            ReleaseLogInfoSlot(Queue->LogContext,MediaLogTag);
            MediaLogTag = 0;
            if(QueueNode->StyleFlags & SP_COPY_REPLACE_BOOT_FILE) {
                RestoreBootReplacedFile(Queue, QueueNode);
            }

            //
            // If there are more files on this media, then try another one.
            // Otherwise we're done with this media.
            //
            QueueNode->InternalFlags |= IQF_PROCESSED;
            for(QueueNode=QueueNode->Next; QueueNode; QueueNode=QueueNode->Next) {
                if(!(QueueNode->InternalFlags & IQF_PROCESSED)) {
                    FirstIteration = FALSE;
                    goto RepromptMedia;
                }
            }
            continue;
        }
        if(u == FILEOP_NEWPATH) {
            //
            // User gave us a new source path. See which parts of the new path
            // match the existing path/overrides we are using.
            //
            WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_INFO,
                        MSG_LOG_NEEDMEDIA_NEWPATH,
                        NULL,
                        UserOverride
                        );
            ReleaseLogInfoSlot(Queue->LogContext,MediaLogTag);
            MediaLogTag = 0;
            pSetupSetPathOverrides(
                        Queue->StringTable,
                        UserSourceRoot,
                        UserSourcePath,
                        SourceMediaInfo->SourceRootPath,
                        QueueNode->SourcePath,
                        UserOverride
                        );
        }
        //
        // logging specific stuff
        //
        if(MediaLogTag!=0) {
            //
            // we explicitly cleared MediaLogTag for each case we handled
            //
            if (u != FILEOP_DOIT) {
                WriteLogEntry(
                            Queue->LogContext,
                            SETUP_LOG_WARNING,
                            MSG_LOG_NEEDMEDIA_BADRESULT,
                            NULL,
                            u);
            }
            ReleaseLogInfoSlot(Queue->LogContext,MediaLogTag);
            MediaLogTag = 0;
        }

        //
        // If we get here, the media is now accessible.
        // Some or all of the files might be in a cabinet whose name is the tagfile.
        //
        // NOTE: Win95 used the tagfile field to be the cabinet name instead.
        // If present it is used as a tagfile of sorts. The absence of a tagfile
        // means the files are not in cabinets. For NT, we don't bother
        // with all of this but instead try to be a little smarter.
        //
        // Scan the media for all source files we expect to find on it.
        // If we find a file, process it. Later we hit the cabinet and only
        // process the files we didn't already find outside the cabinet.
        //
        // exception to this is "explicit cabinet"
        //
        if(LocateCab) {
            //
            // an explicit cabinet was specified
            // this is first iteration
            // we've gone through NEED_MEDIA to obtain disk for this cabinet
            // don't try to process files outside cabinet
            // we know there is at least one file not processed
            //
            b = TRUE;
            queueNode=QueueNode;
        } else {
            //
            // tagfile may also be a cabfile
            // but process all files outside the cabfile first
            //
            for(queueNode=QueueNode; queueNode; queueNode=queueNode->Next) {

                if(queueNode->InternalFlags & IQF_PROCESSED) {
                    //
                    // Already processed. Skip to next file.
                    //
                    continue;
                }

                pSetupBuildSourceForCopy(
                    UserSourceRoot,
                    UserSourcePath,
                    SourceMediaInfo->SourceRootPath,
                    Queue,
                    queueNode,
                    FullSourcePath
                    );

                rc = SetupDetermineSourceFileName(FullSourcePath,&b,&p,NULL);
                if(rc == NO_ERROR || SkipMedia) {
                    //
                    // Found the file outside a cabinet. Process it now.
                    //
                    if(rc == NO_ERROR) {
                        rc = pSetupCopySingleQueuedFile(
                                Queue,
                                queueNode,
                                p,
                                MsgHandler,
                                Context,
                                UserOverride,
                                IsMsgHandlerNativeCharWidth,
                                0
                                );
                        MyFree(p);
                    } else {
                        //
                        // We didn't find the source file, but we're going to try
                        // to copy it anyway since we've decided not to skip the
                        // prompt for media.
                        //
                        rc = pSetupCopySingleQueuedFile(
                                Queue,
                                queueNode,
                                FullSourcePath,
                                MsgHandler,
                                Context,
                                UserOverride,
                                IsMsgHandlerNativeCharWidth,
                                0
                                );
                    }

                    if(rc != NO_ERROR) {
                        return(rc);
                    }

                    //
                    // See if we have a new source path.
                    //
                    if(UserOverride[0]) {
                        pSetupSetPathOverrides(
                            Queue->StringTable,
                            UserSourceRoot,
                            UserSourcePath,
                            SourceMediaInfo->SourceRootPath,
                            queueNode->SourcePath,
                            UserOverride
                            );
                    }
                }
            }
            //
            // See if any files still need to be processed.
            //
            for(b=FALSE,queueNode=QueueNode; queueNode; queueNode=queueNode->Next) {
                if(!(queueNode->InternalFlags & IQF_PROCESSED)) {
                    b = TRUE;
                    break;
                }
            }
        }

        //
        // If any files still need to be processed and we have a potential
        // cabinet file, go try to extract them from a cabinet.
        //
        if(b && (Cabfile != -1) && FirstIteration) {

            pSetupBuildSourceForCopy(
                UserSourceRoot,
                UserSourcePath,
                SourceMediaInfo->SourceRootPath,
                Queue,
                queueNode,
                FullSourcePath
                );

            temp = _tcsrchr(FullSourcePath,TEXT('\\'));
            MYASSERT( temp );
            if(temp) {
                *(temp+1) = 0;
            }

            //
            // obtain path of (potential) cab file
            //
            pSetupConcatenatePaths( FullSourcePath, pSetupStringTableStringFromId(Queue->StringTable,Cabfile), MAX_PATH, NULL );

            if(DiamondIsCabinet(FullSourcePath)) {

                QData.Queue = Queue;
                QData.SourceMedia = SourceMediaInfo;
                QData.MsgHandler = MsgHandler;
                QData.IsMsgHandlerNativeCharWidth = IsMsgHandlerNativeCharWidth;
                QData.Context = Context;
                QData.LogContext = Queue->LogContext;

                rc = DiamondProcessCabinet(
                        FullSourcePath,
                        0,
                        pSetupCabinetQueueCallback,
                        &QData,
                        TRUE
                        );

                if(rc != NO_ERROR) {
                    return(rc);
                }

                //
                // Now reset the cabfile to indicate that there is no cabinet.
                // If we don't do this and there are still files that have not
                // been processed, we'll end up in an infinite loop -- the prompt
                // will come back successfully, and we'll just keep going around
                // and around looking through the cabinet, etc.
                //
                Cabfile = -1;
                Tagfile = -1; // for compatability
            }
        }

        //
        // If we get here and files *still* need to be processed,
        // assume the files are in a different directory somewhere
        // and start all over with this media.
        //
        FirstIteration = FALSE;
        DiskPromptGetDriveType(FullSourcePath, &DriveType, &IsRemovable);
        AnyProcessed = FALSE;
        AnyNotProcessed = FALSE;

        for(QueueNode = SourceMediaInfo->CopyQueue;
            QueueNode;
            QueueNode=QueueNode->Next) {

            if(IsRemovable) {
                if(!(QueueNode->InternalFlags & IQF_PROCESSED)) {
                    if(Tagfile != -1) {
                        SkipMedia = TRUE;
                    }
                    goto RepromptMedia;
                }
            } else { // Fixed media
                if(QueueNode->InternalFlags & IQF_PROCESSED) {
                    AnyProcessed = TRUE;
                } else {
                    AnyNotProcessed = TRUE;
                }
            }
        }

        if(!IsRemovable) {
            if(AnyNotProcessed) {

                //
                // If some of the files are present on fixed media, we don't
                // want to look elsewhere.
                //
                if(AnyProcessed) {
                    SkipMedia = TRUE;
                }

                //
                // Find the first unprocessed file
                //
                for(QueueNode = SourceMediaInfo->CopyQueue;
                    QueueNode;
                    QueueNode = QueueNode->Next) {

                    if(!(QueueNode->InternalFlags & IQF_PROCESSED)) {
                        break;
                    }
                }
                MYASSERT(QueueNode);

                goto RepromptMedia;
            }
        }

    } // end for each source media info

    //
    // Tell handler we're done with the copy queue and return.
    //
    pSetupCallMsgHandler(
        Queue->LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDSUBQUEUE,
        FILEOP_COPY,
        0
        );

    return(NO_ERROR);
}


VOID
pSetupBuildSourceForCopy(
    IN  PCTSTR              UserRoot,
    IN  PCTSTR              UserPath,
    IN  LONG                MediaRoot,
    IN  PSP_FILE_QUEUE      Queue,
    IN  PSP_FILE_QUEUE_NODE QueueNode,
    OUT PTSTR               FullPath
    )
{
    PCTSTR p;


    //
    // If there is a user-specified override root path, use that instead of
    // the root path specified in the source media descriptor.
    //
    MYASSERT(Queue);
    MYASSERT(QueueNode);
    MYASSERT(FullPath);

    p = (UserRoot && UserRoot[0])
      ? UserRoot
      : pSetupStringTableStringFromId(Queue->StringTable,MediaRoot);


    lstrcpyn(FullPath,p,MAX_PATH);

    //
    // If there is a user-specified override path, use that instead of any
    // path specified in the copy node.
    //
    if(UserPath && UserPath[0]) {
        p = UserPath;
    } else {
        if(QueueNode->SourcePath == -1) {
            p = NULL;
        } else {
            p = pSetupStringTableStringFromId(Queue->StringTable,QueueNode->SourcePath);
        }
    }

    if(p) {
        pSetupConcatenatePaths(FullPath,p,MAX_PATH,NULL);
    }

    //
    // Fetch the filename and append.
    //
    p = pSetupStringTableStringFromId(Queue->StringTable,QueueNode->SourceFilename),
    pSetupConcatenatePaths(FullPath,p,MAX_PATH,NULL);

}

VOID
pSetupSetPathOverrides(
    IN     PVOID StringTable,
    IN OUT PTSTR RootPath,
    IN OUT PTSTR SubPath,
    IN     LONG  RootPathId,
    IN     LONG  SubPathId,
    IN     PTSTR NewPath
    )
{
    PCTSTR root,path;
    UINT u,l;

    //
    // See if the existing root override or root path is a prefix
    // of the path the user gave us.
    //
    MYASSERT(RootPath);
    MYASSERT(SubPath);
    root = RootPath[0] ? RootPath : pSetupStringTableStringFromId(StringTable,RootPathId);
    u = lstrlen(root);

    path = SubPath[0]
         ? SubPath
         : ((SubPathId == -1) ? NULL : pSetupStringTableStringFromId(StringTable,SubPathId));

    if(path && (*path == TEXT('\\'))) {
        path++;
    }

    if(_tcsnicmp(NewPath,root,u)) {
        //
        // Root path does not match what we're currently using, ie, the user
        // supplied a new path. In this case, we will see if the currently in-use
        // subpath matches the suffix of the new path, and if so, we'll assume
        // that is the override subpath and shorten the override root path.
        //
        lstrcpy(RootPath,NewPath);
        if(path) {
            u = lstrlen(NewPath);
            l = lstrlen(path);

            if((u > l) && (NewPath[(u-l)-1] == TEXT('\\')) && !lstrcmpi(NewPath+u-l,path)) {
                //
                // Subpath tail matches. Truncate the root override and
                // leave the subpath override alone.
                //
                RootPath[(u-l)-1] = 0;
            } else {
                //
                // In this case, we need to indicate an override subpath of the root,
                // or else all subsequent accesses will still try to append the subpath
                // specified in the copy node, which is not what we want.
                //
                SubPath[0] = TEXT('\\');
                SubPath[1] = 0;
            }
        }
    } else {
        //
        // Root path matches what we are currently using.
        //
        // See if the tail of the user-specified path matches the existing
        // subpath. If not, then use the rest of the root path as the subpath
        // override. If the tail matches, then extend the user override root.
        //
        // Examples:
        //
        //  File was queued with root = f:\, subpath = \mips
        //
        //  User override path is f:\alpha
        //
        //  The new status will be leave override root alone;
        //  override subpath = \alpha
        //
        //  File was queued with root = \\foo\bar, subpath = \i386
        //
        //  User override path is \\foo\bar\new\i386
        //
        //  The new status will be a root override of \\foo\bar\new;
        //  no override subpath.
        //
        NewPath += u;
        if(*NewPath == TEXT('\\')) {
            NewPath++;
        }

        if(path) {
            u = lstrlen(NewPath);
            l = lstrlen(path);

            if((u >= l) && !lstrcmpi(NewPath+u-l,path)) {
                //
                // Change root override and indicate no override subpath.
                //
                SubPath[0] = TEXT('\0');
                NewPath[u-l] = TEXT('\0');
                lstrcpy(RootPath,root);
                pSetupConcatenatePaths(RootPath,NewPath,MAX_PATH,NULL);
                u = lstrlen(RootPath);
                if(u && (*CharPrev(RootPath,RootPath+u) == TEXT('\\'))) {
                    RootPath[u-1] = TEXT('\0'); // valid to do if last char is '\'
                }
            } else {
                //
                // Leave override root alone but change subpath.
                //
                lstrcpy(SubPath,NewPath);
                if(!SubPath[0]) {
                    SubPath[0] = TEXT('\\');
                    SubPath[1] = TEXT('\0');
                }
            }
        } else {
            //
            // File was queued without a subpath. If there's a subpath
            // in what the user gave us, use it as the override.
            //
            if(*NewPath) {
                lstrcpy(SubPath,NewPath);
            }
        }
    }
}


UINT
pSetupCabinetQueueCallback(
    IN PVOID Context,
    IN UINT  Notification,
    IN UINT_PTR  Param1,
    IN UINT_PTR  Param2
    )
{
    UINT rc;
    PCABINET_INFO CabinetInfo;
    PFILE_IN_CABINET_INFO FileInfo;
    TCHAR TempPath[MAX_PATH];
    PTSTR CabinetFile;
    PTSTR QueuedFile;
    PTSTR FilePart1,FilePart2;
    PTSTR FullTargetPath;
    PFILEPATHS FilePaths;
    PSP_FILE_QUEUE_NODE QueueNode,FirstNode,LastNode;
    PQ_CAB_CB_DATA QData;
    UINT h;
    SOURCE_MEDIA SourceMedia;
    DWORD status;

    QData = (PQ_CAB_CB_DATA)Context;

    switch(Notification) {

    case SPFILENOTIFY_CABINETINFO:
        //
        // We don't do anything with this.
        //
        rc = NO_ERROR;
        break;

    case SPFILENOTIFY_FILEINCABINET:
        //
        // New file within a cabinet.
        //
        // Determine whether we want to copy this file.
        // The context we get has all the stuff we need in it
        // to make this determination.
        //
        // Note that the queue could contain multiple copy operations
        // involving this file, but we only want to extract it once!
        //
        FileInfo = (PFILE_IN_CABINET_INFO)Param1;
        CabinetFile = (PTSTR)Param2;

        if(FilePart1 = _tcsrchr(FileInfo->NameInCabinet,TEXT('\\'))) {
            FilePart1++;
        } else {
            FilePart1 = (PTSTR)FileInfo->NameInCabinet;
        }

        rc = FILEOP_SKIP;
        FileInfo->Win32Error = NO_ERROR;
        FirstNode = NULL;

        //
        // Find ALL instances of this file in the queue and mark them.
        //
        for(QueueNode=QData->SourceMedia->CopyQueue; QueueNode; QueueNode=QueueNode->Next) {

            if(QueueNode->InternalFlags & IQF_PROCESSED) {
                //
                // This file was already processed. Ignore it.
                //
                continue;
            }

            //
            // Check the filename in the cabinet against the file
            // in the media's copy queue.
            //
            QueuedFile = pSetupStringTableStringFromId(
                            QData->Queue->StringTable,
                            QueueNode->SourceFilename
                            );

            if(FilePart2 = _tcsrchr(QueuedFile,TEXT('\\'))) {
                FilePart2++;
            } else {
                FilePart2 = QueuedFile;
            }

            if(!lstrcmpi(FilePart1,FilePart2)) {
                //
                // We want this file.
                //
                rc = FILEOP_DOIT;
                QueueNode->InternalFlags |= IQF_PROCESSED | IQF_MATCH;
                if(!FirstNode) {
                    FirstNode = QueueNode;
                }
                LastNode = QueueNode;
            }
        }

        if(rc == FILEOP_DOIT) {
            //
            // We want this file. Tell the caller the full target pathname
            // to be used, which is a temporary file in the directory
            // where the first instance of the file will ultimately go.
            // We do this so we can call SetupInstallFile later (perhaps
            // multiple times), which will handle version checks, etc.
            //
            // Before attempting to create a temp file make sure the path exists.
            //
            lstrcpyn(
                TempPath,
                pSetupStringTableStringFromId(QData->Queue->StringTable,FirstNode->TargetDirectory),
                MAX_PATH
                );
            pSetupConcatenatePaths(TempPath,TEXT("x"),MAX_PATH,NULL); // last component ignored
            status = pSetupMakeSurePathExists(TempPath);
            if(status == NO_ERROR) {
                LastNode->InternalFlags |= IQF_LAST_MATCH;
                if(GetTempFileName(
                        pSetupStringTableStringFromId(QData->Queue->StringTable,FirstNode->TargetDirectory),
                        TEXT("SETP"),
                        0,
                        FileInfo->FullTargetName
                        )) {
                    QData->CurrentFirstNode = FirstNode;
                } else {
                    status = GetLastError();
                    if(status == ERROR_ACCESS_DENIED) {
                        FileInfo->Win32Error = ERROR_INVALID_TARGET;
                    } else {
                        FileInfo->Win32Error = status;
                    }
                    rc = FILEOP_ABORT;
                    SetLastError(FileInfo->Win32Error);
                }
            } else {
                if(status == ERROR_ACCESS_DENIED) {
                    FileInfo->Win32Error = ERROR_INVALID_TARGET;
                } else {
                    FileInfo->Win32Error = status;
                }
                rc = FILEOP_ABORT;
                SetLastError(FileInfo->Win32Error);
            }
        }

        break;

    case SPFILENOTIFY_FILEEXTRACTED:

        FilePaths = (PFILEPATHS)Param1;
        //
        // The current file was extracted. If this was successful,
        // then we need to call SetupInstallFile on it to perform version
        // checks and move it into its final location or locations.
        //
        // The .Source member of FilePaths is the cabinet file.
        //
        // The .Target member is the name of the temporary file, which is
        // very useful, as it is the name if the file to use as the source
        // in copy operations.
        //
        // Process each file in the queue that we care about.
        //
        if((rc = FilePaths->Win32Error) == NO_ERROR) {

            for(QueueNode=QData->CurrentFirstNode; QueueNode && (rc==NO_ERROR); QueueNode=QueueNode->Next) {
                //
                // If we don't care about this file, skip it.
                //
                if(!(QueueNode->InternalFlags & IQF_MATCH)) {
                    continue;
                }

                QueueNode->InternalFlags &= ~IQF_MATCH;


                rc = pSetupCopySingleQueuedFileCabCase(
                        QData->Queue,
                        QueueNode,
                        FilePaths->Source,
                        FilePaths->Target,
                        QData->MsgHandler,
                        QData->Context,
                        QData->IsMsgHandlerNativeCharWidth
                        );

                //
                // If this was the last file that matched, break out.
                //
                if(QueueNode->InternalFlags & IQF_LAST_MATCH) {
                    QueueNode->InternalFlags &= ~IQF_LAST_MATCH;
                    break;
                }
            }
        }

        //
        // Delete the temporary file we extracted -- we don't need it any more.
        //
        DeleteFile(FilePaths->Target);

        break;

    case SPFILENOTIFY_NEEDNEWCABINET:
        //
        // Need a new cabinet.
        //
        CabinetInfo = (PCABINET_INFO)Param1;

        SourceMedia.Tagfile = NULL;
        SourceMedia.Description = CabinetInfo->DiskName;
        SourceMedia.SourcePath = CabinetInfo->CabinetPath;
        SourceMedia.SourceFile = CabinetInfo->CabinetFile;
        SourceMedia.Flags = SP_FLAG_CABINETCONTINUATION | SP_COPY_NOSKIP;

        h = pSetupCallMsgHandler(
                QData->LogContext,
                QData->MsgHandler,
                QData->IsMsgHandlerNativeCharWidth,
                QData->Context,
                SPFILENOTIFY_NEEDMEDIA,
                (UINT_PTR)&SourceMedia,
                Param2
                );

        switch(h) {

        case FILEOP_NEWPATH:
        case FILEOP_DOIT:
            rc = NO_ERROR;
            break;

        case FILEOP_ABORT:
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            break;

        default:
            rc = ERROR_OPERATION_ABORTED;
            break;

        }
        //
        // in this case, rc is a status code
        // but also set it as last error
        //
        SetLastError(rc);
        break;

    default:
        MYASSERT(0);
        rc = 0; // indeterminate
    }

    return(rc);
}


DWORD
pSetupCopySingleQueuedFile(
    IN  PSP_FILE_QUEUE      Queue,
    IN  PSP_FILE_QUEUE_NODE QueueNode,
    IN  PCTSTR              FullSourceName,
    IN  PVOID               MsgHandler,
    IN  PVOID               Context,
    OUT PTSTR               NewSourcePath,
    IN  BOOL                IsMsgHandlerNativeCharWidth,
    IN  DWORD               CopyStyleFlags
    )
{
    PTSTR FullTargetName;
    FILEPATHS FilePaths;
    UINT u;
    BOOL InUse;
    TCHAR source[MAX_PATH],PathBuffer[MAX_PATH];
    DWORD rc;
    BOOL b;
    BOOL BackupInUse = FALSE;
    BOOL SignatureVerifyFailed;

    NewSourcePath[0] = 0;
    PathBuffer[0] = 0;

    QueueNode->InternalFlags |= IQF_PROCESSED;

    //
    // Form the full target path of the file.
    //
    FullTargetName = pSetupFormFullPath(
                        Queue->StringTable,
                        QueueNode->TargetDirectory,
                        QueueNode->TargetFilename,
                        -1
                        );

    if(!FullTargetName) {
        return(ERROR_NOT_ENOUGH_MEMORY);
    }

    lstrcpyn(source,FullSourceName,MAX_PATH);

    //
    // check if we need to backup before we copy
    //
    if((rc=pSetupDoLastKnownGoodBackup(Queue,
                                       FullTargetName,
                                       0,
                                       NULL)) != NO_ERROR) {
        MyFree(FullTargetName);
        goto clean0;
    }
    rc = pSetupCommitSingleBackup(Queue,
                                  FullTargetName,
                                  QueueNode->TargetDirectory,
                                  -1,
                                  QueueNode->TargetFilename,
                                  MsgHandler,
                                  Context,
                                  IsMsgHandlerNativeCharWidth,
                                  (QueueNode->StyleFlags & SP_COPY_REPLACE_BOOT_FILE),
                                  &BackupInUse
                                 );
    if (rc != NO_ERROR) {
        MyFree(FullTargetName);
        goto clean0;
    }

    if (BackupInUse) {
        //
        // if we couldn't do backup, force the IN_USE flag
        //
        QueueNode->StyleFlags |= SP_COPY_FORCE_IN_USE;

    }

    do {
        //
        // Form the full source name.
        //
        FilePaths.Source = source;
        FilePaths.Target = FullTargetName;
        FilePaths.Win32Error = NO_ERROR;

        //
        // Also, pass the callback routine the CopyStyle flags we're about to
        // use.
        //
        // Callback flags are read-only.
        //
        FilePaths.Flags = QueueNode->StyleFlags;

        //
        // Notify the callback that the copy is starting.
        //
        u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_STARTCOPY,
                (UINT_PTR)&FilePaths,
                FILEOP_COPY
                );

        if(u == FILEOP_ABORT) {
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_ERROR|SETUP_LOG_BUFFER,
                        MSG_LOG_STARTCOPY_ABORT,
                        NULL);
            WriteLogError(Queue->LogContext,
                        SETUP_LOG_ERROR,
                        rc);
            break;
        }

        if(u == FILEOP_DOIT) {

            //
            // Attempt the copy.
            //
            //

            b = _SetupInstallFileEx(
                    Queue,
                    QueueNode,
                    NULL,                   // no inf handle
                    NULL,                   // no inf context
                    source,
                    NULL,                   // source path root is part of FullSourcePath
                    FullTargetName,
                    QueueNode->StyleFlags | SP_COPY_SOURCE_ABSOLUTE | CopyStyleFlags,
                    MsgHandler,
                    Context,
                    &InUse,
                    IsMsgHandlerNativeCharWidth,
                    &SignatureVerifyFailed
                    );

            rc = b ? NO_ERROR : GetLastError();

#ifdef UNICODE

            if(b || (rc == NO_ERROR)) {
                if(!InUse && (QueueNode->SecurityDesc != -1)){
                    //
                    // Set security on the file
                    //
                    rc = pSetupCallSCE(ST_SCE_SET,
                                       FullTargetName,
                                       Queue,
                                       NULL,
                                       QueueNode->SecurityDesc,
                                       NULL
                                      );
                }
            }
#endif

            if(rc == NO_ERROR) {
                //
                // File was copied or not copied, but it if was not copied
                // the callback funtcion was already notified about why
                // (version check failed, etc).
                //
                if(QueueNode->StyleFlags & SP_COPY_REPLACE_BOOT_FILE) {
                    //
                    // _SetupInstallFileEx is responsible for failing the copy
                    // when some yahoo comes and copies over a new file (and
                    // locks it) before we get a chance to.
                    //
                    MYASSERT(!InUse);

                    //
                    // If the file was copied, we need to set the wants-reboot
                    // flag.  Otherwise, we need to put back the original file.
                    //
                    if(b) {
                        QueueNode->InternalFlags |= INUSE_INF_WANTS_REBOOT;
                    } else {
                        RestoreBootReplacedFile(Queue, QueueNode);
                    }

                } else {

                    if(InUse) {
                        QueueNode->InternalFlags |= (QueueNode->StyleFlags & SP_COPY_IN_USE_NEEDS_REBOOT)
                                                  ? INUSE_INF_WANTS_REBOOT
                                                  : INUSE_IN_USE;
                    }
                }

            } else {
                DWORD LogTag = 0;
                //
                // File was not copied and a real error occurred.
                // Notify the callback (unless the failure was due to a signature
                // verification problem). Disallow skip if that is specified
                // in the node's flags.
                //
                if(SignatureVerifyFailed) {
                    break;
                } else {
                    LogTag = AllocLogInfoSlotOrLevel(Queue->LogContext,SETUP_LOG_INFO,FALSE);

                    FilePaths.Win32Error = rc;
                    FilePaths.Flags = QueueNode->StyleFlags & (SP_COPY_NOSKIP | SP_COPY_WARNIFSKIP | SP_COPY_NOBROWSE);

                    WriteLogEntry(
                                Queue->LogContext,
                                LogTag,
                                MSG_LOG_COPYERROR,
                                NULL,
                                FilePaths.Source,
                                FilePaths.Target,
                                FilePaths.Flags,
                                FilePaths.Win32Error
                                );

                    u = pSetupCallMsgHandler(
                            Queue->LogContext,
                            MsgHandler,
                            IsMsgHandlerNativeCharWidth,
                            Context,
                            SPFILENOTIFY_COPYERROR,
                            (UINT_PTR)&FilePaths,
                            (UINT_PTR)PathBuffer
                            );
                    if(u == FILEOP_ABORT) {
                        rc = GetLastError();
                        if(!rc) {
                            rc = ERROR_OPERATION_ABORTED;
                        }
                    }
                }

                if(u == FILEOP_ABORT) {
                    WriteLogEntry(
                                Queue->LogContext,
                                SETUP_LOG_ERROR|SETUP_LOG_BUFFER,
                                MSG_LOG_COPYERROR_ABORT,
                                NULL
                                );
                    WriteLogError(Queue->LogContext,
                                SETUP_LOG_ERROR,
                                rc
                                );
                    ReleaseLogInfoSlot(Queue->LogContext,LogTag);
                    LogTag = 0;

                    break;
                } else {
                    if(u == FILEOP_SKIP) {
                        //
                        // If this file was a bootfile replacement, then we need
                        // to restore the original file that was renamed to a
                        // temporary filename.
                        //
                        if(QueueNode->StyleFlags & SP_COPY_REPLACE_BOOT_FILE) {
                            RestoreBootReplacedFile(Queue, QueueNode);
                        }

                        WriteLogEntry(
                                    Queue->LogContext,
                                    SETUP_LOG_WARNING,
                                    MSG_LOG_COPYERROR_SKIP,
                                    NULL
                                    );
                        ReleaseLogInfoSlot(Queue->LogContext,LogTag);
                        LogTag = 0;
                        //
                        // Force termination of processing for this file.
                        //
                        rc = NO_ERROR;
                        break;

                    } else {
                        if((u == FILEOP_NEWPATH) || ((u == FILEOP_RETRY) && PathBuffer[0])) {
                            WriteLogEntry(
                                        Queue->LogContext,
                                        SETUP_LOG_WARNING,
                                        MSG_LOG_COPYERROR_NEWPATH,
                                        NULL,
                                        u,
                                        PathBuffer
                                        );
                            ReleaseLogInfoSlot(Queue->LogContext,LogTag);
                            LogTag = 0;

                            //
                            // Note that rc is already set to something other than
                            // NO_ERROR or we wouldn't be here.
                            //
                            lstrcpyn(NewSourcePath,PathBuffer,MAX_PATH);
                            lstrcpyn(source,NewSourcePath,MAX_PATH);
                            pSetupConcatenatePaths(
                                source,
                                pSetupStringTableStringFromId(Queue->StringTable,QueueNode->SourceFilename),
                                MAX_PATH,
                                NULL
                                );
                        }

                        //
                        // Else we don't have a new path.
                        // Just keep using the one we had.
                        //
                    }
                }
                if (LogTag != 0) {
                    //
                    // haven't done anything regards logging yet, do it now
                    //
                    WriteLogEntry(
                                Queue->LogContext,
                                SETUP_LOG_INFO,
                                MSG_LOG_COPYERROR_RETRY,
                                NULL,
                                u
                                );
                    ReleaseLogInfoSlot(Queue->LogContext,LogTag);
                    LogTag = 0;
                }
            }
        } else {
            //
            // skip file
            //
            WriteLogEntry(
                        Queue->LogContext,
                        SETUP_LOG_INFO, // info level as this would be due to override of callback
                        MSG_LOG_STARTCOPY_SKIP,
                        NULL,
                        u
                        );
            rc = NO_ERROR;
        }
    } while(rc != NO_ERROR);

    //
    // Notify the callback that the copy is done.
    //
    FilePaths.Win32Error = rc;
    pSetupCallMsgHandler(
        Queue->LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDCOPY,
        (UINT_PTR)&FilePaths,
        0
        );


    MyFree(FullTargetName);

clean0:

    return(rc);
}


DWORD
pSetupCopySingleQueuedFileCabCase(
    IN  PSP_FILE_QUEUE      Queue,
    IN  PSP_FILE_QUEUE_NODE QueueNode,
    IN  PCTSTR              CabinetName,
    IN  PCTSTR              FullSourceName,
    IN  PVOID               MsgHandler,
    IN  PVOID               Context,
    IN  BOOL                IsMsgHandlerNativeCharWidth
    )
{
    PTSTR FullTargetName;
    FILEPATHS FilePaths;
    UINT u;
    BOOL InUse;
    TCHAR PathBuffer[MAX_PATH];
    DWORD rc;
    BOOL b;
    BOOL BackupInUse = FALSE;
    BOOL SignatureVerifyFailed;
    DWORD LogTag = 0;
    LPCTSTR SourceName;

    //
    // Form the full target path of the file.
    //
    SourceName = pSetupStringTableStringFromId(Queue->StringTable,QueueNode->SourceFilename);
    FullTargetName = pSetupFormFullPath(
                        Queue->StringTable,
                        QueueNode->TargetDirectory,
                        QueueNode->TargetFilename,
                        -1
                        );

    if(!FullTargetName) {
        return(ERROR_NOT_ENOUGH_MEMORY);
    }


    LogTag = AllocLogInfoSlotOrLevel(Queue->LogContext,SETUP_LOG_INFO,FALSE);
    WriteLogEntry(
                Queue->LogContext,
                LogTag,
                MSG_LOG_COPY_FROM_CAB,
                NULL,
                CabinetName,
                SourceName,
                FullSourceName,
                FullTargetName
                );

    //
    // check if we need to backup before we copy
    //
    if((rc=pSetupDoLastKnownGoodBackup(Queue,
                                       FullTargetName,
                                       0,
                                       NULL)) != NO_ERROR) {
        MyFree(FullTargetName);
        goto clean0;
    }
    rc = pSetupCommitSingleBackup(Queue,
                                  FullTargetName,
                                  QueueNode->TargetDirectory,
                                  -1,
                                  QueueNode->TargetFilename,
                                  MsgHandler,
                                  Context,
                                  IsMsgHandlerNativeCharWidth,
                                  (QueueNode->StyleFlags & SP_COPY_REPLACE_BOOT_FILE),
                                  &BackupInUse
                                 );
    if (rc != NO_ERROR) {
        MyFree(FullTargetName);
        goto clean0;
    }

    if (BackupInUse) {
        //
        // if we couldn't do backup, force the IN_USE flag
        //
        QueueNode->StyleFlags |= SP_COPY_FORCE_IN_USE;

    }
    //
    // We use the cabinet name as the source name so the display looks right
    // to the user. Otherwise he sees the name of some temp file in the
    // source field.
    //
    FilePaths.Source = CabinetName;
    FilePaths.Target = FullTargetName;
    FilePaths.Win32Error = NO_ERROR;

    //
    // Also, pass the callback routine the CopyStyle flags we're about to
    // use.
    //
    // Callback flags are read-only.
    //
    FilePaths.Flags = QueueNode->StyleFlags;

    do {
        //
        // Notify the callback that the copy is starting.
        //
        u = pSetupCallMsgHandler(
                Queue->LogContext,
                MsgHandler,
                IsMsgHandlerNativeCharWidth,
                Context,
                SPFILENOTIFY_STARTCOPY,
                (UINT_PTR)&FilePaths,
                FILEOP_COPY
                );

        if(u == FILEOP_ABORT) {
            rc = GetLastError();
            if(!rc) {
                rc = ERROR_OPERATION_ABORTED;
            }
            break;
        }

        if(u == FILEOP_DOIT) {
            //
            // Attempt the copy.
            //
            b = _SetupInstallFileEx(
                    Queue,
                    QueueNode,
                    NULL,                   // no inf handle
                    NULL,                   // no inf context
                    FullSourceName,
                    NULL,                   // source path root is part of FullSourcePath
                    FullTargetName,
                    QueueNode->StyleFlags | SP_COPY_SOURCE_ABSOLUTE,
                    MsgHandler,
                    Context,
                    &InUse,
                    IsMsgHandlerNativeCharWidth,
                    &SignatureVerifyFailed
                    );

#ifdef UNICODE
            if(b || ((rc = GetLastError()) == NO_ERROR)) {
                if(!InUse && (QueueNode->SecurityDesc != -1) ){
                    // Set security on the file

                    rc = pSetupCallSCE(
                            ST_SCE_SET,
                            FullTargetName,
                            Queue,
                            NULL,
                            QueueNode->SecurityDesc,
                            NULL
                            );
                    SetLastError( rc );
                }

            }
#endif

            if(b || ((rc = GetLastError()) == NO_ERROR)) {
                //
                // File was copied or not copied, but it if was not copied
                // the callback funtcion was already notified about why
                // (version check failed, etc).
                //
                if(InUse) {
                    QueueNode->InternalFlags |= (QueueNode->StyleFlags & SP_COPY_IN_USE_NEEDS_REBOOT)
                                              ? INUSE_INF_WANTS_REBOOT
                                              : INUSE_IN_USE;
                }
                rc = NO_ERROR;
            } else {
                //
                // File was not copied and a real error occurred.
                // Break out and return the error.
                //
                break;
            }
        } else {
            //
            // skip file
            //
            rc = NO_ERROR;
        }
    } while(rc != NO_ERROR);

    //
    // Notify the callback that the copy is done.
    //
    FilePaths.Win32Error = rc;
    pSetupCallMsgHandler(
        Queue->LogContext,
        MsgHandler,
        IsMsgHandlerNativeCharWidth,
        Context,
        SPFILENOTIFY_ENDCOPY,
        (UINT_PTR)&FilePaths,
        0
        );

    MyFree(FullTargetName);

clean0:
    if(LogTag) {
        ReleaseLogInfoSlot(Queue->LogContext,LogTag);
    }

    return(rc);
}


PTSTR
pSetupFormFullPath(
    IN PVOID  StringTable,
    IN LONG   PathPart1,
    IN LONG   PathPart2,    OPTIONAL
    IN LONG   PathPart3     OPTIONAL
    )

/*++

Routine Description:

    Form a full path based on components whose strings are in a string
    table.

Arguments:

    StringTable - supplies handle to string table.

    PathPart1 - Supplies first part of path

    PathPart2 - if specified, supplies second part of path

    PathPart3 - if specified, supplies third part of path

Return Value:

    Pointer to buffer containing full path. Caller can free with MyFree().
    NULL if out of memory.

--*/

{
    UINT RequiredSize;
    PCTSTR p1,p2,p3;
    TCHAR Buffer[MAX_PATH];

    p1 = pSetupStringTableStringFromId(StringTable,PathPart1);
    if (!p1) {
        return NULL;
    }
    p2 = (PathPart2 == -1) ? NULL : pSetupStringTableStringFromId(StringTable,PathPart2);
    p3 = (PathPart3 == -1) ? NULL : pSetupStringTableStringFromId(StringTable,PathPart3);

    lstrcpy(Buffer,p1);
    if(!p2 || pSetupConcatenatePaths(Buffer,p2,MAX_PATH,NULL)) {
        if(p3) {
            pSetupConcatenatePaths(Buffer,p3,MAX_PATH,NULL);
        }
    }

    return(DuplicateString(Buffer));
}


DWORD
pSetupVerifyQueuedCatalogs(
    IN HSPFILEQ FileQueue
    )
/*++

Routine Description:

    Silently verify all catalog nodes in the specified queue.

Arguments:

    FileQueue - supplies a handle to the file queue containing catalog nodes
        to be verified.

Return Value:

    If all catalog nodes are valid, the return value is NO_ERROR.  Otherwise,
    it is a Win32 error code indicating the problem.

--*/
{
    return _SetupVerifyQueuedCatalogs(NULL,  // No UI, thus no HWND needed
                                      (PSP_FILE_QUEUE)FileQueue,
                                      VERCAT_NO_PROMPT_ON_ERROR,
                                      NULL,
                                      NULL
                                     );
}


DWORD
_SetupVerifyQueuedCatalogs(
    IN  HWND           Owner,
    IN  PSP_FILE_QUEUE Queue,
    IN  DWORD          Flags,
    OUT PTSTR          DeviceInfFinalName,  OPTIONAL
    OUT PBOOL          DeviceInfNewlyCopied OPTIONAL
    )

/*++

Routine Description:

    This routine verifies catalogs and infs in a given queue by traversing
    the catalog node list associated with the queue and operating on the
    catalog/inf pair described by each one.

    If any catalog/inf fails verification, the user is notified via a dialog,
    depending on current policy.

    ** Behavior for native platform verification (w/o catalog override)

    If an INF is from a system location, we assume that the catalog is
    already installed on the system. Really there is no other option here,
    since we would have no idea where to get the catalog in order to install it
    even if we wanted to try. But the inf might have originally been an
    oem inf which was copied and renamed by the Di stuff at device install
    time. The catalog file knows nothing about the renamed file, so we must
    track mappings from current inf filename to original inf filename.

    In this case, we calculate the inf's hash value and then using that,
    we ask the system for a catalog file that contains signing data
    for that hash value. We then ask the system for info
    about that catalog file. We keep repeating this process until we get
    at the catalog we want (based on name). Finally we can call WinVerifyTrust
    verify the catalog itself and the inf.

    If an INF file is instead from an oem location, we copy the oem inf to a
    unique name in the system inf directory (or create a zero-length placeholder
    there, depending on whether or not the VERCAT_INSTALL_INF_AND_CAT flag is
    set), and add the catalog using a filename based on that unique filename.

    ** Behavior for non-native platform verification (w/o catalog override) **

    We will validate the catalogs and INFs using the alternate platform info
    provided in the file queue.  Otherwise, the logic is the same as in the
    native case.

    ** Behavior for verification (w/catalog override) **

    The actual verification will be done using native or non-native parameters
    as discussed above, but INFs without a CatalogFile= entry will be validated
    against the specified overriding catalog.  This means that system INFs won't
    get validated globally, and INF in OEM locations can be validated even if
    they don't have a CatalogFile= entry.  The overriding catalog file will be
    installed under its current name, thus blowing away any existing catalog
    having that name.

    See the documentation on SetupSetFileQueueAlternatePlatform for more
    details.

Arguments:

    Owner - supplies window handle of window to own any ui.  This HWND is stored
        away in the queue for use later if any individual files fail verification.

    Queue - supplies pointer to queue structure.

    Flags - supplies flags that control behavior of this routine.

        VERCAT_INSTALL_INF_AND_CAT - if this flag is set, any infs from
            oem locations will be installed on the system, along with
            their catalog files.

        VERCAT_NO_PROMPT_ON_ERROR - if this flag is set, the user will _not_ be
            notified about verification failures we encounter.  If this flag is
            set, then this was only a 'test', and no user prompting should take
            place (nor should any PSS logging take place).  If this flag is set,
            then the VERCAT_INSTALL_INF_AND_CAT _should not_ be specified.

        VERCAT_PRIMARY_DEVICE_INF_FROM_INET - specifies that the primary device
            INF in the queue is from the internet, and should be marked as such
            in the corresponding PNF when installed into the %windir%\Inf
            directory via _SetupCopyOEMInf.

    DeviceInfFinalName - optionally, supplies the address of a character buffer,
        _at least_ MAX_PATH characters long, that upon success receives the
        final name given to the INF under the %windir%\Inf directory (this will
        be different than the INF's original name if it was an OEM INF).

    DeviceInfNewlyCopied - optionally, supplies the address of a boolean
        variable that, upon success, is set to indicate whether the INF name
        returned in DeviceInfFinalName was newly-created.  If this parameter is
        supplied, then DeviceInfFinalName must also be specified.

Return Value:

    If all catalogs/infs were verified and installed, or the user accepted
        the risk if a verification failed, then the return value is NO_ERROR.

    If one or more catalogs/infs were not verified, the return value is a Win32
        error code indicating the cause of the failure.  NOTE:  This error will
        only be returned if the policy is "block", or it it's "warn" and the
        user decided to abort.  In this case, the error returned is for the
        catalog/INF where the error was encountered, and any subsequent catalog
        nodes will not have been verified.  An exception to this is when the
        VERCAT_NO_PROMPT_ON_ERROR flag is set.  In that case, we'll verify all
        catalogs, even if we encounter improperly-signed ones.

Remarks:

    There are some system INFs (for which global verification is required) that
    don't live in %windir%\Inf.  The OCM INFs are an example of this.  Those
    INFs use layout.inf (which _is_ located in %windir%\Inf) for the source
    media information for any files they copy.  There are other INFs that don't
    live in %windir%\Inf which are extracted out of a binary as-needed (into a
    temporary filename), processed in order to do registry munging, and then
    deleted.  Such INFs do not do file copying (thus their 'package' consists
    of just the INF).  To accommodate such INFs, we allow "OEM" INFs (i.e.,
    those INFs not in %windir%\Inf) to be verified globally, but we remember the
    fact that these INFs didn't contain a CatalogFile= entry, and if any files
    are ever queued for copy using such INFs for source media information, then
    we'll fail digital signature verification for such files, since there's no
    way for us to know what catalog should be used for verification.

--*/

{
    PSPQ_CATALOG_INFO CatalogNode;
    LPCTSTR InfFullPath;
    LPCTSTR CatName;
    TCHAR PathBuffer[MAX_PATH];
    TCHAR InfNameBuffer[MAX_PATH];
    TCHAR CatalogName[MAX_PATH];
    TCHAR *p;
    DWORD Err, CatalogNodeStatus, ReturnStatus;
    SetupapiVerifyProblem Problem;
    LPCTSTR ProblemFile;
    BOOL DeleteOemInfOnError;
    BOOL OriginalNameDifferent;
    LPCTSTR AltCatalogFile;
    LONG CatStringId;
    ULONG RequiredSize;
    DWORD InfVerifyType;
    DWORD SCOIFlags;

//
// Define values used to indicate how validation should be done on the INFs.
//
#define VERIFY_INF_AS_OEM       0  // verify solely against the specific
                                   // catalog referenced by the INF

#define VERIFY_INF_AS_SYSTEM    1  // verify globally (using all catalogs)

#define VERIFY_OEM_INF_GLOBALLY 2  // verify OEM INF globally, but remember the
                                   // original error, in case copy operations
                                   // are queued using media descriptor info
                                   // within this INF.


    MYASSERT((Flags & (VERCAT_INSTALL_INF_AND_CAT | VERCAT_NO_PROMPT_ON_ERROR))
             != (VERCAT_INSTALL_INF_AND_CAT | VERCAT_NO_PROMPT_ON_ERROR)
            );

    MYASSERT(!DeviceInfNewlyCopied || DeviceInfFinalName);

    if(Queue->Flags & FQF_DID_CATALOGS_OK) {
        //
        // If the caller wants information about the primary device INF, then
        // find the applicable catalog node.
        //
        if(DeviceInfFinalName) {
            for(CatalogNode=Queue->CatalogList; CatalogNode; CatalogNode=CatalogNode->Next) {

                if(CatalogNode->Flags & CATINFO_FLAG_PRIMARY_DEVICE_INF) {
                    MYASSERT(CatalogNode->InfFinalPath != -1);
                    InfFullPath = pSetupStringTableStringFromId(Queue->StringTable, CatalogNode->InfFinalPath);
                    lstrcpy(DeviceInfFinalName, InfFullPath);
                    if(DeviceInfNewlyCopied) {
                        *DeviceInfNewlyCopied = (CatalogNode->Flags & CATINFO_FLAG_NEWLY_COPIED);
                    }
                }
            }
        }

        return NO_ERROR;
    }

    if(Queue->Flags & FQF_DID_CATALOGS_FAILED) {
        //
        // Scan the catalog nodes until we find the first one that failed
        // verification, and return that failure code.
        //
        for(CatalogNode=Queue->CatalogList; CatalogNode; CatalogNode=CatalogNode->Next) {

            if(CatalogNode->VerificationFailureError != NO_ERROR) {
                return CatalogNode->VerificationFailureError;
            }
        }

        //
        // We didn't find a failed catalog node in our catalog list--something's
        // seriously wrong!
        //
        MYASSERT(0);
        return ERROR_INVALID_DATA;
    }

    //
    // If the queue has an alternate default catalog file associated with it,
    // then retrieve that catalog's name for use later.
    //
    AltCatalogFile = (Queue->AltCatalogFile != -1)
                   ? pSetupStringTableStringFromId(Queue->StringTable, Queue->AltCatalogFile)
                   : NULL;

    Queue->hWndDriverSigningUi = Owner;
    ReturnStatus = NO_ERROR;

    for(CatalogNode=Queue->CatalogList; CatalogNode; CatalogNode=CatalogNode->Next) {
        //
        // Assume success for verification of this catalog node.
        //
        CatalogNodeStatus = NO_ERROR;

        MYASSERT(CatalogNode->InfFullPath != -1);
        InfFullPath = pStringTableStringFromId(Queue->StringTable, CatalogNode->InfFullPath);

        if(Queue->Flags & FQF_USE_ALT_PLATFORM) {
            //
            // We have an alternate platform override, so use the alternate
            // platform's CatalogFile= entry.
            //
            CatStringId = CatalogNode->AltCatalogFileFromInf;
        } else {
            //
            // We're running native--use the native CatalogFile= entry.
            //
            CatStringId = CatalogNode->CatalogFileFromInf;
        }
        CatName = (CatStringId != -1)
                  ? pStringTableStringFromId(Queue->StringTable, CatStringId)
                  : NULL;

        InfVerifyType = pSetupInfIsFromOemLocation(InfFullPath, TRUE)
                      ? VERIFY_INF_AS_OEM
                      : VERIFY_INF_AS_SYSTEM;

        if(InfVerifyType == VERIFY_INF_AS_OEM) {
            //
            // If the caller wants us to, we'll now install the catalog.  In
            // addition, if it's a (native platform) device installation, we'll
            // install the INF as well.
            //
            // (Note: we specify the 'no overwrite' switch so that we won't blow
            // away any existing PNF source path information for this INF.
            // We'll only consider an OEM INF to match up with an existing
            // %windir%\Inf\Oem*.INF entry if the catalogs also match up, so
            // we're not going to get into any trouble doing this.
            //
            if(Flags & VERCAT_INSTALL_INF_AND_CAT) {
                //
                // If we're not doing a device install, then we want to suppress
                // popups and error log entries if the INF doesn't reference a
                // catalog.  This is because we want to allow such INFs to be
                // validated globally, unless they subsequently try to copy files.
                //
                SCOIFlags = (Queue->Flags & FQF_DEVICE_INSTALL)
                          ? 0
                          : SCOI_NO_ERRLOG_ON_MISSING_CATALOG;

                //
                // If we're not supposed to generate popups/log entries at all
                // for signature verification failures (e.g., because we've
                // already done so previously), then set that flag as well.
                //
                if(Queue->Flags & FQF_DIGSIG_ERRORS_NOUI) {
                    SCOIFlags |= SCOI_NO_UI_ON_SIGFAIL;
                }

                if(Queue->Flags & FQF_KEEP_INF_AND_CAT_ORIGINAL_NAMES) {
                    SCOIFlags |= SCOI_KEEP_INF_AND_CAT_ORIGINAL_NAMES;
                }

                if(Queue->Flags & FQF_ABORT_IF_UNSIGNED) {
                    SCOIFlags |= SCOI_ABORT_IF_UNSIGNED;
                }

                if(_SetupCopyOEMInf(InfFullPath,
                                    NULL, // default source location to where INF presently is
                                    ((Flags & VERCAT_PRIMARY_DEVICE_INF_FROM_INET)
                                        ? SPOST_URL
                                        : SPOST_PATH),
                                    (((Queue->Flags & (FQF_DEVICE_INSTALL | FQF_USE_ALT_PLATFORM)) == FQF_DEVICE_INSTALL)
                                        ? SP_COPY_NOOVERWRITE
                                        : SP_COPY_NOOVERWRITE | SP_COPY_OEMINF_CATALOG_ONLY),
                                    PathBuffer,
                                    SIZECHARS(PathBuffer),
                                    NULL,
                                    &p,
                                    ((CatalogNode->InfOriginalName != -1)
                                        ? pStringTableStringFromId(Queue->StringTable,
                                                                   CatalogNode->InfOriginalName)
                                        : pSetupGetFileTitle(InfFullPath)),
                                    CatName,
                                    Owner,
                                    ((Queue->DeviceDescStringId == -1)
                                        ? NULL
                                        : pStringTableStringFromId(Queue->StringTable,
                                                                   Queue->DeviceDescStringId)),
                                    Queue->DriverSigningPolicy,
                                    SCOIFlags,
                                    AltCatalogFile,
                                    ((Queue->Flags & FQF_USE_ALT_PLATFORM)
                                        ? &(Queue->AltPlatformInfo)
                                        : Queue->ValidationPlatform),
                                    &Err,
                                    CatalogNode->CatalogFilenameOnSystem,
                                    Queue->LogContext,
                                    &(Queue->hCatAdmin))) {
                    //
                    // If Err indicates that there was a digital signature
                    // problem that the user chose to ignore (or was silently
                    // ignored), then set a flag in the queue indicating the
                    // user should not be warned about subsequent failures.
                    // Don't set this flag if the queue's policy is "Ignore",
                    // however, on the chance that the policy might be altered
                    // later, and we'd want the user to get informed on any
                    // subsequent errors.
                    //
                    // (Note: if the error was due to the INF not having a
                    // CatalogFile= entry, and if we're supposed to ignore such
                    // problems, then just set the flag to do global validation
                    // later.)
                    //
                    if((Err == ERROR_NO_CATALOG_FOR_OEM_INF) &&
                       (SCOIFlags & SCOI_NO_ERRLOG_ON_MISSING_CATALOG)) {

                        InfVerifyType = VERIFY_OEM_INF_GLOBALLY;

                    } else if((Err != NO_ERROR) && (Queue->DriverSigningPolicy != DRIVERSIGN_NONE)) {

                        Queue->Flags |= FQF_DIGSIG_ERRORS_NOUI;
                    }

                    if(*PathBuffer) {
                        //
                        // Store the INF's final path into our catalog node.
                        // This will be under %windir%\Inf unless the INF didn't
                        // specify a CatalogFile= entry and we did an alternate
                        // catalog installation (i.e., because the file queue had
                        // an associated alternate catalog).
                        //
                        CatalogNode->InfFinalPath = pSetupStringTableAddString(
                                                        Queue->StringTable,
                                                        PathBuffer,
                                                        STRTAB_CASE_INSENSITIVE | STRTAB_BUFFER_WRITEABLE
                                                       );
                    } else {
                        //
                        // _SetupCopyOEMInf returned an empty string for the
                        // destination INF name, which means that we were doing
                        // a catalog-only install, and it didn't find the INF
                        // already existing in %windir%\Inf.  In this case, just
                        // use the INF's original pathname as its final pathname.
                        //
                        CatalogNode->InfFinalPath = CatalogNode->InfFullPath;
                    }

                    if(CatalogNode->InfFinalPath == -1) {

                        CatalogNodeStatus = ERROR_NOT_ENOUGH_MEMORY;
                        if(Err == NO_ERROR) {
                            Err = CatalogNodeStatus;
                        }

                        //
                        // Since we couldn't add this filename to the string
                        // table, we won't be able to undo this copy later--it
                        // must be done here.  Delete the INF, PNF, and CAT.
                        //
                        // NOTE: we should never get here if we did an alternate
                        // catalog file-only install, because in that case our
                        // new INF name is the same as the INF's original name,
                        // thus the string is already in the buffer and there's
                        // no way we could run out of memory.
                        //
                        MYASSERT(lstrcmpi(PathBuffer, InfFullPath));

                        pSetupUninstallOEMInf(PathBuffer,
                                              Queue->LogContext,
                                              SUOI_FORCEDELETE,
                                              NULL
                                             );

                    } else {
                        //
                        // Set a flag in the catalog node indicating that this
                        // INF was newly-copied into %windir%\Inf.  If the
                        // string ID for our INF's original name and that of its
                        // new name are equal, then we know we did an alternate
                        // catalog installation only, and we don't want to set
                        // this flag.
                        //
                        if(CatalogNode->InfFinalPath != CatalogNode->InfFullPath) {
                            CatalogNode->Flags |= CATINFO_FLAG_NEWLY_COPIED;
                        }

                        //
                        // If this is the primary device INF, and the caller
                        // requested information about that INF's final
                        // pathname, then store that information in the caller-
                        // supplied buffer(s) now.
                        //
                        if(DeviceInfFinalName &&
                           (CatalogNode->Flags & CATINFO_FLAG_PRIMARY_DEVICE_INF)) {
                            //
                            // We'd better not just've done an alternate catalog
                            // installation.
                            //
                            MYASSERT(CatalogNode->InfFinalPath != CatalogNode->InfFullPath);

                            lstrcpy(DeviceInfFinalName, PathBuffer);
                            if(DeviceInfNewlyCopied) {
                                *DeviceInfNewlyCopied = TRUE;
                            }
                        }
                    }

                } else {

                    CatalogNodeStatus = GetLastError();
                    MYASSERT(CatalogNodeStatus != NO_ERROR);

                    if(CatalogNodeStatus == ERROR_FILE_EXISTS) {
                        //
                        // INF and CAT already there--this isn't a failure.
                        //
                        // Store the name under which we found this OEM INF into
                        // the catalog node's InfFinalPath field.
                        //
                        CatalogNode->InfFinalPath = pSetupStringTableAddString(
                                                        Queue->StringTable,
                                                        PathBuffer,
                                                        STRTAB_CASE_INSENSITIVE | STRTAB_BUFFER_WRITEABLE
                                                       );

                        if(CatalogNode->InfFinalPath == -1) {
                            CatalogNodeStatus = ERROR_NOT_ENOUGH_MEMORY;
                        } else {
                            CatalogNodeStatus = NO_ERROR;
                            //
                            // If Err indicates that there was a digital signature
                            // problem that the user chose to ignore (or was silently
                            // ignored), then set a flag in the queue indicating the
                            // user should not be warned about subsequent failures.
                            // Don't set this flag if the queue's policy is "Ignore",
                            // however, on the chance that the policy might be altered
                            // later, and we'd want the user to get informed on any
                            // subsequent errors.
                            //
                            if((Err != NO_ERROR) && Queue->DriverSigningPolicy != DRIVERSIGN_NONE) {
                                Queue->Flags |= FQF_DIGSIG_ERRORS_NOUI;
                            }

                            //
                            // If this is the primary device INF, and the caller
                            // requested information about that INF's final
                            // pathname, then store that information in the caller-
                            // supplied buffer(s) now.
                            //
                            if(DeviceInfFinalName &&
                               (CatalogNode->Flags & CATINFO_FLAG_PRIMARY_DEVICE_INF)) {

                                lstrcpy(DeviceInfFinalName, PathBuffer);
                                if(DeviceInfNewlyCopied) {
                                    *DeviceInfNewlyCopied = FALSE;
                                }
                            }
                        }

                    } else if(CatalogNodeStatus == ERROR_SET_SYSTEM_RESTORE_POINT) {
                        //
                        // We should only get this error if the queue flag is
                        // set that causes us to abort unsigned installations.
                        //
                        MYASSERT(Queue->Flags & FQF_ABORT_IF_UNSIGNED);

                        //
                        // We don't want the user to see the driver signing
                        // UI again when the queue is re-committed...
                        //
                        if(Queue->DriverSigningPolicy != DRIVERSIGN_NONE) {
                            Queue->Flags |= FQF_DIGSIG_ERRORS_NOUI;
                        }

                        //
                        // Make sure that Err is also set to this same
                        // "special" error code...
                        //
                        Err = CatalogNodeStatus;
                    }

                    //
                    // If we had a real failure from _SetupCopyOEMInf (or we're
                    // out of memory and couldn't add a string to the string
                    // table above), then we need to propagate the value of
                    // CatalogNodeStatus to Err, if Err doesn't already have a
                    // failure code.
                    //
                    if((CatalogNodeStatus != NO_ERROR) && (Err == NO_ERROR)) {
                        Err = CatalogNodeStatus;
                    }
                }
            } else {
                //
                // We were told not to copy any files, but we've encountered an
                // OEM INF that needs to be installed. Hence, we have a failure.
                // Note that we _don't_ look to see if this OEM INF (and its
                // corresponding catalog) might happen to already be properly
                // installed.  That isn't necessary, because
                // _SetupDiInstallDevice calls _SetupVerifyQueuedCatalogs with
                // the VERCAT_INSTALL_INF_AND_CAT flag _before_ calling
                // SetupScanFileQueue, thus all INFs/CATs should be present when
                // we're called to do simple verification of the catalog nodes.
                //
                Err = CatalogNodeStatus = ERROR_CANNOT_COPY;
            }

        }

        if(InfVerifyType != VERIFY_INF_AS_OEM) {
            //
            // Inf is in system location (%windir%\Inf), or we're going to try
            // validating an "OEM" INF globally. Figure out the expected name
            // of the catalog file. If the file was originally copied in by the
            // Di stuff, then we need to use a name based on the name Di gave
            // the inf. Otherwise we use the name from the inf's CatalogFile=
            // entry, if present.  Finally, if the INF doesn't specify a
            // CatalogFile= entry, we assume it's a system component and
            // attempt to validate against any catalog that we find a hash
            // match in.
            //
            Err = NO_ERROR; // assume success

            if(CatalogNode->InfOriginalName != -1) {

                RequiredSize = SIZECHARS(InfNameBuffer);
                if(pSetupStringTableStringFromIdEx(Queue->StringTable,
                                             CatalogNode->InfOriginalName,
                                             InfNameBuffer,
                                             &RequiredSize)) {

                    OriginalNameDifferent = TRUE;
                } else {
                    //
                    // This should never fail!
                    //
                    MYASSERT(0);
                    Err = ERROR_INVALID_DATA;
                }

            } else {
                OriginalNameDifferent = FALSE;
            }

            if(Err == NO_ERROR) {

                if(CatName) {
                    //
                    // If there is a catalog name, then we'd better not be
                    // doing our "verify OEM INF globally" trick!
                    //
                    MYASSERT(InfVerifyType == VERIFY_INF_AS_SYSTEM);

                    if(OriginalNameDifferent) {
                        //
                        // If the INF specified a catalog file, then we know we
                        // would've installed that catalog file using a name based
                        // on the unique name we assigned the INF when copying it
                        // into the INF directory.
                        //
                        lstrcpy(CatalogName, pSetupGetFileTitle(InfFullPath));
                        p = _tcsrchr(CatalogName, TEXT('.'));
                        if(!p) {
                            p = CatalogName + lstrlen(CatalogName);
                        }
                        lstrcpy(p, pszCatSuffix);
                    } else {
                        lstrcpy(CatalogName, CatName);
                    }

                } else {
                    //
                    // This system INF didn't specify a CatalogFile= entry.  If
                    // an alternate catalog is associated with this file queue,
                    // then use that catalog for verification.
                    //
                    if(AltCatalogFile) {
                        lstrcpy(CatalogName, AltCatalogFile);
                        CatName = pSetupGetFileTitle(CatalogName);
                    }
                }

                //
                // (Note: in the call below, we don't want to store the
                // validating catalog filename in our CatalogFilenameOnSystem
                // field if the INF didn't specify a CatalogFile= entry (and
                // there was no alternate catalog specified), because we want
                // any queue nodes that reference this catalog entry to use
                // global validation as well.)
                //
                if(GlobalSetupFlags & PSPGF_MINIMAL_EMBEDDED) {
                    //
                    // Don't attempt to call _VerifyFile, because we're
                    // asking for the validating catalog's name, and that makes
                    // no sense in the "minimal embedded" case.
                    //
                    *(CatalogNode->CatalogFilenameOnSystem) = TEXT('\0');

                    //
                    // (Err is already set to NO_ERROR.)
                    //

                } else {

                    if(!CatName) {
                        *(CatalogNode->CatalogFilenameOnSystem) = TEXT('\0');
                    }

                    Err = _VerifyFile(
                              Queue->LogContext,
                              &(Queue->hCatAdmin),
                              NULL,
                              (CatName ? CatalogName : NULL),
                              NULL,
                              0,
                              (OriginalNameDifferent ? InfNameBuffer : pSetupGetFileTitle(InfFullPath)),
                              InfFullPath,
                              &Problem,
                              PathBuffer,
                              FALSE,
                              ((Queue->Flags & FQF_USE_ALT_PLATFORM)
                                  ? &(Queue->AltPlatformInfo)
                                  : Queue->ValidationPlatform),
                              (VERIFY_FILE_IGNORE_SELFSIGNED
                               | VERIFY_FILE_USE_OEM_CATALOGS
                               | VERIFY_FILE_NO_DRIVERBLOCKED_CHECK),
                              (CatName ? CatalogNode->CatalogFilenameOnSystem : NULL),
                              NULL,
                              NULL,
                              NULL
                             );
                }
            }

            if(Err == NO_ERROR) {
                //
                // INF/CAT was successfully verified--store the INF's final path
                // (which is the same as its current path) into the catalog
                // node.
                //
                CatalogNode->InfFinalPath = CatalogNode->InfFullPath;

            } else {

                if(Problem != SetupapiVerifyCatalogProblem) {

                    MYASSERT(Problem != SetupapiVerifyNoProblem);
                    //
                    // If the problem was not a catalog problem, then it's an
                    // INF problem (the _VerifyFile routine doesn't know the
                    // file we passed it is an INF).
                    //
                    Problem = SetupapiVerifyInfProblem;
                }
                ProblemFile = PathBuffer;

                if((Flags & VERCAT_NO_PROMPT_ON_ERROR)
                   || (Queue->Flags & FQF_QUEUE_FORCE_BLOCK_POLICY)) {
                    //
                    // Don't notify the caller or log anything--just remember
                    // the error.
                    //
                    CatalogNodeStatus = Err;

                } else {
                    //
                    // Notify the caller of the failure (based on policy).
                    //
                    if(pSetupHandleFailedVerification(
                           Owner,
                           Problem,
                           ProblemFile,
                           ((Queue->DeviceDescStringId == -1)
                               ? NULL
                               : pStringTableStringFromId(Queue->StringTable, Queue->DeviceDescStringId)),
                           Queue->DriverSigningPolicy,
                           Queue->Flags & FQF_DIGSIG_ERRORS_NOUI,
                           Err,
                           Queue->LogContext,
                           NULL,
                           NULL))
                    {
                        //
                        // If the caller wants a chance to set a system restore
                        // point prior to doing any unsigned installations,
                        // then we abort now with a "special" error code that
                        // tells them what to do...
                        //
                        if(Queue->Flags & FQF_ABORT_IF_UNSIGNED) {
                            //
                            // We don't want the user to see the driver signing
                            // UI again when the queue is re-committed...
                            //
                            if(Queue->DriverSigningPolicy != DRIVERSIGN_NONE) {
                                Queue->Flags |= FQF_DIGSIG_ERRORS_NOUI;
                            }

                            CatalogNodeStatus = Err = ERROR_SET_SYSTEM_RESTORE_POINT;

                        } else {
                            //
                            // Set a flag in the queue that indicates the user has been
                            // informed of a signature problem with this queue, and has
                            // elected to go ahead and install anyway.  Don't set this
                            // flag if the queue's policy is "Ignore", on the chance
                            // that the policy might be altered later, and we'd want the
                            // user to get informed on any subsequent errors.
                            //
                            if(Queue->DriverSigningPolicy != DRIVERSIGN_NONE) {
                                Queue->Flags |= FQF_DIGSIG_ERRORS_NOUI;
                            }

                            //
                            // Since we're going to use the INF/CAT anyway, in spite of
                            // digital signature problems, then we need to set the INF's
                            // final path to be the same as its current path.
                            //
                            CatalogNode->InfFinalPath = CatalogNode->InfFullPath;
                        }

                    } else {
                        //
                        // The caller doesn't want to proceed.
                        //
                        CatalogNodeStatus = Err;
                    }
                }
            }

            if(CatalogNodeStatus == NO_ERROR) {
                //
                // If this is the primary device INF, and the caller requested
                // information about that INF's final pathname, then store that
                // information in the caller-supplied buffer(s) now.
                //
                if(DeviceInfFinalName &&
                   (CatalogNode->Flags & CATINFO_FLAG_PRIMARY_DEVICE_INF)) {

                    lstrcpy(DeviceInfFinalName, InfFullPath);
                    if(DeviceInfNewlyCopied) {
                        *DeviceInfNewlyCopied = FALSE;
                    }
                }
            }
        }

        if(Err == NO_ERROR) {
            //
            // If we successfully validated an "OEM" INF globally, then we want
            // to remember this fact.  This will allow us to generate a
            // signature verification failure against any file copy nodes
            // associated with this catalog node.
            //
            if(InfVerifyType == VERIFY_OEM_INF_GLOBALLY) {
                CatalogNode->VerificationFailureError = ERROR_NO_CATALOG_FOR_OEM_INF;
            } else {
                CatalogNode->VerificationFailureError = NO_ERROR;
            }

        } else {
            CatalogNode->VerificationFailureError = Err;
            CatalogNode->CatalogFilenameOnSystem[0] = TEXT('\0');
        }

        if((ReturnStatus == NO_ERROR) && (CatalogNodeStatus != NO_ERROR)) {
            //
            // First critical error we've encountered--propagate the failure
            // for this catalog to our return status that will be returned to
            // the caller once we've finished looking at all the catalogs.
            //
            ReturnStatus = CatalogNodeStatus;

            //
            // Unless the VERCAT_NO_PROMPT_ON_ERROR flag has been set, we
            // should abort right now--there's no since in going any further.
            //
            if(!(Flags & VERCAT_NO_PROMPT_ON_ERROR)) {
                break;
            }
        }
    }

    //
    // If the caller requested no prompting, then we don't want to mark this
    // queue as 'failed', since the user never heard about it.  However, if the
    // verification succeeded, then we _do_ want to mark it as successful.
    //
    if(Flags & VERCAT_NO_PROMPT_ON_ERROR) {

        if(ReturnStatus == NO_ERROR) {
            Queue->Flags |= FQF_DID_CATALOGS_OK;
        }

    } else {

        Queue->Flags |= (ReturnStatus == NO_ERROR) ? FQF_DID_CATALOGS_OK
                                                   : FQF_DID_CATALOGS_FAILED;
    }

    return ReturnStatus;
}

VOID
LogFailedVerification(
    IN PSETUP_LOG_CONTEXT LogContext,           OPTIONAL
    IN DWORD MessageId,
    IN DWORD Error,
    IN LPCTSTR ProblemFile,
    IN LPCTSTR DeviceDesc                       OPTIONAL
    )

/*++

Routine Description:

    This routine logs when a verification failed but the file was installed
    anyway.

Arguments:

    LogContext - optionally supplies a pointer to the context for logging.
        If this is not supplied, errors will be logged to the default context.

    MessageId - Message to display

    Error - supplies the code the the error that caused the failure.

    ProblemFile - supplies the file path to the file associated with
        the problem. In some cases this is a full path, in others it's just a
        filename. The caller decides which makes sense in a particular
        scenario. For example, a system catalog is in some funky directory
        and there is no need to tell the user the full path. But in the case
        where a catalog comes from an oem location, there might be some benefit
        to telling the user the full path.

    DeviceDesc - Optionally, supplies the device description to be used in the
        digital signature verification error dialogs that may be popped up.

Return Value:

    NONE.

--*/

{
    PSETUP_LOG_CONTEXT lc = NULL;

    MYASSERT(Error != NO_ERROR);
    MYASSERT(ProblemFile != NULL);

    if (!LogContext) {
        if (CreateLogContext(NULL, TRUE, &lc) == NO_ERROR) {
            //
            // success
            //
            LogContext = lc;
        } else {
            lc = NULL;
        }
    }

    //
    // a device install failed
    //
    WriteLogEntry(
        LogContext,
        SETUP_LOG_ERROR | SETUP_LOG_BUFFER,
        MessageId,
        NULL,
        ProblemFile,
        DeviceDesc);

    WriteLogError(
        LogContext,
        SETUP_LOG_ERROR,
        Error);

    if (lc) {
        DeleteLogContext(lc);
    }
}

BOOL
pSetupHandleFailedVerification(
    IN HWND                  Owner,
    IN SetupapiVerifyProblem Problem,
    IN LPCTSTR               ProblemFile,
    IN LPCTSTR               DeviceDesc,          OPTIONAL
    IN DWORD                 DriverSigningPolicy,
    IN BOOL                  NoUI,
    IN DWORD                 Error,
    IN PVOID                 LogContext,          OPTIONAL
    OUT PDWORD               Flags,               OPTIONAL
    IN LPCTSTR               TargetFile           OPTIONAL
    )

/*++

Routine Description:

    This routine deals with a failed verification.

    System policy is checked. If the policy is block, UI is displayed telling
    the user that they're hosed. If the policy is ask-user, then ui is
    displayed requesting the user's decision about whether to ignore the
    verification failure and take the risk. If the policy is ignore, nothing
    is done.

Arguments:

    Owner - supplies window to own the dialog.

    Problem - supplies a constant indicating what caused the failure. There are
        5 cases:

        Catalog, meaning that the catalog could not be verified

        CatalogInstall, some other problem occured (e.g., INF directory write-
        protected, thus we couldn't install placeholder INF)

        Inf, meaning that an inf could not be verified or installed, etc.

        File, meaning that some random other file failed verification.

        DriverBlocked, meaning the driver was in the bad driver database.

    ProblemFile - supplies the file path to the file associated with
        the problem. In some cases this is a full path, in others it's just a
        filename. The caller decides which makes sense in a particular
        scenario. For example, a system catalog is in some funky directory
        and there is no need to tell the user the full path. But in the case
        where a catalog comes from an oem location, there might be some benefit
        to telling the user the full path.
        NOTE: if this API is being called because of a blocked driver then a
        full path should always be passed in.

    DeviceDesc - Optionally, supplies the device description to be used in the
        digital signature verification error dialogs that may be popped up.

    DriverSigningPolicy - supplies the driver signing policy currently in
        effect.  May be one of the three following values:

        DRIVERSIGN_NONE    -  silently succeed installation of unsigned/
                              incorrectly-signed files.  A PSS log entry will
                              be generated, however.
        DRIVERSIGN_WARNING -  warn the user, but let them choose whether or not
                              they still want to install the problematic file.
                              If the user elects to proceed with the
                              installation,  A PSS log entry will be generated
                              noting this fact.
        DRIVERSIGN_BLOCKING - do not allow the file to be installed

    NoUI - if TRUE, then a dialog box should not be displayed to the user, even
        if policy is warn or block.  This will typically be set to TRUE after
        the user has previously been informed of a digital signature problem
        with the package they're attempting to install, but have elected to
        proceed with the installation anyway.  The behavior of the "Yes" button,
        then, is really a "yes to all".

    Error - supplies the code of the error that caused the failure.

    LogContext - optionally supplies a pointer to the context for logging.
        If this is not supplied, errors will be logged to the default context.
        This is declared as a PVOID so external functions don't need to know
        what a SETUP_LOG_CONTEXT is.

    Flags - optionally supplies a pointer to a DWORD that receives one or more
        of the following file queue node flags indicating that we made an
        exemption for installing a protected system file:

        IQF_TARGET_PROTECTED - TargetFile (see below) is a protected system
                               file.
        IQF_ALLOW_UNSIGNED   - An exception has been granted so that TargetFile
                               (see below) may be replaced by an unsigned file.

    TargetFile - optionally supplies a pointer to a string that specifies a
       destination file if one exists.  This is only used if we want to exempt
       a file operation on this file.  If this parameter is not specified, then
       it is assumed the file will _not_ be replaced (i.e., it may already be
       on the system in its unsigned state), and no SFP exemption will be
       attempted.

Return Value:

    Boolean value indicating whether the caller should continue.
    If FALSE, then the current operation should be aborted, as the combination
    of system policy and user input indicated that the risk should not
    be taken.

--*/

{
    BOOL b;
    INT_PTR iRes;
    CERT_PROMPT CertPrompt;
    HANDLE hDialogEvent = NULL;

    //
    // If we're running non-interactive, then we always silently block,
    // regardless of policy.
    //
    if(GlobalSetupFlags & PSPGF_NONINTERACTIVE) {
        //
        // SPLOG -- log a PSS entry recording this event.
        //
        if (Problem == SetupapiVerifyDriverBlocked) {
            LogFailedVerification(
                (PSETUP_LOG_CONTEXT) LogContext,
                DeviceDesc ? MSG_LOG_DRIVER_BLOCKED_FOR_DEVICE_ERROR_NONINTERACTIVE : MSG_LOG_DRIVER_BLOCKED_ERROR_NONINTERACTIVE,
                Error,
                ProblemFile,
                DeviceDesc);
        } else {
            LogFailedVerification(
                (PSETUP_LOG_CONTEXT) LogContext,
                DeviceDesc ? MSG_LOG_DRIVER_SIGNING_ERROR_NONINTERACTIVE : MSG_LOG_SIGNING_ERROR_NONINTERACTIVE,
                Error,
                ProblemFile,
                DeviceDesc);
        }

        return FALSE;
    }

    if (GuiSetupInProgress) {
        hDialogEvent = CreateEvent(NULL,TRUE,FALSE,SETUP_HAS_OPEN_DIALOG_EVENT);
    }

#ifdef UNICODE
    if (Problem == SetupapiVerifyDriverBlocked) {
        //
        // Handle a driver block failure.
        // only applicable to UNICODE
        // ANSI won't report this problem code
        //
        HSDB hSDBDrvMain = NULL;
        TAGREF tagref = TAGREF_NULL;
        DRIVERBLOCK_PROMPT DriverBlockPrompt = {0};

        //
        // Never continue if the driver is in the bad driver database!
        //
        b = FALSE;

        LogFailedVerification(
            (PSETUP_LOG_CONTEXT) LogContext,
            DeviceDesc ? MSG_LOG_DRIVER_BLOCKED_FOR_DEVICE_ERROR : MSG_LOG_DRIVER_BLOCKED_ERROR,
            Error,
            ProblemFile,
            DeviceDesc);

        if (!(GlobalSetupFlags & PSPGF_UNATTENDED_SETUP)) {
            //
            // Show the driver blocking UI
            //
            DriverBlockPrompt.lpszFile = (TargetFile != NULL)
                                         ? TargetFile
                                         : ProblemFile;

            if ((hSDBDrvMain = SdbInitDatabase(SDB_DATABASE_MAIN_DRIVERS, NULL))) {
                HANDLE hFile = INVALID_HANDLE_VALUE;

                //
                // We are probably dealing with a temp file name at this point,
                // so we need to get a file handle to pass to SdbGetDatabaseMatch
                // along with the final destination file name.
                //
                hFile = CreateFile(ProblemFile,
                                   GENERIC_READ,
                                   FILE_SHARE_READ,
                                   NULL,
                                   OPEN_EXISTING,
                                   0,
                                   NULL
                                  );
                if (hFile != INVALID_HANDLE_VALUE) {
                    //
                    // Pass the TargetFile (the destination filename) to
                    // SdbGetDatabaseMatch because that will be what is
                    // in the bad driver database.
                    //
                    tagref = SdbGetDatabaseMatch(hSDBDrvMain,
                                                 (TargetFile != NULL)
                                                   ? pSetupGetFileTitle(TargetFile)
                                                   : ProblemFile,
                                                 hFile,
                                                 NULL,
                                                 0);

                    if (tagref != TAGREF_NULL) {
                        SdbReadDriverInformation(hSDBDrvMain,
                                                 tagref,
                                                 &(DriverBlockPrompt.entryinfo));
                    }

                    CloseHandle(hFile);
                }

                SdbReleaseDatabase(hSDBDrvMain);
            }

            //
            // Always call the dialog code, even if we could access the database.
            //
            iRes =  DialogBoxParam(MyDllModuleHandle,
                                   MAKEINTRESOURCE(IDD_DRIVERBLOCK),
                                   IsWindow(Owner) ? Owner : NULL,
                                   DriverBlockDlgProc,
                                   (LPARAM)&DriverBlockPrompt
                                   );
        }
    } else
#endif
    {
        //
        // Handle a digital signature failure.
        //
        // If the policy is block, then the user always gets informed of a problem
        // (i.e., there is no "yes" option, hence no "yes to all" semantics).
        //
        MYASSERT((DriverSigningPolicy != DRIVERSIGN_BLOCKING) || !NoUI);

        CertPrompt.lpszDescription = DeviceDesc;
        CertPrompt.lpszFile = ProblemFile;
        CertPrompt.ProblemType = Problem;
        CertPrompt.DriverSigningPolicy = DriverSigningPolicy;

        switch(DriverSigningPolicy) {

            case DRIVERSIGN_NONE :

                //
                // SPLOG -- log a PSS entry recording this event.
                //
                LogFailedVerification(
                    (PSETUP_LOG_CONTEXT) LogContext,
                    DeviceDesc ? MSG_LOG_DRIVER_SIGNING_ERROR_POLICY_NONE : MSG_LOG_SIGNING_ERROR_POLICY_NONE,
                    Error,
                    ProblemFile,
                    DeviceDesc);
                //
                // If requested, find out if the file is protected (we may need to
                // skip it if it's being queued up for delayed copy).
                //
                if(Flags && TargetFile) {

                    if(IsFileProtected(TargetFile,
                                       (PSETUP_LOG_CONTEXT)LogContext,
                                       NULL)) {

                        *Flags = IQF_TARGET_PROTECTED;
                    }
                }

                b = TRUE;
                goto exit;

            case DRIVERSIGN_WARNING :
                if(NoUI) {
                    //
                    // SPLOG -- log a PSS entry recording this event.
                    //
                    LogFailedVerification(
                        (PSETUP_LOG_CONTEXT) LogContext,
                        DeviceDesc ? MSG_LOG_DRIVER_SIGNING_ERROR_AUTO_YES : MSG_LOG_SIGNING_ERROR_AUTO_YES,
                        Error,
                        ProblemFile,
                        DeviceDesc);
                    iRes = IDC_VERIFY_WARN_YES;
                } else if(GlobalSetupFlags & PSPGF_UNATTENDED_SETUP) {
                    //
                    // SPLOG -- log a PSS entry recording this event.
                    //
                    LogFailedVerification(
                        (PSETUP_LOG_CONTEXT) LogContext,
                        DeviceDesc ? MSG_LOG_DRIVER_SIGNING_ERROR_AUTO_NO : MSG_LOG_SIGNING_ERROR_AUTO_NO,
                        Error,
                        ProblemFile,
                        DeviceDesc);
                    iRes = IDC_VERIFY_WARN_NO;
                } else {
                    if (hDialogEvent) {
                       SetEvent(hDialogEvent);
                    }
                    iRes =  DialogBoxParam(MyDllModuleHandle,
                                           CertPrompt.lpszDescription ?
                                               MAKEINTRESOURCE(IDD_DEVICE_VERIFY_WARNING) :
                                               MAKEINTRESOURCE(IDD_SOFTWARE_VERIFY_WARNING),
                                           IsWindow(Owner) ? Owner : NULL,
                                           CertifyDlgProc,
                                           (LPARAM)&CertPrompt
                                          );
                    LogFailedVerification(
                        (PSETUP_LOG_CONTEXT) LogContext,
                        DeviceDesc
                            ?(iRes == IDC_VERIFY_WARN_YES ? MSG_LOG_DRIVER_SIGNING_ERROR_WARN_YES : MSG_LOG_DRIVER_SIGNING_ERROR_WARN_NO)
                            :(iRes == IDC_VERIFY_WARN_YES ? MSG_LOG_SIGNING_ERROR_WARN_YES : MSG_LOG_SIGNING_ERROR_WARN_NO),
                        Error,
                        ProblemFile,
                        DeviceDesc);
                }
                break;

        case DRIVERSIGN_BLOCKING :

                if(GlobalSetupFlags & PSPGF_UNATTENDED_SETUP) {
                    //
                    // During UNATTENDED, we block silently
                    //
                    LogFailedVerification(
                        (PSETUP_LOG_CONTEXT) LogContext,
                        DeviceDesc ? MSG_LOG_DRIVER_SIGNING_ERROR_SILENT_BLOCK : MSG_LOG_SIGNING_ERROR_SILENT_BLOCK,
                        Error,
                        ProblemFile,
                        DeviceDesc);
                    iRes = IDC_VERIFY_BLOCK_OK;
                } else {
                    LogFailedVerification(
                        (PSETUP_LOG_CONTEXT) LogContext,
                        DeviceDesc ? MSG_LOG_DRIVER_SIGNING_ERROR_POLICY_BLOCK : MSG_LOG_SIGNING_ERROR_POLICY_BLOCK,
                        Error,
                        ProblemFile,
                        DeviceDesc);

                    if (hDialogEvent) {
                        SetEvent(hDialogEvent);
                    }
                    iRes =  DialogBoxParam(MyDllModuleHandle,
                                           CertPrompt.lpszDescription ?
                                               MAKEINTRESOURCE(IDD_DEVICE_VERIFY_BLOCK) :
                                               MAKEINTRESOURCE(IDD_SOFTWARE_VERIFY_BLOCK),
                                           IsWindow(Owner) ? Owner : NULL,
                                           CertifyDlgProc,
                                           (LPARAM)&CertPrompt
                                          );
                }
                break;

            default :
                //
                // We don't know about any other policy values!
                //
                MYASSERT(0);
                b = FALSE;
                goto exit;
        }

        switch(iRes) {

            case IDC_VERIFY_WARN_NO:
            case IDC_VERIFY_BLOCK_OK:
                b = FALSE;
                break;

            case IDC_VERIFY_WARN_YES:
                if(TargetFile) {
                    pSetupExemptFileFromProtection(TargetFile,
                                                   (DWORD) -1,
                                                   (PSETUP_LOG_CONTEXT)LogContext,
                                                   Flags
                                                  );
                }

                b = TRUE;
                break;

            default:
                //
                // Shouldn't get any other values.
                //
                MYASSERT(0);
                b = FALSE;
        }
    }

exit:
    if (hDialogEvent) {
        ResetEvent(hDialogEvent);
        CloseHandle(hDialogEvent);
    }


    return b;
}


INT_PTR
CALLBACK
CertifyDlgProc(
    IN HWND hwnd,
    IN UINT msg,
    IN WPARAM wParam,
    IN LPARAM lParam
    )

/*++

Routine Description:

    This is the dialog procedure for the driver signing UI that is presented to
    the user when a verification failure is encountered.  This dialog handles
    both the 'warn' and 'block' cases.

--*/

{
    UINT MessageLen;
    LOGFONT LogFont;
    HFONT hFontBold = NULL;
    HICON hIcon = NULL;

    PCERT_PROMPT lpCertPrompt;

    lpCertPrompt = (PCERT_PROMPT)GetWindowLongPtr(hwnd, DWLP_USER);

    switch(msg) {

        case WM_INITDIALOG:
            SetWindowLongPtr(hwnd, DWLP_USER, lParam);
            MessageBeep(MB_ICONASTERISK);
            lpCertPrompt = (PCERT_PROMPT)lParam;

            //
            // If lpszDescription is not NULL then this is the device verify
            // warning dialog, otherwise it is the software warning dialog.
            //
            if(lpCertPrompt->lpszDescription != NULL) {
                SetDlgItemText(hwnd, IDC_VERIFY_FILENAME, lpCertPrompt->lpszDescription);
                SetDlgText(hwnd, IDC_VERIFY_BOLD, IDS_DEVICE_VERIFY_MSG1, IDS_DEVICE_VERIFY_MSG2);
            } else {
                SetDlgText(hwnd, IDC_VERIFY_BOLD, IDS_SOFTWARE_VERIFY_MSG1, IDS_SOFTWARE_VERIFY_MSG2);
            }

            //
            // Create the bold font and bold any necessary text.
            //
            hFontBold = (HFONT)SendMessage(GetDlgItem(hwnd, IDC_VERIFY_BOLD),
                                           WM_GETFONT, 0, 0);
            GetObject(hFontBold, sizeof(LogFont), &LogFont);
            LogFont.lfWeight = FW_BOLD;
            hFontBold = CreateFontIndirect(&LogFont);
            if (hFontBold) {
                SetWindowFont(GetDlgItem(hwnd, IDC_VERIFY_BOLD), hFontBold, TRUE);
            }

            //
            // Set the appropriate warning or error icon.
            //
            hIcon = LoadIcon(NULL,
                            (lpCertPrompt->DriverSigningPolicy == DRIVERSIGN_WARNING) ?
                                IDI_WARNING :
                                IDI_ERROR
                            );
            SendDlgItemMessage(hwnd, IDC_VERIFY_ICON, STM_SETICON, (WPARAM)hIcon, 0L);

            //
            // The link won't work in GUI mode setup since help center has not yet
            // been installed, so we will just show the static text instead.
            //
            ShowWindow(GetDlgItem(hwnd, IDC_VERIFY_TESTING_LINK), !GuiSetupInProgress);
            ShowWindow(GetDlgItem(hwnd, IDC_VERIFY_TESTING_TEXT), GuiSetupInProgress);

            //
            // If we are in GUI mode setup then we want to change the text of
            // the buttons to be "Yes" and "No".  We also add the following line
            // of text: "Do you want to continue installing the software for
            // this hardware?"
            //
            ShowWindow(GetDlgItem(hwnd, IDC_VERIFY_SETUP_TEXT), GuiSetupInProgress);

            if (GuiSetupInProgress) {
                TCHAR szButtonText[MAX_PATH];

                if (LoadString(MyDllModuleHandle, IDS_YES, szButtonText, SIZECHARS(szButtonText))) {
                    SetDlgItemText(hwnd, IDC_VERIFY_WARN_YES, szButtonText);
                }

                if (LoadString(MyDllModuleHandle, IDS_NO, szButtonText, SIZECHARS(szButtonText))) {
                    SetDlgItemText(hwnd, IDC_VERIFY_WARN_NO, szButtonText);
                }
            }

            //
            // Make sure this dialog is in the foreground (at least for this
            // process).
            //
            SetForegroundWindow(hwnd);

            if (lpCertPrompt->DriverSigningPolicy == DRIVERSIGN_WARNING) {
                SetFocus(GetDlgItem(hwnd, IDC_VERIFY_WARN_NO));
            }

            return FALSE;

        case WM_DESTROY:
            if (hFontBold) {
                DeleteObject(hFontBold);
                hFontBold = NULL;
            }

            if (hIcon) {
                DestroyIcon(hIcon);
            }
            break;

        case WM_NOTIFY:
            switch (((NMHDR FAR *)lParam)->code) {
            case NM_RETURN:
            case NM_CLICK:
                ShellExecute(hwnd,
                             TEXT("open"),
                             TEXT("HELPCTR.EXE"),
                             TEXT("HELPCTR.EXE -url hcp://services/subsite?node=TopLevelBucket_4/Hardware&topic=MS-ITS%3A%25HELP_LOCATION%25%5Csysdm.chm%3A%3A/logo_testing.htm"),
                             NULL,
                             SW_SHOWNORMAL
                             );
                break;
            }
            break;

        case WM_COMMAND:
            switch(wParam) {

                case IDC_VERIFY_WARN_NO:
                case IDC_VERIFY_WARN_YES:
                case IDC_VERIFY_BLOCK_OK:
                    EndDialog(hwnd, (int)wParam);
                    break;

                default:
                    break;
            }

            break;

        default:
            break;
    }

    return FALSE;
}

#ifdef UNICODE
INT_PTR
CALLBACK
DriverBlockDlgProc(
    IN HWND hwnd,
    IN UINT msg,
    IN WPARAM wParam,
    IN LPARAM lParam
    )

/*++

Routine Description:

    This is the dialog procedure for the driver blocking UI that is presented to
    the user when a a driver that is about to be installed is found in the bad
    driver database.

--*/

{
    UINT MessageLen;
    HICON hIcon = NULL;
    LPTSTR pBuffer = NULL;
    ULONG BufferSize;
    static HAPPHELPINFOCONTEXT hAppHelpInfoContext = NULL;
    static SDBENTRYINFO SdbEntryInfo;

    PDRIVERBLOCK_PROMPT lpDriverBlockPrompt;

    lpDriverBlockPrompt = (PDRIVERBLOCK_PROMPT)GetWindowLongPtr(hwnd, DWLP_USER);

    switch(msg) {

        case WM_INITDIALOG:
            SetWindowLongPtr(hwnd, DWLP_USER, lParam);
            MessageBeep(MB_ICONASTERISK);
            lpDriverBlockPrompt = (PDRIVERBLOCK_PROMPT)lParam;

            hIcon = LoadIcon(MyDllModuleHandle,
                             MAKEINTRESOURCE(IDI_DRIVERBLOCK));

            SendDlgItemMessage(hwnd, IDC_DRIVERBLOCK_ICON, STM_SETICON, (WPARAM)hIcon, 0L);

            hAppHelpInfoContext = SdbOpenApphelpInformation(&lpDriverBlockPrompt->entryinfo.guidDB,
                                                            &lpDriverBlockPrompt->entryinfo.guidID);

            if ((hAppHelpInfoContext) &&
                ((BufferSize = SdbQueryApphelpInformation(hAppHelpInfoContext,
                                                         ApphelpAppName,
                                                         NULL,
                                                         0)) != 0) &&
                (pBuffer = MyMalloc(BufferSize)) &&
                ((BufferSize = SdbQueryApphelpInformation(hAppHelpInfoContext,
                                                         ApphelpAppName,
                                                         pBuffer,
                                                         BufferSize)) != 0)) {
                SetDlgItemText(hwnd, IDC_DRIVERBLOCK_APPNAME, pBuffer);
                MyFree(pBuffer);
            } else if (lpDriverBlockPrompt->lpszFile) {
                SetDlgItemText(hwnd, IDC_DRIVERBLOCK_APPNAME, pSetupGetFileTitle(lpDriverBlockPrompt->lpszFile));
            }

            if ((hAppHelpInfoContext) &&
                ((BufferSize = SdbQueryApphelpInformation(hAppHelpInfoContext,
                                                         ApphelpDetails,
                                                         NULL,
                                                         0)) != 0) &&
                (pBuffer = MyMalloc(BufferSize)) &&
                ((BufferSize = SdbQueryApphelpInformation(hAppHelpInfoContext,
                                                         ApphelpDetails,
                                                         pBuffer,
                                                         BufferSize)) != 0)) {

                SetDlgItemText(hwnd, IDC_DRIVERBLOCK_SUMMARY, pBuffer);
                MyFree(pBuffer);
            }

            //
            // Make sure this dialog is in the foreground (at least for this
            // process).
            //
            SetForegroundWindow(hwnd);
            return FALSE;

        case WM_DESTROY:
            if (hIcon) {
                DestroyIcon(hIcon);
            }
            if (hAppHelpInfoContext) {
                SdbCloseApphelpInformation(hAppHelpInfoContext);
            }
            break;

        case WM_COMMAND:
            switch(LOWORD(wParam)) {
            case IDCANCEL:
                EndDialog(hwnd, (int)wParam);
                break;

            case IDC_DRIVERBLOCK_DETAILS:
                if (hAppHelpInfoContext) {

                    BufferSize = SdbQueryApphelpInformation(hAppHelpInfoContext,
                                                                  ApphelpHelpCenterURL,
                                                                  NULL,
                                                                  0);

                    if (BufferSize && (pBuffer = MyMalloc(BufferSize + (lstrlen(TEXT("HELPCTR.EXE -url ")) * sizeof(TCHAR))))) {
                        lstrcpy(pBuffer, TEXT("HELPCTR.EXE -url "));

                        BufferSize = SdbQueryApphelpInformation(hAppHelpInfoContext,
                                                                ApphelpHelpCenterURL,
                                                                (PVOID)&pBuffer[lstrlen(TEXT("HELPCTR.EXE -url "))],
                                                                BufferSize);

                        if (BufferSize) {
                            ShellExecute(hwnd,
                                         TEXT("open"),
                                         TEXT("HELPCTR.EXE"),
                                         pBuffer,
                                         NULL,
                                         SW_SHOWNORMAL);
                        }

                        MyFree(pBuffer);
                    }
                }
                break;

            default:
                break;
            }

            break;

        default:
            break;
    }

    return FALSE;
}
#endif


DWORD
pGetInfOriginalNameAndCatalogFile(
    IN  PLOADED_INF             Inf,                     OPTIONAL
    IN  LPCTSTR                 CurrentName,             OPTIONAL
    OUT PBOOL                   DifferentName,           OPTIONAL
    OUT LPTSTR                  OriginalName,            OPTIONAL
    IN  DWORD                   OriginalNameSize,
    OUT LPTSTR                  OriginalCatalogName,     OPTIONAL
    IN  DWORD                   OriginalCatalogNameSize,
    IN  PSP_ALTPLATFORM_INFO_V2 AltPlatformInfo          OPTIONAL
    )

/*++

Routine Description:

    This routine determines whether a specified inf once had a different
    original name, such as in the case where the Di stuff copied and renamed a
    device inf.

    (Information about an INF's original name comes from the PNF.)

    This routine can also optionally return the original name of the catalog
    file for this INF.

Arguments:

    Inf - optionally, supplies a pointer to a LOADED_INF whose original name
        and catalog file are to be queried.  If this parameter isn't specified,
        then CurrentName must be specified.

    CurrentName - optionally, supplies the path to the INF whose original name
        is to be queried.  If Inf parameter is specified, this parameter is
        ignored.

    DifferentName - optionally, supplies the address of a boolean variable that,
        upon successful return, is set to TRUE if the INF's current name is
        different than its original name.

    OriginalName - if this routine returns successfully, and the DifferentName
        boolean was set to TRUE, then this optional buffer receives
        the INF's original name, which _will not_ be the same as the current
        name.

    OriginalNameSize - supplies size of buffer (bytes for ansi, chars for
        unicode) of OriginalName buffer, or zero if OriginalName is NULL.

    OriginalCatalogName - optionally, supplies a buffer that receives the
        original name of the catalog specified by this INF.  If the catalog
        doesn't specify a catalog file, this buffer will be set to an empty
        string.

    OriginalCatalogNameSize - supplies size, in characters, of
        OriginalCatalogName buffer (zero if buffer not supplied).

    AltPlatformInfo - optionally, supplies the address of a structure describing
        the platform parameters that should be used in formulating the decorated
        CatalogFile= entry to be used when searching for the INF's associated
        catalog file.

Return Value:

    If information is successfully retrieved from the INF, the return value is
    NO_ERROR.  Otherwise, it is a Win32 error code indicating the cause of
    failure.

--*/

{
    DWORD d;
    HINF hInf = INVALID_HANDLE_VALUE;

    MYASSERT((DifferentName && OriginalName && OriginalNameSize) ||
             !(DifferentName || OriginalName || OriginalNameSize));

    MYASSERT((OriginalCatalogName && OriginalCatalogNameSize) ||
             !(OriginalCatalogName || OriginalCatalogNameSize));

    MYASSERT(Inf || CurrentName);

    if(DifferentName) {
        *DifferentName = FALSE;
    }

    if(!Inf) {
        //
        // Open the INF.
        //
        hInf = SetupOpenInfFile(CurrentName,
                                NULL,
                                INF_STYLE_OLDNT | INF_STYLE_WIN4,
                                NULL
                               );

        if(hInf == INVALID_HANDLE_VALUE) {
            return GetLastError();
        }

        //
        // We don't need to lock the INF because it'll never be accessible
        // outside of this routine.
        //
        Inf = (PLOADED_INF)hInf;
    }

    //
    // Enclose in try/except in case we hit an inpage error while using this
    // memory-mapped image.
    //
    d = NO_ERROR;
    try {

        if(DifferentName) {
            if(Inf->OriginalInfName) {
                lstrcpyn(OriginalName, Inf->OriginalInfName, OriginalNameSize);
                *DifferentName = TRUE;
            }
        }

        if(OriginalCatalogName) {

            if(!pSetupGetCatalogFileValue(&(Inf->VersionBlock),
                                          OriginalCatalogName,
                                          OriginalCatalogNameSize,
                                          AltPlatformInfo)) {
                //
                // The INF didn't specify an associated catalog file
                //
                *OriginalCatalogName = TEXT('\0');
            }
        }

    } except(EXCEPTION_EXECUTE_HANDLER) {
        //
        // If we hit an AV, then use invalid parameter error, otherwise, assume
        // an inpage error when dealing with a mapped-in file.
        //
        d = (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) ? ERROR_INVALID_PARAMETER : ERROR_READ_FAULT;
    }

    if(hInf != INVALID_HANDLE_VALUE) {
        SetupCloseInfFile(hInf);
    }

    return d;
}


#ifdef UNICODE

PSECURITY_DESCRIPTOR
pSetupConvertTextToSD(
    IN PCWSTR SDS,
    OUT PULONG SecDescSize
    )
/*++

Routine Description:

    Helper for cfgmgr.lib

    Obtains a binary security descriptor from an SDS
    Resulting buffer must be free'd using LocalFree (not MyFree)
    returns NULL if not supported and sets last error

Arguments:

    SDS - string to obtain security descriptor from

    SecDescSize - filled in with size of security descriptor

Return Value:

    returns security descriptor (use LocalFree to release)
    or NULL with GetLastError indicating error

--*/
{
    SCESTATUS status;
    PSECURITY_DESCRIPTOR pSD = NULL;
    ULONG ulSDSize;
    SECURITY_INFORMATION siSeInfo;

    //
    // If we're in "Disable SCE" mode on embedded, don't do security stuff...
    //
    if(GlobalSetupFlags & PSPGF_NO_SCE_EMBEDDED) {
        SetLastError(ERROR_SCE_DISABLED);
        return NULL;
    }

    try {
        status = SceSvcConvertTextToSD((PWSTR)SDS,&pSD,&ulSDSize,&siSeInfo);
        switch (status ) {
            case SCESTATUS_SUCCESS:
                MYASSERT(pSD);
                MYASSERT(ulSDSize);
                if (SecDescSize) {
                    *SecDescSize = ulSDSize;
                }
                SetLastError(NO_ERROR);
                break;

            case SCESTATUS_INVALID_PARAMETER:
                SetLastError(ERROR_INVALID_PARAMETER);
                pSD = NULL;
                break;

            case SCESTATUS_NOT_ENOUGH_RESOURCE:
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                pSD = NULL;
                break;

            case SCESTATUS_RECORD_NOT_FOUND:
            default:
                SetLastError(ERROR_INVALID_DATA);
                pSD = NULL;
        }
    } except(EXCEPTION_EXECUTE_HANDLER) {
        //
        // If we hit an AV, then use invalid parameter error, otherwise, assume
        // an inpage error when dealing with a mapped-in file.
        //
        SetLastError(ERROR_INVALID_DATA);
        pSD = NULL;
    }
    return pSD;
}

PWSTR
pSetupConvertSDToText(
    IN PSECURITY_DESCRIPTOR SD,
    OUT PULONG pSDSSize
    )
/*++

Routine Description:

    Helper for cfgmgr.lib

    Obtains an SDS from a binary security descriptor
    Resulting buffer must be free'd using LocalFree (not MyFree)
    returns NULL if not supported and sets last error

Arguments:

    SD - security descriptor to convert to a string

    pSDSSize - return size of string

Return Value:

    returns security descriptor string (use LocalFree to release)
    or NULL with GetLastError indicating error

--*/
{
    HINSTANCE Dll_Handle;
    FARPROC SceFileProc;
    SCESTATUS status;
    DWORD LoadStatus;
    SECURITY_INFORMATION securityInformation = 0;
    PSID sid;
    PACL acl;
    BOOLEAN tmp,present;
    LPWSTR SDS = NULL;
    ULONG ulSSDSize;
    SECURITY_INFORMATION siSeInfo;

    //
    // If we're in "Disable SCE" mode on embedded, don't do security stuff...
    //
    if(GlobalSetupFlags & PSPGF_NO_SCE_EMBEDDED) {
        //
        // Report an empty string
        //
        return LocalAlloc(LPTR, sizeof(WCHAR)); // LPTR zeroes out the char
    }

    try {
        //
        // find out what relevent information is in the descriptor
        // up a securityInformation block to go with it.
        //

        status = RtlGetOwnerSecurityDescriptor(SD, &sid, &tmp);

        if(NT_SUCCESS(status) && (sid != NULL)) {
            securityInformation |= OWNER_SECURITY_INFORMATION;
        }

        status = RtlGetGroupSecurityDescriptor(SD, &sid, &tmp);

        if(NT_SUCCESS(status) && (sid != NULL)) {
            securityInformation |= GROUP_SECURITY_INFORMATION;
        }

        status = RtlGetSaclSecurityDescriptor(SD,
                                              &present,
                                              &acl,
                                              &tmp);

        if(NT_SUCCESS(status) && (present)) {
            securityInformation |= SACL_SECURITY_INFORMATION;
        }

        status = RtlGetDaclSecurityDescriptor(SD,
                                              &present,
                                              &acl,
                                              &tmp);

        if(NT_SUCCESS(status) && (present)) {
            securityInformation |= DACL_SECURITY_INFORMATION;
        }

        //
        // now obtain an SDS
        //
        status = SceSvcConvertSDToText(SD,securityInformation,&SDS,&ulSSDSize);
        switch (status ) {
            case SCESTATUS_SUCCESS:
                MYASSERT(SDS);
                MYASSERT(ulSSDSize);
                if(pSDSSize != NULL) {
                    *pSDSSize = ulSSDSize;
                }
                SetLastError(NO_ERROR);
                break;

            case SCESTATUS_INVALID_PARAMETER:
                SetLastError(ERROR_INVALID_PARAMETER);
                SDS = NULL;
                break;

            case SCESTATUS_NOT_ENOUGH_RESOURCE:
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                SDS = NULL;
                break;

            case SCESTATUS_RECORD_NOT_FOUND:
            default:
                SetLastError(ERROR_INVALID_DATA);
                SDS = NULL;
        }

    } except(EXCEPTION_EXECUTE_HANDLER) {
        if (SDS) {
            LocalFree(SDS);
        }
        SetLastError(ERROR_INVALID_DATA);
        SDS = NULL;
    }
    return SDS;
}

DWORD
pSetupCallSCE(
    IN DWORD Operation,
    IN PCWSTR FullName,
    IN PSP_FILE_QUEUE Queue,
    IN PCWSTR String1,
    IN DWORD Index1,
    IN PSECURITY_DESCRIPTOR SecDesc  OPTIONAL
    )
/*

    Operation ST_SCE_SET : - Sets security on a File in File Queue and informs SCE database
    FullName : - Filename (Needed)
    Queue    : - Pointer to FileQueue (Needed)
    Index    : - Index in String Table of Queue (Needed)

    Operation ST_SCE_RENAME : - Sets security on a File in File Queue and informs SCE database to
                                record it for the filename mentioned in String1
    FullName : - Filename (Needed)
    Queue    : - Pointer to FileQueue (Needed)
    String1  ; - Filename to record in Database (Needed)
    Index    : - Index in String Table of Queue (Optional - only if it needs to be set otherwise -1)

    Operation ST_SCE_DELETE : - Removes record of file in SCE database
    FullName : - Filename (Needed)

    Operation ST_SCE_UNWIND : - Used for Backup Unwinds when we reset the security on a dirty file
    FullName : - Filename (Needed)
    SecDesc  : - Pointer to Security Descriptor for the original file that we unwind (Needed)

    Operation ST_SCE_SERVICES : - Sets security on a Service and informs SCE database
    FullName : - Service Name (Needed)
    Index    : - Service Style (Needed)
    String1  ; - Security Descriptor string

    Operation ST_SCE_SDS_TO_BIN : - Sets security on a Service and informs SCE database
    FullName : - Service Name (Needed)
    Index    : - Service Style (Needed)
    String1  ; - Security Descriptor string

    In each case, return value is error or NO_ERROR
*/
{

    FARPROC SceFileProc;
    PCWSTR SecurityDescriptor;
    HINSTANCE Dll_Handle;
    DWORD ret, LoadStatus;

    //
    // If we're in "Disable SCE" mode on embedded, don't do security stuff...
    //
    if(GlobalSetupFlags & PSPGF_NO_SCE_EMBEDDED) {
        return NO_ERROR;
    }

    try {
        switch (Operation) {

            case ST_SCE_SET:

                //Get the Security descriptor from the String table of the node

                if( Index1 != -1 ){
                    SecurityDescriptor = pSetupStringTableStringFromId( Queue->StringTable, Index1 );

                    if(!SecurityDescriptor) {
                        ret= NO_ERROR;
                        break;
                    }
                }
                else {
                    ret = NO_ERROR;
                    break;
                }


                ret = SceSetupUpdateSecurityFile((PWSTR)FullName, 0, (PWSTR)SecurityDescriptor );
                break;

            case ST_SCE_RENAME:

                if( Index1 != -1 ) {
                    SecurityDescriptor = pSetupStringTableStringFromId( Queue->StringTable, Index1 );
                } else {
                    SecurityDescriptor = NULL;
                }

                ret = SceSetupMoveSecurityFile( (PWSTR)FullName, (PWSTR)String1, (PWSTR)SecurityDescriptor );
                break;



            case ST_SCE_DELETE:

                ret = SceSetupMoveSecurityFile( (PWSTR)FullName, NULL, NULL );
                break;


            case ST_SCE_UNWIND:

                ret = SceSetupUnwindSecurityFile( (PWSTR)FullName, SecDesc );
                break;

            case ST_SCE_SERVICES:

               if( String1 == NULL ){
                   ret = NO_ERROR;
               } else {
                   ret = SceSetupUpdateSecurityService( (PWSTR)FullName, Index1, (PWSTR)String1 );
               }
               break;

            default:
                MYASSERT(0);
                ret = ERROR_INVALID_DATA;
        }

    } except(EXCEPTION_EXECUTE_HANDLER) {
        ret = ERROR_INVALID_DATA;
    }
    return ret;
}

#endif  // UNICODE


VOID
RestoreBootReplacedFile(
    IN PSP_FILE_QUEUE      Queue,
    IN PSP_FILE_QUEUE_NODE QueueNode
    )
/*++

Routine Description:

    This routine restores a file that was renamed in preparation for a bootfile
    installation.

Arguments:

    Queue - queue that contains the bootfile copy operation

    QueueNode - bootfile copy operation being aborted

Return Value:

    None.

--*/
{
    DWORD rc;
    LONG TargetID;
    SP_TARGET_ENT TargetInfo;
    PCTSTR TargetFilename, RenamedFilename;
    BOOL UnPostSucceeded;

    //
    // First, we need to find the corresponding target info node so
    // we can find out what temporary name our file was renamed to.
    //
    rc = pSetupBackupGetTargetByPath((HSPFILEQ)Queue,
                                     NULL, // use Queue's string table
                                     NULL,
                                     QueueNode->TargetDirectory,
                                     -1,
                                     QueueNode->TargetFilename,
                                     &TargetID,
                                     &TargetInfo
                                    );

    if(rc == NO_ERROR) {
        //
        // Has the file previously been renamed (and not yet
        // restored)?
        //
        if((TargetInfo.InternalFlags & (SP_TEFLG_MOVED | SP_TEFLG_RESTORED)) == SP_TEFLG_MOVED) {

            TargetFilename = pSetupFormFullPath(
                                Queue->StringTable,
                                TargetInfo.TargetRoot,
                                TargetInfo.TargetSubDir,
                                TargetInfo.TargetFilename
                               );
            MYASSERT(TargetFilename);

            RenamedFilename = pSetupStringTableStringFromId(
                                Queue->StringTable,
                                TargetInfo.NewTargetFilename
                               );
            MYASSERT(RenamedFilename);

            //
            // Move the renamed file back to its original name.
            //
            RestoreRenamedOrBackedUpFile(TargetFilename,
                                         RenamedFilename,
                                         TRUE,
                                         Queue->LogContext
                                        );
            //
            // Set the flag indicating that this file has been
            // restored, and save this info.
            //
            TargetInfo.InternalFlags |= SP_TEFLG_RESTORED;
            pSetupBackupSetTargetByID((HSPFILEQ)Queue, TargetID, &TargetInfo);

            //
            // Finally, get rid of the delayed-move node that was to
            // delete the renamed file upon reboot.
            //
            UnPostSucceeded = UnPostDelayedMove(Queue,
                                                RenamedFilename,
                                                NULL
                                               );
            MYASSERT(UnPostSucceeded);
        }
    }
}


VOID
pSetupExemptFileFromProtection(
    IN  PCTSTR             FileName,
    IN  DWORD              FileChangeFlags,
    IN  PSETUP_LOG_CONTEXT LogContext,      OPTIONAL
    OUT PDWORD             QueueNodeFlags   OPTIONAL
    )
/*++

Routine Description:

    This routine checks to see if the specified file is a protected system
    file, and if so, it tells SFC to make a replacement exception for this file.

Arguments:

    FileName - Supplies the name of the file for which an exception is being
        requested.

    FileChangeFlags - Supplies the flags to be passed to SfcFileException, if
        this file is determined to be under the protection of SFP.

    LogContext - Optionally, supplies the log context to be used when logging
        information resulting from this request.

    QueueNodeFlags - Optionally, supplies the address of a variable that
        receives one or more of the following queue node flags indicating
        whether the specified file is a protected system file, and whether an
        exception was granted for its replacement:

        IQF_TARGET_PROTECTED - File is a protected system file.
        IQF_ALLOW_UNSIGNED   - An exception has been granted so that the file
                               may be replaced by an unsigned file.

Return Value:

    None.

--*/
{
#ifdef UNICODE
    HANDLE hSfp;
    PSETUP_LOG_CONTEXT lc = NULL;
    DWORD Result = NO_ERROR;

    if(QueueNodeFlags) {
        *QueueNodeFlags = 0;
    }

    //
    // If the caller didn't supply us with a LogContext, then create our own.
    // We want to do this so that all log entries generated herein will end up
    // in the same section.
    //
    if(!LogContext) {
        if(CreateLogContext(NULL, TRUE, &lc) == NO_ERROR) {
            //
            // success
            //
            LogContext = lc;
        } else {
            lc = NULL;
        }
    }

    if(IsFileProtected(FileName, LogContext, &hSfp)) {

        if(QueueNodeFlags) {
            *QueueNodeFlags = IQF_TARGET_PROTECTED;
        }

        Result = SfcFileException(hSfp,
                                  (PWSTR)FileName,
                                  FileChangeFlags
                                 );

        if(Result == NO_ERROR) {

            WriteLogEntry(
                LogContext,
                SETUP_LOG_ERROR,
                MSG_LOG_SFC_EXEMPT_SUCCESS,
                NULL,
                FileName);

            if(QueueNodeFlags) {
                *QueueNodeFlags |= IQF_ALLOW_UNSIGNED;
            }

        } else {
            WriteLogEntry(
                LogContext,
                SETUP_LOG_ERROR|SETUP_LOG_BUFFER,
                MSG_LOG_SFC_EXEMPT_FAIL,
                NULL,
                FileName);
            WriteLogError(
                LogContext,
                SETUP_LOG_ERROR,
                Result);
        }

        SfcClose(hSfp);

        //
        // If we created our own local LogContext, we can free it now.
        //
        if(lc) {
            DeleteLogContext(lc);
        }
    }
#else // no file protection on win9x
    if(QueueNodeFlags) {
        *QueueNodeFlags = 0;
    }
#endif
}


BOOL
pSetupProtectedRenamesFlag(
    BOOL bSet
    )
{
    HKEY hKey;
    long rslt = ERROR_SUCCESS;

    if (OSVersionInfo.dwPlatformId != VER_PLATFORM_WIN32_NT) {
        return(TRUE);
    }

    rslt = RegOpenKeyEx(
                 HKEY_LOCAL_MACHINE,
                 TEXT("System\\CurrentControlSet\\Control\\Session Manager"),
                 0,
                 KEY_SET_VALUE,
                 &hKey);

    if (rslt == ERROR_SUCCESS) {
        DWORD Value = bSet ? 1 : 0;
        rslt = RegSetValueEx(
                 hKey,
                 TEXT("AllowProtectedRenames"),
                 0,
                 REG_DWORD,
                 (LPBYTE)&Value,
                 sizeof(DWORD));

        RegCloseKey(hKey);

        if (rslt != ERROR_SUCCESS) {
            DebugPrintEx( DPFLTR_ERROR_LEVEL, TEXT("couldn't RegSetValueEx, ec = %d\n"), rslt );
        }

    } else {
        DebugPrintEx( DPFLTR_ERROR_LEVEL, TEXT("couldn't RegOpenKeyEx, ec = %d\n"), rslt );
    }

    return(rslt == ERROR_SUCCESS);

}


VOID
pSetupUninstallNewCatalogNodes(
    IN PSP_FILE_QUEUE     Queue,
    IN PSETUP_LOG_CONTEXT LogContext OPTIONAL
    )
/*++

Routine Description:

    This routine uninstalls any newly-copied INFs/PNFs/CATs contained in the
    specified linked list of catalog nodes.

Arguments:

    Queue - Supplies a pointer to the file queue (potentially) containing
        newly-copied catalog nodes to be uninstalled.

Return Value:

    None.

--*/
{
    PSPQ_CATALOG_INFO CatalogNode;
    PTSTR InfToUninstall;
    BOOL Locked = FALSE;

    try {

        if(!_pSpUtilsStringTableLock(Queue->StringTable)) {
            leave;
        }

        Locked = TRUE;

        for(CatalogNode = Queue->CatalogList;
            CatalogNode;
            CatalogNode = CatalogNode->Next) {

            if(CatalogNode->Flags & CATINFO_FLAG_NEWLY_COPIED) {

                InfToUninstall = _pSpUtilsStringTableStringFromId(
                                     Queue->StringTable,
                                     CatalogNode->InfFinalPath
                                     );

                MYASSERT(InfToUninstall);

                if(InfToUninstall) {
                    pSetupUninstallOEMInf(InfToUninstall, LogContext, SUOI_FORCEDELETE, NULL);
                }
            }
        }

    } except(EXCEPTION_EXECUTE_HANDLER) {
        //
        // Reference the following variable so the compiler will respect
        // statement ordering w.r.t. its assignment.
        //
        Locked = Locked;
    }

    if(Locked) {
        _pSpUtilsStringTableUnlock(Queue->StringTable);
    }
}


BOOL
WINAPI
SetupUninstallNewlyCopiedInfs(
    IN HSPFILEQ QueueHandle,
    IN DWORD Flags,
    IN PVOID Reserved
    )

/*++

Routine Description:

    This API uninstalls any INFs (and their associated PNFs and CATs) that
    were previously installed during committal of the specified file queue.

Arguments:

    QueueHandle - Supplies a handle to a committed file queue (potentially)
        containing newly-copied INFs to be uninstalled.

    Flags - Supplies flags that alter the behavior of this API.  Presently, no
        flags are defined.  This parameter must be zero.

    Reserved - Reserved for future use.  This parameter must be NULL.

Return Value:

    If all the parameters were valid, the return value is non-zero (TRUE). Note
    that this does _not_ necessarily mean that any newly-copied INFs were
    uninstalled.

    If there was a problem with the parameters passed in, the return value is
    FALSE, and GetLastError provides more information on the problem.

--*/

{
    PSP_FILE_QUEUE Queue;
    BOOL Success;
    PSETUP_LOG_CONTEXT LogContext;

    if(Flags) {
        SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }

    if(Reserved) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    //
    // Queue handle is actually a pointer to the queue structure.
    //
    Queue = (PSP_FILE_QUEUE)QueueHandle;

    //
    // do a quick handle validation before anything else
    //
    try {
        Success = ((Queue != NULL) && (Queue != INVALID_HANDLE_VALUE) && (Queue->Signature == SP_FILE_QUEUE_SIG));
        if(Success) {
            LogContext = Queue->LogContext;
        }
    } except(EXCEPTION_EXECUTE_HANDLER) {
        Success = FALSE;
    }

    if(!Success) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    pSetupUninstallNewCatalogNodes(Queue, LogContext);

    return TRUE;
}

