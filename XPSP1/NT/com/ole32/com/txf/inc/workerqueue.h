//  Copyright (C) 1995-1999 Microsoft Corporation.  All rights reserved.
//
// workerqueue.h
//
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////
//// Thread Pool Services: header is in viper\src\inc
////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#include "threadpool.h"

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////
//// Timeout Services
////
//// Support for maintaining a large number of objects that have a like or
//// an approximate timeout duration.
////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

struct TIMEOUT_ENTRY
    {
    LONGLONG              m_deadline;           // timeout deadline: FILETIME format
    RTL_SPLAY_LINKS       m_links;              // links that connect this timeout entry to others
    WorkerQueueItem::PFN  m_pfn;
    PVOID                 m_pvContext;
    ULONG                 m_reserved;

    TIMEOUT_ENTRY(WorkerQueueItem::PFN pfn=NULL, PVOID pvContext=NULL)
        {
        RtlInitializeSplayLinks(&m_links);
        m_pfn = pfn;
        m_pvContext = pvContext;
        m_reserved = 0;
        }

    HRESULT TimeoutAfter(LONGLONG delta);
    // Time this entry out after the indicated delta from the current time

    void CancelTimeout();
    // Cancel any timeout that this entry presently has
    };
