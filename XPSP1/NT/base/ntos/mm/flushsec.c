/*++

Copyright (c) 1990  Microsoft Corporation

Module Name:

   flushsec.c

Abstract:

    This module contains the routines which implement the
    NtFlushVirtualMemory service.

Author:

    Lou Perazzoli (loup) 8-May-1990
    Landy Wang (landyw) 02-June-1997

Revision History:

--*/

#include "mi.h"

PSUBSECTION
MiGetSystemCacheSubsection (
    IN PVOID BaseAddress,
    OUT PMMPTE *ProtoPte
    );

VOID
MiFlushDirtyBitsToPfn (
    IN PMMPTE PointerPte,
    IN PMMPTE LastPte,
    IN PEPROCESS Process,
    IN BOOLEAN SystemCache
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE,NtFlushVirtualMemory)
#pragma alloc_text(PAGE,MmFlushVirtualMemory)
#endif

extern POBJECT_TYPE IoFileObjectType;

NTSTATUS
NtFlushVirtualMemory (
    IN HANDLE ProcessHandle,
    IN OUT PVOID *BaseAddress,
    IN OUT PSIZE_T RegionSize,
    OUT PIO_STATUS_BLOCK IoStatus
    )

/*++

Routine Description:

    This function flushes a range of virtual address which map
    a data file back into the data file if they have been modified.

Arguments:

    ProcessHandle - Supplies an open handle to a process object.

    BaseAddress - Supplies a pointer to a variable that will receive
         the base address the flushed region.  The initial value
         of this argument is the base address of the region of the
         pages to flush.

    RegionSize - Supplies a pointer to a variable that will receive
         the actual size in bytes of the flushed region of pages.
         The initial value of this argument is rounded up to the
         next host-page-size boundary.

         If this value is specified as zero, the mapped range from
         the base address to the end of the range is flushed.

    IoStatus - Returns the value of the IoStatus for the last attempted
         I/O operation.

Return Value:

    Returns the status

    TBS


--*/

{
    PEPROCESS Process;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
    PVOID CapturedBase;
    SIZE_T CapturedRegionSize;
    IO_STATUS_BLOCK TemporaryIoStatus;

    PAGED_CODE();

    PreviousMode = KeGetPreviousMode();
    if (PreviousMode != KernelMode) {

        //
        // Establish an exception handler, probe the specified addresses
        // for write access and capture the initial values.
        //

        try {

            ProbeForWritePointer (BaseAddress);
            ProbeForWriteUlong_ptr (RegionSize);
            ProbeForWriteIoStatus (IoStatus);

            //
            // Capture the base address.
            //

            CapturedBase = *BaseAddress;

            //
            // Capture the region size.
            //

            CapturedRegionSize = *RegionSize;

        } except (EXCEPTION_EXECUTE_HANDLER) {

            //
            // If an exception occurs during the probe or capture
            // of the initial values, then handle the exception and
            // return the exception code as the status value.
            //

            return GetExceptionCode();
        }

    }
    else {

        //
        // Capture the base address.
        //

        CapturedBase = *BaseAddress;

        //
        // Capture the region size.
        //

        CapturedRegionSize = *RegionSize;

    }

    //
    // Make sure the specified starting and ending addresses are
    // within the user part of the virtual address space.
    //

    if (CapturedBase > MM_HIGHEST_USER_ADDRESS) {

        //
        // Invalid base address.
        //

        return STATUS_INVALID_PARAMETER_2;
    }

    if (((ULONG_PTR)MM_HIGHEST_USER_ADDRESS - (ULONG_PTR)CapturedBase) <
                                                        CapturedRegionSize) {

        //
        // Invalid region size;
        //

        return STATUS_INVALID_PARAMETER_2;

    }

    Status = ObReferenceObjectByHandle ( ProcessHandle,
                                         PROCESS_VM_OPERATION,
                                         PsProcessType,
                                         PreviousMode,
                                         (PVOID *)&Process,
                                         NULL );
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = MmFlushVirtualMemory (Process,
                                   &CapturedBase,
                                   &CapturedRegionSize,
                                   &TemporaryIoStatus);

    ObDereferenceObject (Process);

    //
    // Establish an exception handler and write the size and base
    // address.
    //

    try {

        *RegionSize = CapturedRegionSize;
        *BaseAddress = PAGE_ALIGN (CapturedBase);
        *IoStatus = TemporaryIoStatus;

    } except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return Status;

}


VOID
MiFlushAcquire (
    IN PCONTROL_AREA ControlArea
    )

/*++

Routine Description:

    This is a helper routine to reference count the control area if needed
    during a flush section call to prevent the section object from being
    deleted while the flush is ongoing.

Arguments:

    ControlArea - Supplies a pointer to the control area.

Return Value:

    None.

--*/

{
    KIRQL OldIrql;

    LOCK_PFN (OldIrql);

    ASSERT ((LONG)ControlArea->NumberOfMappedViews >= 1);
    ControlArea->NumberOfMappedViews += 1;

    UNLOCK_PFN (OldIrql);
}


VOID
MiFlushRelease (
    IN PCONTROL_AREA ControlArea
    )

/*++

Routine Description:

    This is a helper routine to release the control area reference needed
    during a flush section call.

Arguments:

    ControlArea - Supplies a pointer to the control area.

Return Value:

    None.

--*/

{
    KIRQL OldIrql;

    LOCK_PFN (OldIrql);

    ASSERT ((LONG)ControlArea->NumberOfMappedViews >= 1);
    ControlArea->NumberOfMappedViews -= 1;

    //
    // Check to see if the control area should be deleted.  This
    // will release the PFN lock.
    //

    MiCheckControlArea (ControlArea, NULL, OldIrql);
}


NTSTATUS
MmFlushVirtualMemory (
    IN PEPROCESS Process,
    IN OUT PVOID *BaseAddress,
    IN OUT PSIZE_T RegionSize,
    OUT PIO_STATUS_BLOCK IoStatus
    )

/*++

Routine Description:

    This function flushes a range of virtual address which map
    a data file back into the data file if they have been modified.

    Note that the modification is this process's view of the pages,
    on certain implementations (like the Intel 386), the modify
    bit is captured in the PTE and not forced to the PFN database
    until the page is removed from the working set.  This means
    that pages which have been modified by another process will
    not be flushed to the data file.

Arguments:

    Process - Supplies a pointer to a process object.

    BaseAddress - Supplies a pointer to a variable that will receive
                  the base address of the flushed region.  The initial value
                  of this argument is the base address of the region of the
                  pages to flush.

    RegionSize - Supplies a pointer to a variable that will receive
                 the actual size in bytes of the flushed region of pages.
                 The initial value of this argument is rounded up to the
                 next host-page-size boundary.

                 If this value is specified as zero, the mapped range from
                 the base address to the end of the range is flushed.

    IoStatus - Returns the value of the IoStatus for the last attempted
               I/O operation.

Return Value:

    NTSTATUS.

--*/

{
    PMMVAD Vad;
    PVOID EndingAddress;
    PVOID Va;
    PEPROCESS CurrentProcess;
    BOOLEAN SystemCache;
    PCONTROL_AREA ControlArea;
    PMMPTE PointerPte;
    PMMPTE PointerPde;
    PMMPTE PointerPpe;
    PMMPTE PointerPxe;
    PMMPTE LastPte;
    PMMPTE FinalPte;
    PSUBSECTION Subsection;
    PSUBSECTION LastSubsection;
    NTSTATUS Status;
    ULONG ConsecutiveFileLockFailures;
    ULONG Waited;
    LOGICAL EntireRestOfVad;
    LOGICAL Attached;
    KAPC_STATE ApcState;

    PAGED_CODE();

    Attached = FALSE;

    //
    // Determine if the specified base address is within the system
    // cache and if so, don't attach, the working set mutex is still
    // required to "lock" paged pool pages (proto PTEs) into the
    // working set.
    //

    EndingAddress = (PVOID)(((ULONG_PTR)*BaseAddress + *RegionSize - 1) |
                                                            (PAGE_SIZE - 1));
    *BaseAddress = PAGE_ALIGN (*BaseAddress);

    if (MI_IS_SESSION_ADDRESS (*BaseAddress)) {

        //
        // Nothing in session space needs flushing.
        //

        return STATUS_NOT_MAPPED_VIEW;
    }

    CurrentProcess = PsGetCurrentProcess ();

    if (!MI_IS_SYSTEM_CACHE_ADDRESS(*BaseAddress)) {

        SystemCache = FALSE;

        //
        // Attach to the specified process.
        //

        if (CurrentProcess != Process) {
            KeStackAttachProcess (&Process->Pcb, &ApcState);
            Attached = TRUE;
        }

        LOCK_ADDRESS_SPACE (Process);

        //
        // Make sure the address space was not deleted, if so, return an error.
        //

        if (Process->Flags & PS_PROCESS_FLAGS_VM_DELETED) {
            Status = STATUS_PROCESS_IS_TERMINATING;
            goto ErrorReturn;
        }

        Vad = MiLocateAddress (*BaseAddress);

        if (Vad == NULL) {

            //
            // No Virtual Address Descriptor located for Base Address.
            //

            Status = STATUS_NOT_MAPPED_VIEW;
            goto ErrorReturn;
        }

        if (*RegionSize == 0) {
            EndingAddress = MI_VPN_TO_VA_ENDING (Vad->EndingVpn);
            EntireRestOfVad = TRUE;
        }
        else {
            EntireRestOfVad = FALSE;
        }

        if ((Vad->u.VadFlags.PrivateMemory == 1) ||
            (MI_VA_TO_VPN (EndingAddress) > Vad->EndingVpn)) {

            //
            // This virtual address descriptor does not refer to a Segment
            // object.
            //

            Status = STATUS_NOT_MAPPED_VIEW;
            goto ErrorReturn;
        }

        //
        // Make sure this VAD maps a data file (not an image file).
        //

        ControlArea = Vad->ControlArea;

        if ((ControlArea->FilePointer == NULL) ||
             (Vad->u.VadFlags.ImageMap == 1)) {

            //
            // This virtual address descriptor does not refer to a Segment
            // object.
            //

            Status = STATUS_NOT_MAPPED_DATA;
            goto ErrorReturn;
        }

        LOCK_WS_UNSAFE (Process);
    }
    else {

        //
        // Initializing Vad, ControlArea and EntireRestOfVad is not needed for
        // correctness but without it the compiler cannot compile this code
        // W4 to check for use of uninitialized variables.
        //

        Vad = NULL;
        ControlArea = NULL;
        EntireRestOfVad = FALSE;

        SystemCache = TRUE;
        Process = CurrentProcess;
        LOCK_WS (Process);
    }

    PointerPxe = MiGetPxeAddress (*BaseAddress);
    PointerPpe = MiGetPpeAddress (*BaseAddress);
    PointerPde = MiGetPdeAddress (*BaseAddress);
    PointerPte = MiGetPteAddress (*BaseAddress);
    LastPte = MiGetPteAddress (EndingAddress);
    *RegionSize = (PCHAR)EndingAddress - (PCHAR)*BaseAddress + 1;

retry:

    while (!MiDoesPxeExistAndMakeValid (PointerPxe, Process, FALSE, &Waited)) {

        //
        // This page directory parent entry is empty, go to the next one.
        //

        PointerPxe += 1;
        PointerPpe = MiGetVirtualAddressMappedByPte (PointerPxe);
        PointerPde = MiGetVirtualAddressMappedByPte (PointerPpe);
        PointerPte = MiGetVirtualAddressMappedByPte (PointerPde);
        Va = MiGetVirtualAddressMappedByPte (PointerPte);

        if (PointerPte > LastPte) {
            break;
        }
    }

    while (!MiDoesPpeExistAndMakeValid (PointerPpe, Process, FALSE, &Waited)) {

        //
        // This page directory parent entry is empty, go to the next one.
        //

        PointerPpe += 1;
        PointerPxe = MiGetPteAddress (PointerPpe);
        PointerPde = MiGetVirtualAddressMappedByPte (PointerPpe);
        PointerPte = MiGetVirtualAddressMappedByPte (PointerPde);
        Va = MiGetVirtualAddressMappedByPte (PointerPte);

        if (PointerPte > LastPte) {
            break;
        }
#if (_MI_PAGING_LEVELS >= 4)
        if (MiIsPteOnPdeBoundary (PointerPpe)) {
            goto retry;
        }
#endif
    }

    Waited = 0;

    if (PointerPte <= LastPte) {
        while (!MiDoesPdeExistAndMakeValid(PointerPde, Process, FALSE, &Waited)) {

            //
            // No page table page exists for this address.
            //

            PointerPde += 1;

            PointerPte = MiGetVirtualAddressMappedByPte (PointerPde);

            if (PointerPte > LastPte) {
                break;
            }

#if (_MI_PAGING_LEVELS >= 3)
            if (MiIsPteOnPdeBoundary (PointerPde)) {

                if (MiIsPteOnPpeBoundary (PointerPde)) {
                    PointerPxe = MiGetPdeAddress (PointerPde);
                }
                PointerPpe = MiGetPteAddress (PointerPde);
                goto retry;
            }
#endif

            Va = MiGetVirtualAddressMappedByPte (PointerPte);
        }

        //
        // If the PFN lock (and accordingly the WS mutex) was
        // released and reacquired we must retry the operation.
        //

        if ((PointerPte <= LastPte) && (Waited != 0)) {
            goto retry;
        }
    }

    MiFlushDirtyBitsToPfn (PointerPte, LastPte, Process, SystemCache);

    if (SystemCache) {

        //
        // No VADs exist for the system cache.
        //

        UNLOCK_WS (Process);

        Subsection = MiGetSystemCacheSubsection (*BaseAddress, &PointerPte);

        LastSubsection = MiGetSystemCacheSubsection (EndingAddress, &FinalPte);

        //
        // Flush the PTEs from the specified section.
        //

        Status = MiFlushSectionInternal (PointerPte,
                                         FinalPte,
                                         Subsection,
                                         LastSubsection,
                                         FALSE,
                                         TRUE,
                                         IoStatus);
    }
    else {

        //
        // Protect against the section being prematurely deleted.
        //

        MiFlushAcquire (ControlArea);

        PointerPte = MiGetProtoPteAddress (Vad, MI_VA_TO_VPN (*BaseAddress));
        Subsection = MiLocateSubsection (Vad, MI_VA_TO_VPN(*BaseAddress));
        LastSubsection = MiLocateSubsection (Vad, MI_VA_TO_VPN(EndingAddress));

        //
        // The last subsection is NULL if the section is not fully 
        // committed.  Only allow the flush if the caller said do the whole
        // thing, otherwise it's an error.
        //

        if (LastSubsection == NULL) {

            if (EntireRestOfVad == FALSE) {

                //
                // Caller can only specify the range that is committed or zero
                // to indicate the entire range.
                //

                UNLOCK_WS_AND_ADDRESS_SPACE (Process);
                if (Attached == TRUE) {
                    KeUnstackDetachProcess (&ApcState);
                }
                MiFlushRelease (ControlArea);
                return STATUS_NOT_MAPPED_VIEW;
            }

            LastSubsection = Subsection;
            while (LastSubsection->NextSubsection) {
                LastSubsection = LastSubsection->NextSubsection;
            }

            //
            // A memory barrier is needed to read the subsection chains
            // in order to ensure the writes to the actual individual
            // subsection data structure fields are visible in correct
            // order.  This avoids the need to acquire any stronger
            // synchronization (ie: PFN lock), thus yielding better
            // performance and pagability.
            //

            KeMemoryBarrier ();

            FinalPte = LastSubsection->SubsectionBase + LastSubsection->PtesInSubsection - 1;
        }
        else {
            FinalPte = MiGetProtoPteAddress (Vad, MI_VA_TO_VPN (EndingAddress));
        }

        UNLOCK_WS_AND_ADDRESS_SPACE (Process);
        if (Attached == TRUE) {
            KeUnstackDetachProcess (&ApcState);
        }

        //
        // Preacquire the file to synchronize the flush.
        //

        ConsecutiveFileLockFailures = 0;

        do {

            Status = FsRtlAcquireFileForCcFlushEx (ControlArea->FilePointer);

            if (!NT_SUCCESS(Status)) {
                break;
            }

            //
            // Flush the PTEs from the specified section.
            //

            Status = MiFlushSectionInternal (PointerPte,
                                             FinalPte,
                                             Subsection,
                                             LastSubsection,
                                             TRUE,
                                             TRUE,
                                             IoStatus);

            //
            // Release the file we acquired.
            //

            FsRtlReleaseFileForCcFlush (ControlArea->FilePointer);

            //
            // Only try the request more than once if the filesystem told us
            // it had a deadlock.
            //

            if (Status != STATUS_FILE_LOCK_CONFLICT) {
                break;
            }

            ConsecutiveFileLockFailures += 1;
            KeDelayExecutionThread (KernelMode, FALSE, (PLARGE_INTEGER)&MmShortTime);

        } while (ConsecutiveFileLockFailures < 5);

        MiFlushRelease (ControlArea);
    }

    return Status;

ErrorReturn:

    ASSERT (SystemCache == FALSE);

    UNLOCK_ADDRESS_SPACE (Process);

    if (Attached == TRUE) {
        KeUnstackDetachProcess (&ApcState);
    }
    return Status;

}

NTSTATUS
MmFlushSection (
    IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
    IN PLARGE_INTEGER Offset,
    IN SIZE_T RegionSize,
    OUT PIO_STATUS_BLOCK IoStatus,
    IN ULONG AcquireFile
    )

/*++

Routine Description:

    This function flushes to the backing file any modified pages within
    the specified range of the section.

Arguments:

    SectionObjectPointer - Supplies a pointer to the section objects.

    Offset - Supplies the offset into the section in which to begin
             flushing pages.  If this argument is not present, then the
             whole section is flushed without regard to the region size
             argument.

    RegionSize - Supplies the size in bytes to flush.  This is rounded
                 to a page multiple.

    IoStatus - Returns the value of the IoStatus for the last attempted
               I/O operation.

    AcquireFile - Nonzero if the callback should be used to acquire the file.

Return Value:

    Returns status of the operation.

--*/

{
    PCONTROL_AREA ControlArea;
    PMMPTE PointerPte;
    PMMPTE LastPte;
    KIRQL OldIrql;
    ULONG PteOffset;
    ULONG LastPteOffset;
    PSUBSECTION Subsection;
    PSUBSECTION TempSubsection;
    PSUBSECTION LastSubsection;
    PSUBSECTION LastSubsectionWithProtos;
    PMAPPED_FILE_SEGMENT Segment;
    PETHREAD CurrentThread;
    NTSTATUS status;
    BOOLEAN OldClusterState;
    ULONG ConsecutiveFileLockFailures;

    //
    // Initialize IoStatus for success, in case we take an early exit.
    //

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = RegionSize;

    LOCK_PFN (OldIrql);

    ControlArea = ((PCONTROL_AREA)(SectionObjectPointer->DataSectionObject));

    ASSERT ((ControlArea == NULL) || (ControlArea->u.Flags.Image == 0));

    if ((ControlArea == NULL) ||
        (ControlArea->u.Flags.BeingDeleted) ||
        (ControlArea->u.Flags.BeingCreated) ||
        (ControlArea->u.Flags.Rom) ||
        (ControlArea->NumberOfPfnReferences == 0)) {

        //
        // This file no longer has an associated segment or is in the
        // process of coming or going.
        // If the number of PFN references is zero, then this control
        // area does not have any valid or transition pages that need
        // to be flushed.
        //

        UNLOCK_PFN (OldIrql);
        return STATUS_SUCCESS;
    }

    //
    // Locate the subsection.
    //

    ASSERT (ControlArea->u.Flags.Image == 0);
    ASSERT (ControlArea->u.Flags.GlobalOnlyPerSession == 0);
    ASSERT (ControlArea->u.Flags.PhysicalMemory == 0);

    Subsection = (PSUBSECTION)(ControlArea + 1);

    if (!ARGUMENT_PRESENT (Offset)) {

        //
        // If the offset is not specified, flush the complete file ignoring
        // the region size.
        //

        ASSERT (ControlArea->FilePointer != NULL);

        PteOffset = 0;

        LastSubsection = Subsection;

        Segment = (PMAPPED_FILE_SEGMENT) ControlArea->Segment;

        if (MmIsAddressValid (Segment)) {
            if (Segment->LastSubsectionHint != NULL) {
                LastSubsection = (PSUBSECTION) Segment->LastSubsectionHint;
            }
        }

        while (LastSubsection->NextSubsection != NULL) {
            LastSubsection = LastSubsection->NextSubsection;
        }

        LastPteOffset = LastSubsection->PtesInSubsection - 1;
    }
    else {

        PteOffset = (ULONG)(Offset->QuadPart >> PAGE_SHIFT);

        //
        // Make sure the PTEs are not in the extended part of the segment.
        //

        while (PteOffset >= Subsection->PtesInSubsection) {
            PteOffset -= Subsection->PtesInSubsection;
            if (Subsection->NextSubsection == NULL) {

                //
                // Past end of mapping, just return success.
                //

                UNLOCK_PFN (OldIrql);
                return STATUS_SUCCESS;
            }
            Subsection = Subsection->NextSubsection;
        }

        ASSERT (PteOffset < Subsection->PtesInSubsection);

        //
        // Locate the address of the last prototype PTE to be flushed.
        //

        LastPteOffset = PteOffset + (ULONG)(((RegionSize + BYTE_OFFSET(Offset->LowPart)) - 1) >> PAGE_SHIFT);

        LastSubsection = Subsection;

        while (LastPteOffset >= LastSubsection->PtesInSubsection) {
            LastPteOffset -= LastSubsection->PtesInSubsection;
            if (LastSubsection->NextSubsection == NULL) {
                LastPteOffset = LastSubsection->PtesInSubsection - 1;
                break;
            }
            LastSubsection = LastSubsection->NextSubsection;
        }

        ASSERT (LastPteOffset < LastSubsection->PtesInSubsection);
    }

    //
    // Try for the fast reference on the first and last subsection.
    // If that cannot be gotten, then there are no prototype PTEs for this
    // subsection, therefore there is nothing in it to flush so leap forwards.
    //
    // Note that subsections in between do not need referencing as
    // MiFlushSectionInternal is smart enough to skip them if they're
    // nonresident.
    //

    if (MiReferenceSubsection ((PMSUBSECTION)Subsection) == FALSE) {
        do {
            //
            // If this increment would put us past the end offset, then nothing
            // to flush, just return success.
            //

            if (Subsection == LastSubsection) {
                UNLOCK_PFN (OldIrql);
                return STATUS_SUCCESS;
            }
            Subsection = Subsection->NextSubsection;

            //
            // If this increment put us past the end of section, then nothing
            // to flush, just return success.
            //

            if (Subsection == NULL) {
                UNLOCK_PFN (OldIrql);
                return STATUS_SUCCESS;
            }

            if ((PMSUBSECTION)Subsection->SubsectionBase == NULL) {
                continue;
            }

            if (MiReferenceSubsection ((PMSUBSECTION)Subsection) == FALSE) {
                continue;
            }

            //
            // Start the flush at this subsection which is now referenced.
            //

            PointerPte = &Subsection->SubsectionBase[0];
            break;

        } while (TRUE);
    }
    else {
        PointerPte = &Subsection->SubsectionBase[PteOffset];
    }

    ASSERT (Subsection->SubsectionBase != NULL);

    //
    // The first subsection is referenced, now reference count the last one.
    // If the first is the last, just double reference it anyway as it
    // simplifies cleanup later.
    //

    if (MiReferenceSubsection ((PMSUBSECTION)LastSubsection) == FALSE) {

        ASSERT (Subsection != LastSubsection);

        TempSubsection = Subsection->NextSubsection;
        LastSubsectionWithProtos = NULL;

        while (TempSubsection != LastSubsection) {

            //
            // If this increment put us past the end of section, then nothing
            // to flush, just return success.
            //

            ASSERT (TempSubsection != NULL);

            if ((PMSUBSECTION)TempSubsection->SubsectionBase != NULL) {
                LastSubsectionWithProtos = TempSubsection;
            }

            TempSubsection = TempSubsection->NextSubsection;
        }

        //
        // End the flush at this subsection and reference it.
        //

        if (LastSubsectionWithProtos == NULL) {
            ASSERT (Subsection != NULL);
            ASSERT (Subsection->SubsectionBase != NULL);
            TempSubsection = Subsection;
        }
        else {
            TempSubsection = LastSubsectionWithProtos;
        }

        if (MiReferenceSubsection ((PMSUBSECTION)TempSubsection) == FALSE) {
            ASSERT (FALSE);
        }

        ASSERT (TempSubsection->SubsectionBase != NULL);

        LastSubsection = TempSubsection;
        LastPteOffset = LastSubsection->PtesInSubsection - 1;
    }

    //
    // End the flush at this subsection which is now referenced.
    //

    LastPte = &LastSubsection->SubsectionBase[LastPteOffset];

    //
    // Up the map view count so the control area cannot be deleted
    // out from under the call.
    //

    ControlArea->NumberOfMappedViews += 1;

    UNLOCK_PFN (OldIrql);

    CurrentThread = PsGetCurrentThread();

    //
    // Indicate that disk verify errors should be returned as exceptions.
    //

    OldClusterState = CurrentThread->ForwardClusterOnly;
    CurrentThread->ForwardClusterOnly = TRUE;

    //
    // Preacquire the file if we are going to synchronize the flush.
    //

    if (AcquireFile == 0) {

        //
        // Flush the PTEs from the specified section.
        //

        status = MiFlushSectionInternal (PointerPte,
                                         LastPte,
                                         Subsection,
                                         LastSubsection,
                                         TRUE,
                                         TRUE,
                                         IoStatus);
    }
    else {

        ConsecutiveFileLockFailures = 0;

        do {

            status = FsRtlAcquireFileForCcFlushEx (ControlArea->FilePointer);

            if (!NT_SUCCESS(status)) {
                break;
            }

            //
            // Flush the PTEs from the specified section.
            //

            status = MiFlushSectionInternal (PointerPte,
                                             LastPte,
                                             Subsection,
                                             LastSubsection,
                                             TRUE,
                                             TRUE,
                                             IoStatus);

            //
            // Release the file we acquired.
            //

            FsRtlReleaseFileForCcFlush (ControlArea->FilePointer);

            //
            // Only try the request more than once if the filesystem told us
            // it had a deadlock.
            //

            if (status != STATUS_FILE_LOCK_CONFLICT) {
                break;
            }

            ConsecutiveFileLockFailures += 1;
            KeDelayExecutionThread (KernelMode, FALSE, (PLARGE_INTEGER)&MmShortTime);

        } while (ConsecutiveFileLockFailures < 5);
    }

    CurrentThread->ForwardClusterOnly = OldClusterState;

    LOCK_PFN (OldIrql);

    MiDecrementSubsections (Subsection, Subsection);
    MiDecrementSubsections (LastSubsection, LastSubsection);

    ASSERT ((LONG)ControlArea->NumberOfMappedViews >= 1);
    ControlArea->NumberOfMappedViews -= 1;

    //
    // Check to see if the control area should be deleted.  This
    // will release the PFN lock.
    //

    MiCheckControlArea (ControlArea, NULL, OldIrql);

    return status;
}


LONGLONG
MiStartingOffset(
    IN PSUBSECTION Subsection,
    IN PMMPTE PteAddress
    )

/*++

Routine Description:

    This function calculates the file offset given a subsection and a PTE
    offset.  Note that images are stored in 512-byte units whereas data is
    stored in 4K units.

    When this is all debugged, this should be made into a macro.

Arguments:

    Subsection - Supplies a subsection to reference for the file address.

    PteAddress - Supplies a PTE within the subsection

Return Value:

    Returns the file offset to obtain the backing data from.

--*/

{
    LONGLONG PteByteOffset;
    LARGE_INTEGER StartAddress;

    if (Subsection->ControlArea->u.Flags.Image == 1) {
            return MI_STARTING_OFFSET ( Subsection,
                                        PteAddress);
    }

    ASSERT (Subsection->SubsectionBase != NULL);

    PteByteOffset = (LONGLONG)((PteAddress - Subsection->SubsectionBase))
                            << PAGE_SHIFT;

    Mi4KStartFromSubsection (&StartAddress, Subsection);

    StartAddress.QuadPart = StartAddress.QuadPart << MM4K_SHIFT;

    PteByteOffset += StartAddress.QuadPart;

    return PteByteOffset;
}

LARGE_INTEGER
MiEndingOffset(
    IN PSUBSECTION Subsection
    )

/*++

Routine Description:

    This function calculates the last valid file offset in a given subsection.
    offset.  Note that images are stored in 512-byte units whereas data is
    stored in 4K units.

    When this is all debugged, this should be made into a macro.

Arguments:

    Subsection - Supplies a subsection to reference for the file address.

    PteAddress - Supplies a PTE within the subsection

Return Value:

    Returns the file offset to obtain the backing data from.

--*/

{
    LARGE_INTEGER FileByteOffset;

    if (Subsection->ControlArea->u.Flags.Image == 1) {
        FileByteOffset.QuadPart =
            (Subsection->StartingSector + Subsection->NumberOfFullSectors) <<
                MMSECTOR_SHIFT;
    }
    else {
        Mi4KStartFromSubsection (&FileByteOffset, Subsection);

        FileByteOffset.QuadPart += Subsection->NumberOfFullSectors;

        FileByteOffset.QuadPart = FileByteOffset.QuadPart << MM4K_SHIFT;
    }

    FileByteOffset.QuadPart += Subsection->u.SubsectionFlags.SectorEndOffset;

    return FileByteOffset;
}


NTSTATUS
MiFlushSectionInternal (
    IN PMMPTE StartingPte,
    IN PMMPTE FinalPte,
    IN PSUBSECTION FirstSubsection,
    IN PSUBSECTION LastSubsection,
    IN ULONG Synchronize,
    IN LOGICAL WriteInProgressOk,
    OUT PIO_STATUS_BLOCK IoStatus
    )

/*++

Routine Description:

    This function flushes to the backing file any modified pages within
    the specified range of the section.  The parameters describe the
    section's prototype PTEs (start and end) and the subsections
    which correspond to the starting and ending PTE.

    Each PTE in the subsection between the specified start and end
    is examined and if the page is either valid or transition AND
    the page has been modified, the modify bit is cleared in the PFN
    database and the page is flushed to its backing file.

Arguments:

    StartingPte - Supplies a pointer to the first prototype PTE to
                  be examined for flushing.

    FinalPte - Supplies a pointer to the last prototype PTE to be
               examined for flushing.

    FirstSubsection - Supplies the subsection that contains the
                      StartingPte.

    LastSubsection - Supplies the subsection that contains the
                     FinalPte.

    Synchronize - Supplies TRUE if synchronization with all threads
                  doing flush operations to this section should occur.

    WriteInProgressOk - Supplies TRUE if the caller can tolerate a write
                        already in progress for any dirty pages.

    IoStatus - Returns the value of the IoStatus for the last attempted
               I/O operation.

Return Value:

    Returns status of the operation.

--*/

{
    LOGICAL DroppedPfnLock;
    PCONTROL_AREA ControlArea;
    PMMPTE PointerPte;
    PMMPTE LastPte;
    PMMPTE LastWritten;
    PMMPTE FirstWritten;
    MMPTE PteContents;
    PMMPFN Pfn1;
    PMMPFN Pfn2;
    KIRQL OldIrql;
    PMDL Mdl;
    KEVENT IoEvent;
    PSUBSECTION Subsection;
    PMSUBSECTION MappedSubsection;
    PPFN_NUMBER Page;
    PFN_NUMBER PageFrameIndex;
    PPFN_NUMBER LastPage;
    NTSTATUS Status;
    UINT64 StartingOffset;
    UINT64 TempOffset;
    LOGICAL WriteNow;
    LOGICAL Bail;
    PFN_NUMBER MdlHack[(sizeof(MDL)/sizeof(PFN_NUMBER)) + (MM_MAXIMUM_DISK_IO_SIZE / PAGE_SIZE) + 1];
    ULONG ReflushCount;
    ULONG MaxClusterSize;
    PFILE_OBJECT FilePointer;
    LOGICAL CurrentThreadIsDereferenceThread;

    //
    // WriteInProgressOk is only FALSE when the segment dereference thread is
    // doing a top-level flush just prior to cleaning the section or subsection.
    // Note that this flag may be TRUE even for the dereference thread because
    // the dereference thread calls filesystems who may then issue a flush.
    //

    if (WriteInProgressOk == FALSE) {
        CurrentThreadIsDereferenceThread = TRUE;
        ASSERT (PsGetCurrentThread()->StartAddress == (PVOID)(ULONG_PTR)MiDereferenceSegmentThread);
    }
    else {
        CurrentThreadIsDereferenceThread = FALSE;

        //
        // This may actually be the dereference thread as the segment deletion
        // dereferences the file object potentially calling the filesystem which
        // may then issue a CcFlushCache/MmFlushSection.  For our purposes,
        // lower level flushes in this context are treated as though they
        // came from a different thread.
        //
    }

    WriteNow = FALSE;
    Bail = FALSE;

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = 0;
    Mdl = (PMDL)&MdlHack[0];

    KeInitializeEvent (&IoEvent, NotificationEvent, FALSE);

    FinalPte += 1;  // Point to 1 past the last one.

    FirstWritten = NULL;
    LastWritten = NULL;
    LastPage = 0;
    Subsection = FirstSubsection;
    PointerPte = StartingPte;
    ControlArea = FirstSubsection->ControlArea;
    FilePointer = ControlArea->FilePointer;

    ASSERT ((ControlArea->u.Flags.Image == 0) &&
            (FilePointer != NULL) &&
            (ControlArea->u.Flags.PhysicalMemory == 0));

    //
    // Initializing these is not needed for correctness
    // but without it the compiler cannot compile this code
    // W4 to check for use of uninitialized variables.
    //

    MappedSubsection = NULL;
    StartingOffset = 0;

    //
    // Try to cluster pages as long as the storage stack can handle it.
    //

    MaxClusterSize = MmModifiedWriteClusterSize;

    LOCK_PFN (OldIrql);

    ASSERT (ControlArea->u.Flags.Image == 0);

    if (ControlArea->NumberOfPfnReferences == 0) {

        //
        // No transition or valid prototype PTEs present, hence
        // no need to flush anything.
        //

        UNLOCK_PFN (OldIrql);
        return STATUS_SUCCESS;
    }

    while ((Synchronize) && (ControlArea->FlushInProgressCount != 0)) {

        //
        // Another thread is currently performing a flush operation on
        // this file.  Wait for that flush to complete.
        //

        ControlArea->u.Flags.CollidedFlush = 1;

        //
        // Keep APCs blocked so no special APCs can be delivered in KeWait
        // which would cause the dispatcher lock to be released opening a
        // window where this thread could miss a pulse.
        //

        UNLOCK_PFN_AND_THEN_WAIT (APC_LEVEL);

        KeWaitForSingleObject (&MmCollidedFlushEvent,
                               WrPageOut,
                               KernelMode,
                               FALSE,
                               (PLARGE_INTEGER)&MmOneSecond);
        KeLowerIrql (OldIrql);
        LOCK_PFN (OldIrql);
    }

    ControlArea->FlushInProgressCount += 1;

    //
    // Clear the deferred entry list as pages from it may get marked modified
    // during the processing.  Note that any transition page which is currently
    // clean but has a nonzero reference count may get marked modified if
    // there is a pending transaction and note well that this transaction may
    // complete at any time !  Thus, this case must be carefully handled.
    //

#if !defined(MI_MULTINODE)
    if (MmPfnDeferredList != NULL) {
        MiDeferredUnlockPages (MI_DEFER_PFN_HELD);
    }
#else
    //
    // Each and every node's deferred list would have to be checked so
    // we might as well go the long way and just call.
    //

    MiDeferredUnlockPages (MI_DEFER_PFN_HELD);
#endif

    for (;;) {

        if (LastSubsection != Subsection) {

            //
            // Flush to the last PTE in this subsection.
            //

            LastPte = &Subsection->SubsectionBase[Subsection->PtesInSubsection];
        }
        else {

            //
            // Flush to the end of the range.
            //

            LastPte = FinalPte;
        }

        if (Subsection->SubsectionBase == NULL) {

            //
            // The prototype PTEs for this subsection have either never been
            // created or have been tossed due to memory pressure.  Either
            // way, this range can be skipped as there are obviously no
            // dirty pages in it.  If there are other dirty pages
            // to be written, write them now as we are skipping over PTEs.
            //

            if (LastWritten != NULL) {
                ASSERT (MappedSubsection != NULL);
                WriteNow = TRUE;
                goto CheckForWrite;
            }
            if (LastSubsection == Subsection) {
                break;
            }
            Subsection = Subsection->NextSubsection;
            PointerPte = Subsection->SubsectionBase;
            continue;
        }

        //
        // Up the number of mapped views to prevent other threads
        // from freeing this to the unused subsection list while we're
        // operating on it.
        //

        MappedSubsection = (PMSUBSECTION) Subsection;
        MappedSubsection->NumberOfMappedViews += 1;

        if (MappedSubsection->DereferenceList.Flink != NULL) {

            //
            // Remove this from the list of unused subsections.
            //

            RemoveEntryList (&MappedSubsection->DereferenceList);

            MI_UNUSED_SUBSECTIONS_COUNT_REMOVE (MappedSubsection);

            MappedSubsection->DereferenceList.Flink = NULL;
        }

        if (CurrentThreadIsDereferenceThread == FALSE) {

            //
            // Set the access bit so an already ongoing trim won't blindly
            // delete the prototype PTEs on completion of a mapped write.
            // This can happen if the current thread dirties some pages and
            // then deletes the view before the trim write finishes - this
            // bit informs the trimming thread that a rescan is needed so
            // that writes are not lost.
            //

            MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed = 1;
        }

        //
        // If the prototype PTEs are paged out or have a share count
        // of 1, they cannot contain any transition or valid PTEs.
        //

        if (!MiCheckProtoPtePageState(PointerPte, TRUE, &DroppedPfnLock)) {
            PointerPte = (PMMPTE)(((ULONG_PTR)PointerPte | (PAGE_SIZE - 1)) + 1);
        }

        while (PointerPte < LastPte) {

            if (MiIsPteOnPdeBoundary(PointerPte)) {

                //
                // We are on a page boundary, make sure this PTE is resident.
                //

                if (!MiCheckProtoPtePageState(PointerPte, TRUE, &DroppedPfnLock)) {
                    PointerPte = (PMMPTE)((PCHAR)PointerPte + PAGE_SIZE);

                    //
                    // If there are dirty pages to be written, write them
                    // now as we are skipping over PTEs.
                    //

                    if (LastWritten != NULL) {
                        WriteNow = TRUE;
                        goto CheckForWrite;
                    }
                    continue;
                }
            }

            PteContents = *PointerPte;

            if ((PteContents.u.Hard.Valid == 1) ||
                   ((PteContents.u.Soft.Prototype == 0) &&
                     (PteContents.u.Soft.Transition == 1))) {

                //
                // Prototype PTE in transition, there are 3 possible cases:
                //  1. The page is part of an image which is sharable and
                //     refers to the paging file - dereference page file
                //     space and free the physical page.
                //  2. The page refers to the segment but is not modified -
                //     free the physical page.
                //  3. The page refers to the segment and is modified -
                //     write the page to the file and free the physical page.
                //

                if (PteContents.u.Hard.Valid == 1) {
                    PageFrameIndex = MI_GET_PAGE_FRAME_FROM_PTE (&PteContents);
                }
                else {
                    PageFrameIndex = MI_GET_PAGE_FRAME_FROM_TRANSITION_PTE (&PteContents);
                }

                Pfn1 = MI_PFN_ELEMENT (PageFrameIndex);
                ASSERT (Pfn1->OriginalPte.u.Soft.Prototype == 1);
                ASSERT (Pfn1->OriginalPte.u.Hard.Valid == 0);

                //
                // Note that any transition page which is currently clean but
                // has a nonzero reference count may get marked modified if
                // there is a pending transaction and note well that this
                // transaction may complete at any time !  Thus, this case
                // must be carefully handled since the segment dereference
                // thread must be given a collision error for this one as it
                // requires that no pages be dirtied after a successful return.
                //

                if ((CurrentThreadIsDereferenceThread == TRUE) &&
                    (Pfn1->u3.e2.ReferenceCount != 0)) {

#if DBG
                    if ((PteContents.u.Hard.Valid != 0) &&
                        (MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed == 0) &&
                        (ControlArea->u.Flags.Accessed == 0)) {

                        if (!KdDebuggerNotPresent) {
                            DbgPrint ("MM: flushing valid proto, %p %p\n",
                                            Pfn1, PointerPte);
                            DbgBreakPoint ();
                        }
                    }
#endif

                    PointerPte = LastPte;
                    Bail = TRUE;

                    if (LastWritten != NULL) {
                        WriteNow = TRUE;
                    }
                    goto CheckForWrite;
                }

                //
                // If the page is modified OR a write is in progress
                // flush it.  The write in progress case catches problems
                // where the modified page write continually writes a
                // page and gets errors writing it, by writing pages
                // in this state, the error will be propagated back to
                // the caller.
                //

                if ((Pfn1->u3.e1.Modified == 1) ||
                    (Pfn1->u3.e1.WriteInProgress)) {

                    if ((WriteInProgressOk == FALSE) &&
                        (Pfn1->u3.e1.WriteInProgress)) {

                            PointerPte = LastPte;
                            Bail = TRUE;

                            if (LastWritten != NULL) {
                                WriteNow = TRUE;
                            }
                            goto CheckForWrite;
                    }

                    if (LastWritten == NULL) {

                        //
                        // This is the first page of a cluster, initialize
                        // the MDL, etc.
                        //

                        LastPage = (PPFN_NUMBER)(Mdl + 1);

                        //
                        // Calculate the offset to read into the file.
                        //  offset = base + ((thispte - basepte) << PAGE_SHIFT)
                        //

                        StartingOffset = (UINT64) MiStartingOffset (
                                                             Subsection,
                                                             Pfn1->PteAddress);

                        MI_INITIALIZE_ZERO_MDL (Mdl);

                        Mdl->MdlFlags |= MDL_PAGES_LOCKED;
                        Mdl->StartVa =
                                  (PVOID)ULongToPtr(Pfn1->u3.e1.PageColor << PAGE_SHIFT);
                        Mdl->Size = (CSHORT)(sizeof(MDL) +
                                   (sizeof(PFN_NUMBER) * MaxClusterSize));
                        FirstWritten = PointerPte;
                    }

                    LastWritten = PointerPte;
                    Mdl->ByteCount += PAGE_SIZE;
                    if (Mdl->ByteCount == (PAGE_SIZE * MaxClusterSize)) {
                        WriteNow = TRUE;
                    }

                    if (PteContents.u.Hard.Valid == 0) {

                        //
                        // The page is in transition.
                        //

                        MiUnlinkPageFromList (Pfn1);
                        MI_ADD_LOCKED_PAGE_CHARGE_FOR_MODIFIED_PAGE(Pfn1, 18);
                    }
                    else {
                        MI_ADD_LOCKED_PAGE_CHARGE(Pfn1, 20);
                    }

                    //
                    // Clear the modified bit for this page.
                    //

                    MI_SET_MODIFIED (Pfn1, 0, 0x22);

                    //
                    // Up the reference count for the physical page as there
                    // is I/O in progress.
                    //

                    Pfn1->u3.e2.ReferenceCount += 1;

                    *LastPage = PageFrameIndex;
                    LastPage += 1;
                }
                else {

                    //
                    // This page was not modified and therefore ends the
                    // current write cluster if any.  Set WriteNow to TRUE
                    // if there is a cluster being built.
                    //

                    if (LastWritten != NULL) {
                        WriteNow = TRUE;
                    }
                }
            }
            else {

                //
                // This page was not modified and therefore ends the
                // current write cluster if any.  Set WriteNow to TRUE
                // if there is a cluster being built.
                //

                if (LastWritten != NULL) {
                    WriteNow = TRUE;
                }
            }

            PointerPte += 1;

CheckForWrite:

            //
            // Write the current cluster if it is complete,
            // full, or the loop is now complete.
            //

            if ((WriteNow) ||
                ((PointerPte == LastPte) && (LastWritten != NULL))) {

                LARGE_INTEGER EndOfFile;

                //
                // Issue the write request.
                //

                UNLOCK_PFN (OldIrql);

                WriteNow = FALSE;

                //
                // Make sure the write does not go past the
                // end of file. (segment size).
                //

                EndOfFile = MiEndingOffset(Subsection);
                TempOffset = (UINT64) EndOfFile.QuadPart;

                if (StartingOffset + Mdl->ByteCount > TempOffset) {

                    ASSERT ((ULONG_PTR)(TempOffset - StartingOffset) >
                             (Mdl->ByteCount - PAGE_SIZE));

                    Mdl->ByteCount = (ULONG)(TempOffset- StartingOffset);
                }

                ReflushCount = 0;
                
                while (TRUE) {

                    KeClearEvent (&IoEvent);

                    Status = IoSynchronousPageWrite (FilePointer,
                                                     Mdl,
                                                     (PLARGE_INTEGER)&StartingOffset,
                                                     &IoEvent,
                                                     IoStatus);

                    if (NT_SUCCESS(Status)) {

                        //
                        // Success was returned, so wait for the i/o event.
                        //

                        KeWaitForSingleObject (&IoEvent,
                                               WrPageOut,
                                               KernelMode,
                                               FALSE,
                                               NULL);
                    }
                    else {

                        //
                        // Copy the error to the IoStatus, for error
                        // handling below.
                        //
    
                        IoStatus->Status = Status;
                    }

                    if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA) {
                        MmUnmapLockedPages (Mdl->MappedSystemVa, Mdl);
                    }

                    if (MmIsRetryIoStatus(IoStatus->Status)) {
                        
                        ReflushCount -= 1;
                        if (ReflushCount & MiIoRetryMask) {
                            KeDelayExecutionThread (KernelMode, FALSE, (PLARGE_INTEGER)&Mm30Milliseconds);
                            continue;
                        }
                    }

                    break;
                }

                Page = (PPFN_NUMBER)(Mdl + 1);

                LOCK_PFN (OldIrql);

                if (MiIsPteOnPdeBoundary(PointerPte) == 0) {

                    //
                    // The next PTE is not in a different page, make
                    // sure the PTE for the prototype PTE page was not
                    // put in transition while the I/O was in progress.
                    // Note the prototype PTE page itself cannot be reused
                    // as each outstanding page has a sharecount on it - but
                    // the PTE mapping it can be put in transition regardless
                    // of sharecount because it is a system page.
                    //

                    MiMakeSystemAddressValidPfn (PointerPte);
                }

                if (NT_SUCCESS(IoStatus->Status)) {

                    //
                    // The I/O completed successfully, unlock the pages.
                    //

                    while (Page < LastPage) {

                        Pfn2 = MI_PFN_ELEMENT (*Page);
                        MI_REMOVE_LOCKED_PAGE_CHARGE_AND_DECREF(Pfn2, 19);
                        Page += 1;
                    }
                }
                else {

                    //
                    // Don't count on the file system to convey
                    // anything in the information field on errors.
                    //

                    IoStatus->Information = 0;

                    //
                    // The I/O completed unsuccessfully, unlock the pages
                    // and return an error status.
                    //

                    while (Page < LastPage) {

                        Pfn2 = MI_PFN_ELEMENT (*Page);

                        //
                        // Mark the page dirty again so it can be rewritten.
                        //

                        MI_SET_MODIFIED (Pfn2, 1, 0x1);

                        MI_REMOVE_LOCKED_PAGE_CHARGE_AND_DECREF (Pfn2, 21);

                        Page += 1;
                    }

                    if ((MmIsRetryIoStatus(IoStatus->Status)) &&
                        (MaxClusterSize != 1) &&
                        (Mdl->ByteCount > PAGE_SIZE)) {

                        //
                        // Retries of a cluster have failed, reissue
                        // the cluster one page at a time as the
                        // storage stack should always be able to
                        // make forward progress this way.
                        //

                        ASSERT (FirstWritten != NULL);
                        ASSERT (LastWritten != NULL);
                        ASSERT (FirstWritten != LastWritten);

                        PointerPte = FirstWritten;
                        MiMakeSystemAddressValidPfn (PointerPte);
                        MaxClusterSize = 1;
                    }
                    else {
    
                        //
                        // Calculate how much was written thus far
                        // and add that to the information field
                        // of the IOSB.
                        //
    
                        IoStatus->Information +=
                            (((LastWritten - StartingPte) << PAGE_SHIFT) -
                                                            Mdl->ByteCount);
                        LastWritten = NULL;
    
                        //
                        // Set this to force termination of the outermost loop.
                        //
    
                        Subsection = LastSubsection;
                        break;
                    }

                } // end if error on i/o

                //
                // As the PFN lock has been released and
                // reacquired, do this loop again as the
                // PTE may have changed state.
                //

                LastWritten = NULL;
            } // end if chunk to write

        } //end while

        ASSERT (MappedSubsection->DereferenceList.Flink == NULL);
        ASSERT ((MappedSubsection->NumberOfMappedViews >= 1) ||
                (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 1));

        MappedSubsection->NumberOfMappedViews -= 1;

        if ((MappedSubsection->NumberOfMappedViews == 0) &&
            (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 0)) {

            //
            // Insert this subsection into the unused subsection list.
            //

            InsertTailList (&MmUnusedSubsectionList,
                            &MappedSubsection->DereferenceList);
            MI_UNUSED_SUBSECTIONS_COUNT_INSERT (MappedSubsection);
        }

        if ((Bail == TRUE) || (Subsection == LastSubsection)) {

            //
            // The last range has been flushed or we have collided with the
            // mapped page writer.  Regardless, exit the top FOR loop
            // and return.
            //

            break;
        }

        Subsection = Subsection->NextSubsection;
        PointerPte = Subsection->SubsectionBase;

    }  //end for

    ASSERT (LastWritten == NULL);

    ControlArea->FlushInProgressCount -= 1;
    if ((ControlArea->u.Flags.CollidedFlush == 1) &&
        (ControlArea->FlushInProgressCount == 0)) {
        ControlArea->u.Flags.CollidedFlush = 0;
        KePulseEvent (&MmCollidedFlushEvent, 0, FALSE);
    }
    UNLOCK_PFN (OldIrql);

    if (Bail == TRUE) {

        //
        // This routine collided with the mapped page writer and the caller
        // expects an error for this.  Give it to him.
        //

        return STATUS_MAPPED_WRITER_COLLISION;
    }

    return IoStatus->Status;
}

BOOLEAN
MmPurgeSection (
    IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
    IN PLARGE_INTEGER Offset,
    IN SIZE_T RegionSize,
    IN ULONG IgnoreCacheViews
    )

/*++

Routine Description:

    This function determines if any views of the specified section
    are mapped, and if not, purges valid pages (even modified ones)
    from the specified section and returns any used pages to the free
    list.  This is accomplished by examining the prototype PTEs
    from the specified offset to the end of the section, and if
    any prototype PTEs are in the transition state, putting the
    prototype PTE back into its original state and putting the
    physical page on the free list.

    NOTE:

    If there is an I/O operation ongoing for one of the pages,
    that page is eliminated from the segment and allowed to "float"
    until the i/o is complete.  Once the share count goes to zero
    the page will be added to the free page list.

Arguments:

    SectionObjectPointer - Supplies a pointer to the section objects.

    Offset - Supplies the offset into the section in which to begin
             purging pages.  If this argument is not present, then the
             whole section is purged without regard to the region size
             argument.


    RegionSize - Supplies the size of the region to purge.  If this
                 is specified as zero and Offset is specified, the
                 region from Offset to the end of the file is purged.

                 Note: The largest value acceptable for RegionSize is
                 0xFFFF0000;

    IgnoreCacheViews - Supplies FALSE if mapped views in the system
                 cache should cause the function to return FALSE.
                 This is the normal case.
                 Supplies TRUE if mapped views should be ignored
                 and the flush should occur.  NOTE THAT IF TRUE
                 IS SPECIFIED AND ANY DATA PURGED IS CURRENTLY MAPPED
                 AND VALID A BUGCHECK WILL OCCUR!!

Return Value:

    Returns TRUE if either no section exists for the file object or
    the section is not mapped and the purge was done, FALSE otherwise.

    Note that FALSE is returned if during the purge operation, a page
    could not be purged due to a non-zero reference count.

--*/

{
    LOGICAL DroppedPfnLock;
    PCONTROL_AREA ControlArea;
    PMAPPED_FILE_SEGMENT Segment;
    PMMPTE PointerPte;
    PMMPTE LastPte;
    PMMPTE FinalPte;
    MMPTE PteContents;
    PMMPFN Pfn1;
    PMMPFN Pfn2;
    KIRQL OldIrql;
    ULONG PteOffset;
    ULONG LastPteOffset;
    PMSUBSECTION MappedSubsection;
    PSUBSECTION Subsection;
    PSUBSECTION FirstSubsection;
    PSUBSECTION LastSubsection;
    PSUBSECTION TempSubsection;
    PSUBSECTION LastSubsectionWithProtos;
    LARGE_INTEGER LocalOffset;
    LOGICAL LockHeld;
    BOOLEAN ReturnValue;
    PFN_NUMBER PageFrameIndex;
    PFN_NUMBER PageTableFrameIndex;
#if DBG
    PFN_NUMBER LastLocked = 0;
#endif

    //
    // This is needed in case a page is on the mapped page writer list -
    // the PFN lock will need to be released and APCs disabled.
    //

    ASSERT (KeGetCurrentIrql() < DISPATCH_LEVEL);

    //
    //  Capture caller's file size, since we may modify it.
    //

    if (ARGUMENT_PRESENT(Offset)) {

        LocalOffset = *Offset;
        Offset = &LocalOffset;
    }

    //
    //  See if we can truncate this file to where the caller wants
    //  us to.
    //

    if (!MiCanFileBeTruncatedInternal(SectionObjectPointer, Offset, TRUE, &OldIrql)) {
        return FALSE;
    }

    //
    // PFN LOCK IS NOW HELD!
    //

    ControlArea = (PCONTROL_AREA)(SectionObjectPointer->DataSectionObject);
    if ((ControlArea == NULL) || (ControlArea->u.Flags.Rom)) {
        UNLOCK_PFN (OldIrql);
        return TRUE;
    }

    //
    //  Even though MiCanFileBeTruncatedInternal returned TRUE, there could
    //  still be a system cache mapped view.  We cannot truncate if
    //  the Cache Manager has a view mapped.
    //

    if ((IgnoreCacheViews == FALSE) &&
        (ControlArea->NumberOfSystemCacheViews != 0)) {

        UNLOCK_PFN (OldIrql);
        return FALSE;
    }

#if 0

    //
    // Prevent races when the control area is being deleted as the clean
    // path releases the PFN lock midway through.  File objects may still have
    // section object pointers and data section objects that point at this
    // control area, hence the purge can be issued.
    //
    // Check for this and fail the purge as the control area (and the section
    // object pointers/data section objects) will be going away momentarily.
    // Note that even though drivers have these data section objects, no one
    // currently has an open section for this control area and no one is
    // allowed to open one until the clean path finishes.
    //

    if (ControlArea->u.Flags.BeingDeleted == 1) {
        UNLOCK_PFN (OldIrql);
        return FALSE;
    }

#else

    //
    // The above check can be removed as MiCanFileBeTruncatedInternal does
    // the same check, so just assert it below.
    //

    ASSERT (ControlArea->u.Flags.BeingDeleted == 0);

#endif

    //
    // Purge the section - locate the subsection which
    // contains the PTEs.
    //

    ASSERT (ControlArea->u.Flags.GlobalOnlyPerSession == 0);

    Subsection = (PSUBSECTION)(ControlArea + 1);

    if (!ARGUMENT_PRESENT (Offset)) {

        //
        // If the offset is not specified, flush the complete file ignoring
        // the region size.
        //

        PteOffset = 0;
        RegionSize = 0;

    }
    else {

        PteOffset = (ULONG)(Offset->QuadPart >> PAGE_SHIFT);

        //
        // Make sure the PTEs are not in the extended part of the segment.
        //

        while (PteOffset >= Subsection->PtesInSubsection) {
            PteOffset -= Subsection->PtesInSubsection;
            Subsection = Subsection->NextSubsection;
            if (Subsection == NULL) {

                //
                // The offset must be equal to the size of
                // the section, don't purge anything just return.
                //

                UNLOCK_PFN (OldIrql);
                return TRUE;
            }
        }

        ASSERT (PteOffset < Subsection->PtesInSubsection);
    }

    //
    // Locate the address of the last prototype PTE to be flushed.
    //

    if (RegionSize == 0) {

        //
        // Flush to end of section.
        //

        LastSubsection = Subsection;

        Segment = (PMAPPED_FILE_SEGMENT) ControlArea->Segment;

        if (MmIsAddressValid (Segment)) {
            if (Segment->LastSubsectionHint != NULL) {
                LastSubsection = (PSUBSECTION) Segment->LastSubsectionHint;
            }
        }

        while (LastSubsection->NextSubsection != NULL) {
            LastSubsection = LastSubsection->NextSubsection;
        }

        LastPteOffset = LastSubsection->PtesInSubsection - 1;
    }
    else {

        //
        // Calculate the end of the region.
        //

        LastPteOffset = PteOffset +
            (ULONG) (((RegionSize + BYTE_OFFSET(Offset->LowPart)) - 1) >> PAGE_SHIFT);

        LastSubsection = Subsection;

        while (LastPteOffset >= LastSubsection->PtesInSubsection) {
            LastPteOffset -= LastSubsection->PtesInSubsection;
            if (LastSubsection->NextSubsection == NULL) {
                LastPteOffset = LastSubsection->PtesInSubsection - 1;
                break;
            }
            LastSubsection = LastSubsection->NextSubsection;
        }

        ASSERT (LastPteOffset < LastSubsection->PtesInSubsection);
    }

    //
    // Try for the fast reference on the first and last subsection.
    // If that cannot be gotten, then there are no prototype PTEs for this
    // subsection, therefore there is nothing in it to flush so leap forwards.
    //
    // Note that subsections in between do not need referencing as
    // the purge is smart enough to skip them if they're nonresident.
    //

    if (MiReferenceSubsection ((PMSUBSECTION)Subsection) == FALSE) {
        do {
            //
            // If this increment would put us past the end offset, then nothing
            // to flush, just return success.
            //

            if (Subsection == LastSubsection) {
                UNLOCK_PFN (OldIrql);
                return TRUE;
            }
            Subsection = Subsection->NextSubsection;

            //
            // If this increment put us past the end of section, then nothing
            // to flush, just return success.
            //

            if (Subsection == NULL) {
                UNLOCK_PFN (OldIrql);
                return TRUE;
            }

            if (MiReferenceSubsection ((PMSUBSECTION)Subsection) == FALSE) {
                continue;
            }

            //
            // Start the flush at this subsection which is now referenced.
            //

            PointerPte = &Subsection->SubsectionBase[0];
            break;

        } while (TRUE);
    }
    else {
        PointerPte = &Subsection->SubsectionBase[PteOffset];
    }

    FirstSubsection = Subsection;
    ASSERT (Subsection->SubsectionBase != NULL);

    //
    // The first subsection is referenced, now reference count the last one.
    // If the first is the last, just double reference it anyway as it
    // simplifies cleanup later.
    //

    if (MiReferenceSubsection ((PMSUBSECTION)LastSubsection) == FALSE) {

        ASSERT (Subsection != LastSubsection);

        TempSubsection = Subsection->NextSubsection;
        LastSubsectionWithProtos = NULL;

        while (TempSubsection != LastSubsection) {

            //
            // If this increment put us past the end of section, then nothing
            // to flush, just return success.
            //

            ASSERT (TempSubsection != NULL);

            if ((PMSUBSECTION)TempSubsection->SubsectionBase != NULL) {
                LastSubsectionWithProtos = TempSubsection;
            }

            TempSubsection = TempSubsection->NextSubsection;
        }

        //
        // End the flush at this subsection and reference it.
        //

        if (LastSubsectionWithProtos == NULL) {
            ASSERT (Subsection != NULL);
            ASSERT (Subsection->SubsectionBase != NULL);
            TempSubsection = Subsection;
        }
        else {
            TempSubsection = LastSubsectionWithProtos;
        }

        if (MiReferenceSubsection ((PMSUBSECTION)TempSubsection) == FALSE) {
            ASSERT (FALSE);
        }

        ASSERT (TempSubsection->SubsectionBase != NULL);

        LastSubsection = TempSubsection;
        LastPteOffset = LastSubsection->PtesInSubsection - 1;
    }

    //
    // End the flush at this subsection which is now referenced.
    //
    // Point final PTE to 1 beyond the end.
    //

    FinalPte = &LastSubsection->SubsectionBase[LastPteOffset + 1];

    //
    // Increment the number of mapped views to
    // prevent the section from being deleted while the purge is
    // in progress.
    //

    ControlArea->NumberOfMappedViews += 1;

    //
    // Set being purged so no one can map a view
    // while the purge is going on.
    //

    ControlArea->u.Flags.BeingPurged = 1;
    ControlArea->u.Flags.WasPurged = 1;

    LockHeld = TRUE;
    ReturnValue = TRUE;

    for (;;) {

        if (!LockHeld) {
            LockHeld = TRUE;
            LOCK_PFN (OldIrql);
        }

        if (LastSubsection != Subsection) {

            //
            // Flush to the last PTE in this subsection.
            //

            LastPte = &Subsection->SubsectionBase[Subsection->PtesInSubsection];
        }
        else {

            //
            // Flush to the end of the range.
            //

            LastPte = FinalPte;
        }

        if (Subsection->SubsectionBase == NULL) {

            //
            // The prototype PTEs for this subsection have either never been
            // created or have been tossed due to memory pressure.  Either
            // way, this range can be skipped as there are obviously no
            // pages to purge in this range.
            //

            ASSERT (LockHeld);
            UNLOCK_PFN (OldIrql);
            LockHeld = FALSE;
            goto nextrange;
        }

        //
        // Up the number of mapped views to prevent other threads
        // from freeing this to the unused subsection list while we're
        // operating on it.
        //

        MappedSubsection = (PMSUBSECTION) Subsection;
        MappedSubsection->NumberOfMappedViews += 1;

        if (MappedSubsection->DereferenceList.Flink != NULL) {

            //
            // Remove this from the list of unused subsections.
            //

            RemoveEntryList (&MappedSubsection->DereferenceList);

            MI_UNUSED_SUBSECTIONS_COUNT_REMOVE (MappedSubsection);

            MappedSubsection->DereferenceList.Flink = NULL;
        }

        //
        // Set the access bit so an already ongoing trim won't blindly
        // delete the prototype PTEs on completion of a mapped write.
        // This can happen if the current thread dirties some pages and
        // then deletes the view before the trim write finishes - this
        // bit informs the trimming thread that a rescan is needed so
        // that writes are not lost.
        //

        MappedSubsection->u2.SubsectionFlags2.SubsectionAccessed = 1;

        //
        // If the page table page containing the PTEs is not
        // resident, then no PTEs can be in the valid or transition
        // state!  Skip over the PTEs.
        //

        if (!MiCheckProtoPtePageState(PointerPte, LockHeld, &DroppedPfnLock)) {
            PointerPte = (PMMPTE)(((ULONG_PTR)PointerPte | (PAGE_SIZE - 1)) + 1);
        }

        while (PointerPte < LastPte) {

            //
            // If the page table page containing the PTEs is not
            // resident, then no PTEs can be in the valid or transition
            // state!  Skip over the PTEs.
            //

            if (MiIsPteOnPdeBoundary(PointerPte)) {
                if (!MiCheckProtoPtePageState(PointerPte, LockHeld, &DroppedPfnLock)) {
                    PointerPte = (PMMPTE)((PCHAR)PointerPte + PAGE_SIZE);
                    continue;
                }
            }

            PteContents = *PointerPte;

            if (PteContents.u.Hard.Valid == 1) {

                //
                // A valid PTE was found, it must be mapped in the
                // system cache.  Just exit the loop and return FALSE
                // and let the caller fix this.
                //

                ReturnValue = FALSE;
                break;
            }

            if ((PteContents.u.Soft.Prototype == 0) &&
                     (PteContents.u.Soft.Transition == 1)) {

                if (!LockHeld) {
                    LockHeld = TRUE;
                    LOCK_PFN (OldIrql);
                    MiMakeSystemAddressValidPfn (PointerPte);
                    continue;
                }

                PageFrameIndex = MI_GET_PAGE_FRAME_FROM_TRANSITION_PTE(&PteContents);
                Pfn1 = MI_PFN_ELEMENT (PageFrameIndex);

                if ((Pfn1->OriginalPte.u.Soft.Prototype != 1) ||
                    (Pfn1->OriginalPte.u.Hard.Valid != 0) ||
                    (Pfn1->PteAddress != PointerPte)) {

                    //
                    // The pool containing the prototype PTEs has been
                    // corrupted.  Pool corruption like this is fatal.
                    //

                    KeBugCheckEx (POOL_CORRUPTION_IN_FILE_AREA,
                                  0x2,
                                  (ULONG_PTR)PointerPte,
                                  (ULONG_PTR)Pfn1->PteAddress,
                                  (ULONG_PTR)PteContents.u.Long);
                }

#if DBG
                if ((Pfn1->u3.e2.ReferenceCount != 0) &&
                    (Pfn1->u3.e1.WriteInProgress == 0)) {

                    //
                    // There must be an I/O in progress on this page.
                    //

                    if (MI_GET_PAGE_FRAME_FROM_TRANSITION_PTE(&PteContents) != LastLocked) {
                        UNLOCK_PFN (OldIrql);

                        LastLocked = MI_GET_PAGE_FRAME_FROM_TRANSITION_PTE (&PteContents);
                        LOCK_PFN (OldIrql);
                        MiMakeSystemAddressValidPfn (PointerPte);
                        continue;
                    }
                }
#endif //DBG

                //
                // If the modified page writer has page locked for I/O
                // wait for the I/O's to be completed and the pages
                // to be unlocked.  The eliminates a race condition
                // when the modified page writer locks the pages, then
                // a purge occurs and completes before the mapped
                // writer thread runs.
                //

                if (Pfn1->u3.e1.WriteInProgress == 1) {

                    //
                    // A 3 or more thread deadlock can occur where:
                    //
                    // 1.  The mapped page writer thread has issued a write
                    //     and is in the filesystem code waiting for a resource.
                    //
                    // 2.  Thread 2 owns the resource above but is waiting for
                    //     the filesystem's quota mutex.
                    //
                    // 3.  Thread 3 owns the quota mutex and is right here
                    //     doing a purge from the cache manager when he notices
                    //     the page to be purged is either already being written
                    //     or is in the mapped page writer list.  If it is
                    //     already being written everything will unjam.  If it
                    //     is still on the mapped page writer list awaiting
                    //     processing, then it must be cancelled - otherwise
                    //     if this thread were to wait, deadlock can occur.
                    //
                    // The alternative to all this is for the filesystems to
                    // always release the quota mutex before purging but the
                    // filesystem overhead to do this is substantial.
                    //

                    if (MiCancelWriteOfMappedPfn (PageFrameIndex) == TRUE) {

                        //
                        // Stopping any failed writes (even deliberately
                        // cancelled ones) automatically cause a delay.  A
                        // successful stop also results in the PFN lock
                        // being released and reacquired.  So loop back to
                        // the top now as the world may have changed.
                        //

                        MiMakeSystemAddressValidPfn (PointerPte);
                        continue;
                    }

                    ASSERT (ControlArea->ModifiedWriteCount != 0);
                    ASSERT (Pfn1->u3.e2.ReferenceCount != 0);

                    ControlArea->u.Flags.SetMappedFileIoComplete = 1;

                    //
                    // Keep APCs blocked so no special APCs can be delivered
                    // in KeWait which would cause the dispatcher lock to be
                    // released opening a window where this thread could miss
                    // a pulse.
                    //

                    UNLOCK_PFN_AND_THEN_WAIT (APC_LEVEL);

                    KeWaitForSingleObject (&MmMappedFileIoComplete,
                                           WrPageOut,
                                           KernelMode,
                                           FALSE,
                                           NULL);
                    KeLowerIrql (OldIrql);
                    LOCK_PFN (OldIrql);
                    MiMakeSystemAddressValidPfn (PointerPte);
                    continue;
                }

                if (Pfn1->u3.e1.ReadInProgress == 1) {

                    //
                    // The page currently is being read in from the
                    // disk.  Treat this just like a valid PTE and
                    // return false.
                    //

                    ReturnValue = FALSE;
                    break;
                }

                ASSERT (!((Pfn1->OriginalPte.u.Soft.Prototype == 0) &&
                    (Pfn1->OriginalPte.u.Soft.Transition == 1)));

                MI_WRITE_INVALID_PTE (PointerPte, Pfn1->OriginalPte);

                ASSERT (Pfn1->OriginalPte.u.Hard.Valid == 0);

                ControlArea->NumberOfPfnReferences -= 1;
                ASSERT ((LONG)ControlArea->NumberOfPfnReferences >= 0);

                MiUnlinkPageFromList (Pfn1);

                MI_SET_PFN_DELETED (Pfn1);

                PageTableFrameIndex = Pfn1->u4.PteFrame;
                Pfn2 = MI_PFN_ELEMENT (PageTableFrameIndex);

                MiDecrementShareCountInline (Pfn2, PageTableFrameIndex);

                //
                // If the reference count for the page is zero, insert
                // it into the free page list, otherwise leave it alone
                // and when the reference count is decremented to zero
                // the page will go to the free list.
                //

                if (Pfn1->u3.e2.ReferenceCount == 0) {
                    MiReleasePageFileSpace (Pfn1->OriginalPte);
                    MiInsertPageInFreeList (MI_GET_PAGE_FRAME_FROM_TRANSITION_PTE (&PteContents));
                }
            }
            PointerPte += 1;

            if ((MiIsPteOnPdeBoundary(PointerPte)) && (LockHeld)) {

                //
                // Unlock PFN so large requests will not block other
                // threads on MP systems.
                //

                UNLOCK_PFN (OldIrql);
                LockHeld = FALSE;
            }
        }

        if (!LockHeld) {
            LockHeld = TRUE;
            LOCK_PFN (OldIrql);
        }

        ASSERT (MappedSubsection->DereferenceList.Flink == NULL);
        ASSERT ((MappedSubsection->NumberOfMappedViews >= 1) ||
                (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 1));

        MappedSubsection->NumberOfMappedViews -= 1;

        if ((MappedSubsection->NumberOfMappedViews == 0) &&
            (MappedSubsection->u.SubsectionFlags.SubsectionStatic == 0)) {

            //
            // Insert this subsection into the unused subsection list.
            //

            InsertTailList (&MmUnusedSubsectionList,
                            &MappedSubsection->DereferenceList);
            MI_UNUSED_SUBSECTIONS_COUNT_INSERT (MappedSubsection);
        }

        ASSERT (LockHeld);

        UNLOCK_PFN (OldIrql);
        LockHeld = FALSE;

nextrange:

        if ((LastSubsection != Subsection) && (ReturnValue)) {

            //
            // Get the next subsection in the list.
            //

            Subsection = Subsection->NextSubsection;
            PointerPte = Subsection->SubsectionBase;

        }
        else {

            //
            // The last range has been flushed, exit the top FOR loop
            // and return.
            //

            break;
        }
    }

    if (!LockHeld) {
        LOCK_PFN (OldIrql);
    }

    MiDecrementSubsections (FirstSubsection, FirstSubsection);
    MiDecrementSubsections (LastSubsection, LastSubsection);

    ASSERT ((LONG)ControlArea->NumberOfMappedViews >= 1);
    ControlArea->NumberOfMappedViews -= 1;

    ControlArea->u.Flags.BeingPurged = 0;

    //
    // Check to see if the control area should be deleted.  This
    // will release the PFN lock.
    //

    MiCheckControlArea (ControlArea, NULL, OldIrql);
    return ReturnValue;
}

BOOLEAN
MmFlushImageSection (
    IN PSECTION_OBJECT_POINTERS SectionPointer,
    IN MMFLUSH_TYPE FlushType
    )

/*++

Routine Description:

    This function determines if any views of the specified image section
    are mapped, and if not, flushes valid pages (even modified ones)
    from the specified section and returns any used pages to the free
    list.  This is accomplished by examining the prototype PTEs
    from the specified offset to the end of the section, and if
    any prototype PTEs are in the transition state, putting the
    prototype PTE back into its original state and putting the
    physical page on the free list.

Arguments:

    SectionPointer - Supplies a pointer to a section object pointers
                     within the FCB.

    FlushType - Supplies the type of flush to check for.  One of
                MmFlushForDelete or MmFlushForWrite.

Return Value:

    Returns TRUE if either no section exists for the file object or
    the section is not mapped and the purge was done, FALSE otherwise.

--*/

{
    PLIST_ENTRY Next;
    PCONTROL_AREA ControlArea;
    PLARGE_CONTROL_AREA LargeControlArea;
    KIRQL OldIrql;
    LOGICAL state;

    if (FlushType == MmFlushForDelete) {

        //
        // Do a quick check to see if there are any mapped views for
        // the data section.  If so, just return FALSE.
        //

        LOCK_PFN (OldIrql);
        ControlArea = (PCONTROL_AREA)(SectionPointer->DataSectionObject);
        if (ControlArea != NULL) {
            if ((ControlArea->NumberOfUserReferences != 0) ||
                (ControlArea->u.Flags.BeingCreated)) {
                UNLOCK_PFN (OldIrql);
                return FALSE;
            }
        }
        UNLOCK_PFN (OldIrql);
    }

    //
    // Check the status of the control area.  If the control area is in use
    // or the control area is being deleted, this operation cannot continue.
    //

    state = MiCheckControlAreaStatus (CheckImageSection,
                                      SectionPointer,
                                      FALSE,
                                      &ControlArea,
                                      &OldIrql);

    if (ControlArea == NULL) {
        return (BOOLEAN) state;
    }

    //
    // PFN LOCK IS NOW HELD!
    //

    //
    // Repeat until there are no more control areas - multiple control areas
    // for the same image section occur to support user global DLLs - these DLLs
    // require data that is shared within a session but not across sessions.
    // Note this can only happen for Hydra.
    //

    do {

        //
        // Set the being deleted flag and up the number of mapped views
        // for the segment.  Upping the number of mapped views prevents
        // the segment from being deleted and passed to the deletion thread
        // while we are forcing a delete.
        //

        ControlArea->u.Flags.BeingDeleted = 1;
        ControlArea->NumberOfMappedViews = 1;
        LargeControlArea = NULL;

        if (ControlArea->u.Flags.GlobalOnlyPerSession == 0) {
            NOTHING;
        }
        else if (IsListEmpty(&((PLARGE_CONTROL_AREA)ControlArea)->UserGlobalList)) {
            ASSERT (ControlArea ==
                    (PCONTROL_AREA)SectionPointer->ImageSectionObject);
        }
        else {

            //
            // Check if there's only one image section in this control area, so
            // we don't reference the section object pointers as the
            // MiCleanSection call may result in its deletion.
            //

            //
            // There are multiple control areas, bump the reference count
            // on one of them (doesn't matter which one) so that it can't
            // go away.  This ensures the section object pointers will stick
            // around even after the calls below so we can safely reloop to
            // flush any other remaining control areas.
            //

            ASSERT (ControlArea->u.Flags.GlobalOnlyPerSession == 1);

            Next = ((PLARGE_CONTROL_AREA)ControlArea)->UserGlobalList.Flink;

            LargeControlArea = CONTAINING_RECORD (Next,
                                                  LARGE_CONTROL_AREA,
                                                  UserGlobalList);
        
            ASSERT (LargeControlArea->u.Flags.GlobalOnlyPerSession == 1);

            LargeControlArea->NumberOfSectionReferences += 1;
        }

        //
        // This is a page file backed or image segment.  The segment is being
        // deleted, remove all references to the paging file and physical
        // memory.
        //

        UNLOCK_PFN (OldIrql);

        MiCleanSection (ControlArea, TRUE);

        //
        // Get the next Hydra control area.
        //

        if (LargeControlArea != NULL) {
            state = MiCheckControlAreaStatus (CheckImageSection,
                                              SectionPointer,
                                              FALSE,
                                              &ControlArea,
                                              &OldIrql);
            if (!ControlArea) {
                LOCK_PFN (OldIrql);
                LargeControlArea->NumberOfSectionReferences -= 1;
                MiCheckControlArea ((PCONTROL_AREA)LargeControlArea,
                                    NULL,
                                    OldIrql);
            }
            else {
                LargeControlArea->NumberOfSectionReferences -= 1;
                MiCheckControlArea ((PCONTROL_AREA)LargeControlArea,
                                    NULL,
                                    OldIrql);
                LOCK_PFN (OldIrql);
            }
        }
        else {
            state = TRUE;
            break;
        }

    } while (ControlArea);

    return (BOOLEAN) state;
}

VOID
MiFlushDirtyBitsToPfn (
    IN PMMPTE PointerPte,
    IN PMMPTE LastPte,
    IN PEPROCESS Process,
    IN BOOLEAN SystemCache
    )

{
    KIRQL OldIrql;
    MMPTE PteContents;
    PMMPFN Pfn1;
    PVOID Va;
    PMMPTE PointerPde;
    PMMPTE PointerPpe;
    PMMPTE PointerPxe;
    ULONG Waited;

    Va = MiGetVirtualAddressMappedByPte (PointerPte);
    LOCK_PFN (OldIrql);

    while (PointerPte <= LastPte) {

        PteContents = *PointerPte;

        if ((PteContents.u.Hard.Valid == 1) &&
            (MI_IS_PTE_DIRTY (PteContents))) {

            //
            // Flush the modify bit to the PFN database.
            //

            Pfn1 = MI_PFN_ELEMENT (PteContents.u.Hard.PageFrameNumber);

            MI_SET_MODIFIED (Pfn1, 1, 0x2);

            MI_SET_PTE_CLEAN (PteContents);

            //
            // No need to capture the PTE contents as we are going to
            // write the page anyway and the Modify bit will be cleared
            // before the write is done.
            //

            (VOID)KeFlushSingleTb (Va,
                                   FALSE,
                                   SystemCache,
                                   (PHARDWARE_PTE)PointerPte,
                                   PteContents.u.Flush);
        }

        Va = (PVOID)((PCHAR)Va + PAGE_SIZE);
        PointerPte += 1;

        if (MiIsPteOnPdeBoundary (PointerPte)) {

            PointerPde = MiGetPteAddress (PointerPte);

            while (PointerPte <= LastPte) {

                PointerPxe = MiGetPdeAddress (PointerPde);
                PointerPpe = MiGetPteAddress (PointerPde);

                if (!MiDoesPxeExistAndMakeValid (PointerPxe,
                                                 Process,
                                                 TRUE,
                                                 &Waited)) {

                    //
                    // No page directory parent page exists for this address.
                    //

                    PointerPxe += 1;
                    PointerPpe = MiGetVirtualAddressMappedByPte (PointerPxe);
                    PointerPde = MiGetVirtualAddressMappedByPte (PointerPpe);
                    PointerPte = MiGetVirtualAddressMappedByPte (PointerPde);
                }
                else if (!MiDoesPpeExistAndMakeValid (PointerPpe,
                                                 Process,
                                                 TRUE,
                                                 &Waited)) {

                    //
                    // No page directory page exists for this address.
                    //

                    PointerPpe += 1;
                    PointerPde = MiGetVirtualAddressMappedByPte (PointerPpe);
                    PointerPte = MiGetVirtualAddressMappedByPte (PointerPde);
                }
                else {

                    Waited = 0;

                    if (!MiDoesPdeExistAndMakeValid (PointerPde,
                                                     Process,
                                                     TRUE,
                                                     &Waited)) {

                        //
                        // No page table page exists for this address.
                        //

                        PointerPde += 1;

                        PointerPte = MiGetVirtualAddressMappedByPte (PointerPde);
                    }
                    else {

                        //
                        // If the PFN lock (and accordingly the WS mutex) was
                        // released and reacquired we must retry the operation.
                        //

                        if (Waited != 0) {
                            continue;
                        }

                        //
                        // The PFN lock has been held since we acquired the
                        // page directory parent, ie: this PTE we can operate on
                        // immediately.
                        //

                        break;
                    }
                }
            }

            Va = MiGetVirtualAddressMappedByPte (PointerPte);
        }
    }

    UNLOCK_PFN (OldIrql);
    return;
}

PSUBSECTION
MiGetSystemCacheSubsection (
    IN PVOID BaseAddress,
    OUT PMMPTE *ProtoPte
    )

{
    KIRQL OldIrql;
    PMMPTE PointerPte;
    PSUBSECTION Subsection;

    PointerPte = MiGetPteAddress (BaseAddress);

    LOCK_PFN (OldIrql);

    Subsection = MiGetSubsectionAndProtoFromPte (PointerPte, ProtoPte);
    UNLOCK_PFN (OldIrql);
    return Subsection;
}


LOGICAL
MiCheckProtoPtePageState (
    IN PMMPTE PrototypePte,
    IN LOGICAL PfnLockHeld,
    OUT PLOGICAL DroppedPfnLock
    )

/*++

Routine Description:

    Checks the state of the page containing the specified
    prototype PTE.

    If the page is valid or transition and has transition or valid prototype
    PTEs contained with it, TRUE is returned and the page is made valid
    (if transition).  Otherwise return FALSE indicating no prototype
    PTEs within this page are of interest.

Arguments:

    PrototypePte - Supplies a pointer to a prototype PTE within the page.

    DroppedPfnLock - Supplies a pointer to a logical this routine sets to
                     TRUE if the PFN lock is released & reacquired.

Return Value:

    TRUE if the page containing the proto PTE was made resident.
    FALSE if otherwise.

--*/

{
    PMMPTE PointerPte;
    MMPTE PteContents;
    PFN_NUMBER PageFrameIndex;
    PMMPFN Pfn;

    *DroppedPfnLock = FALSE;

#if (_MI_PAGING_LEVELS >= 3)

    //
    // First check whether the page directory page is present.  Since there
    // is no lazy loading of PPEs, the validity check alone is sufficient.
    //

    PointerPte = MiGetPdeAddress (PrototypePte);
    PteContents = *PointerPte;

    if (PteContents.u.Hard.Valid == 0) {
        return FALSE;
    }

#endif

    PointerPte = MiGetPteAddress (PrototypePte);

#if (_MI_PAGING_LEVELS < 3)

    if (PointerPte->u.Hard.Valid == 0) {
        MiCheckPdeForPagedPool (PrototypePte);
    }

#endif

    PteContents = *PointerPte;

    if (PteContents.u.Hard.Valid == 1) {
        PageFrameIndex = MI_GET_PAGE_FRAME_FROM_PTE (&PteContents);
        Pfn = MI_PFN_ELEMENT (PageFrameIndex);
        if (Pfn->u2.ShareCount != 1) {
            return TRUE;
        }
    }
    else if ((PteContents.u.Soft.Prototype == 0) &&
               (PteContents.u.Soft.Transition == 1)) {

        //
        // Transition, if on standby or modified, return FALSE.
        //

        PageFrameIndex = MI_GET_PAGE_FRAME_FROM_TRANSITION_PTE (&PteContents);
        Pfn = MI_PFN_ELEMENT (PageFrameIndex);
        if (Pfn->u3.e1.PageLocation >= ActiveAndValid) {
            if (PfnLockHeld) {
                MiMakeSystemAddressValidPfn (PrototypePte);
                *DroppedPfnLock = TRUE;
            }
            return TRUE;
        }
    }

    //
    // Page is not resident or is on standby / modified list.
    //

    return FALSE;
}
