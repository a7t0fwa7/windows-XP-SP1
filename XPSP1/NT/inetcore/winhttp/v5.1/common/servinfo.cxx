/*++

Copyright (c) 1996-1997 Microsoft Corporation

Module Name:

    servinfo.cxx

Abstract:

    Class implementation for global server info list

    Contents:
        INTERNET_HANDLE_OBJECT::GetServerInfo
        INTERNET_HANDLE_OBJECT::FindServerInfo
        ReleaseServerInfo
        INTERNET_HANDLE_OBJECT::PurgeServerInfoList
        CServerInfo::CServerInfo
        CServerInfo::~CServerInfo
        CServerInfo::Reference
        CServerInfo::Dereference
        CServerInfo::UpdateConnectTime
        CServerInfo::UpdateRTT
        CServerInfo::GetConnection
        CFsm_GetConnection::RunSM
        CServerInfo::GetConnection_Fsm
        CServerInfo::ReleaseConnection
        CServerInfo::RemoveWaiter
        (CServerInfo::FindKeepAliveConnection)
        (CServerInfo::KeepAliveWaiters)
        (CServerInfo::UpdateConnectionLimit)
        CServerInfo::PurgeKeepAlives
        ContainingServerInfo

Author:

    Richard L Firth (rfirth) 07-Oct-1996

Revision History:

    07-Oct-1996 rfirth
        Created

--*/

#include <wininetp.h>
#include <perfdiag.hxx>

//
// private macros
//

//#define CHECK_CONNECTION_COUNT() \
//    INET_ASSERT(!UnlimitedConnections() \
//        ? (TotalAvailableConnections() <= ConnectionLimit()) : TRUE)

#define CHECK_CONNECTION_COUNT()    /* NOTHING */

//#define RLF_DEBUG   1

#if INET_DEBUG
#ifdef RLF_DEBUG
#define DPRINTF dprintf
#else
#define DPRINTF (void)
#endif
#else
#define DPRINTF (void)
#endif


#define SOCK_FLAGS  (SF_ENCRYPT | SF_DECRYPT | SF_SECURE | SF_TUNNEL)


//
// functions
//


DWORD
INTERNET_HANDLE_OBJECT::GetServerInfo(
    IN LPSTR lpszHostName,
    IN DWORD dwServiceType,
    IN BOOL bDoResolution,
    OUT CServerInfo * * lplpServerInfo
    )

/*++

Routine Description:

    Finds or creates a CServerInfo entry

Arguments:

    lpszHostName    - pointer to server name to get info for

    dwServiceType   - type of service for which CServerInfo requested

    bDoResolution   - TRUE if we are to resolve host name

    lplpServerInfo  - pointer to created/found CServerInfo if successful

Return Value:

    DWORD
        Success - ERROR_SUCCESS

        Failure - ERROR_NOT_ENOUGH_MEMORY
                    Couldn't create the CServerInfo

                  ERROR_WINHTTP_NAME_NOT_RESOLVED
                    We were asked to resolve the name, but failed

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 Dword,
                 "GetServerInfo",
                 "%q, %s (%d), %B, %#x",
                 lpszHostName,
                 InternetMapService(dwServiceType),
                 dwServiceType,
                 bDoResolution,
                 lplpServerInfo
                 ));

    ICSTRING hostName(lpszHostName);
    CServerInfo * lpServerInfo = NULL;
    BOOL bCreated = FALSE;
    DWORD error = ERROR_SUCCESS;

    if (hostName.HaveString()) {
        hostName.MakeLowerCase();

        LPSTR lpszHostNameLower = hostName.StringAddress();

        if (!LockSerializedList(&_ServerInfoList))
        {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto quit;
        }

        lpServerInfo = FindServerInfo(lpszHostNameLower);

        if (lpServerInfo == NULL) 
        {
            lpServerInfo = New CServerInfo(&_ServerInfoList,
                                           lpszHostNameLower,
                                           &error,
                                           dwServiceType,
                                           GetMaxConnectionsPerServer(WINHTTP_OPTION_MAX_CONNS_PER_SERVER)
                                           );
            if (lpServerInfo != NULL)
            {
                if (error != ERROR_SUCCESS)
                {
                    delete lpServerInfo;
                    lpServerInfo = NULL;
                }
                else
                {
                    bCreated = TRUE;
                    // Reference this to keep it alive beyond the unlock/
                    lpServerInfo->Reference();
                }
            }
            else
            {
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        UnlockSerializedList(&_ServerInfoList);
    } else {

        //
        // failed to create ICSTRING
        //

        error = GetLastError();

        INET_ASSERT(error != ERROR_SUCCESS);

        lpServerInfo = NULL;
    }

    //
    // if we created a new CServerInfo and we are instructed to resolve the host
    // name then do it now, outside of the global server info list lock. This
    // operation may take some time
    //

    if (bDoResolution && (lpServerInfo != NULL)) {
        //error = lpServerInfo->ResolveHostName();
        if (error != ERROR_SUCCESS) {
            ReleaseServerInfo(lpServerInfo);
            lpServerInfo = NULL;
        }
    }

quit:
    *lplpServerInfo = lpServerInfo;

    DEBUG_LEAVE(error);

    return error;
}


CServerInfo *
INTERNET_HANDLE_OBJECT::FindServerInfo(
    IN LPSTR lpszHostName
    )

/*++

Routine Description:

    Walks the server info list looking for the requested server

Arguments:

    lpszHostName    - pointer to server name to find (IN LOWER CASE!)

Return Value:

    CServerInfo *
        Success - pointer to found list entry

        Failure - NULL

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 Pointer,
                 "FindServerInfo",
                 "%q",
                 lpszHostName
                 ));

    DWORD hashHostName = CalculateHashValue(lpszHostName);

    CServerInfo * lpServerInfo = NULL;
    BOOL found = FALSE;

    if (!LockSerializedList(&_ServerInfoList))
    {
        goto quit;
    }

    for (lpServerInfo = (CServerInfo *)HeadOfSerializedList(&_ServerInfoList);
        lpServerInfo != (CServerInfo *)SlSelf(&_ServerInfoList);
        lpServerInfo = lpServerInfo->Next()) {

        if (lpServerInfo->Match(hashHostName, lpszHostName)) {
            found = TRUE;
            break;
        }
    }

    if (!found) 
    {
        lpServerInfo = NULL;
    }

    // Need to keep this alive beyond the lock.
    if (lpServerInfo)
    {
        lpServerInfo->Reference();
    }
    UnlockSerializedList(&_ServerInfoList);

quit:
    DEBUG_LEAVE(lpServerInfo);
    return lpServerInfo;
}



VOID
ReleaseServerInfo(
    IN CServerInfo * lpServerInfo
    )

/*++

Routine Description:

    Release a CServerInfo by dereferencing it. If the reference count goes to
    zero, the CServerInfo will be destroyed

Arguments:

    lpServerInfo    - pointer to CServerInfo to release

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "ReleaseServerInfo",
                 "%#x [%q]",
                 lpServerInfo,
                 lpServerInfo->GetHostName()
                 ));

    lpServerInfo->Dereference();

    DEBUG_LEAVE(0);
}


VOID
INTERNET_HANDLE_OBJECT::PurgeServerInfoList(
    IN BOOL bForce
    )

/*++

Routine Description:

    Throw out any CServerInfo entries that have expired or any KEEP_ALIVE
    entries (for any CServerInfo) that have expired

Arguments:

    bForce  - TRUE if we forcibly remove entries which have not yet expired but
              which have a reference count of 1, else FALSE to remove only
              entries that have expired

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "PurgeServerInfoList",
                 "%B",
                 bForce
                 ));

    if (!LockSerializedList(&_ServerInfoList))
    {
        // Can't purge list if unable to obtain the lock.
        goto quit;
    }

    PLIST_ENTRY pEntry = HeadOfSerializedList(&_ServerInfoList);
    PLIST_ENTRY pPrevious = (PLIST_ENTRY)SlSelf(&_ServerInfoList);

    while (TRUE) {
        if (pEntry == (PLIST_ENTRY)SlSelf(&_ServerInfoList)) {
            break;
        }

        CServerInfo * pServerInfo;

        //pServerInfo = (CServerInfo *)pEntry;
        //pServerInfo = CONTAINING_RECORD(pEntry, CONNECTION_LIMIT, m_List);
        pServerInfo = ContainingServerInfo(pEntry);

        BOOL deleted = FALSE;

        if (pServerInfo->ReferenceCount() == 1) {
            if (bForce || pServerInfo->Expired()) {
//dprintf("purging server info entry for %q\n", pServerInfo->GetHostName());
                deleted = pServerInfo->Dereference();
            } else {
                pServerInfo->PurgeKeepAlives(PKA_NO_FORCE);
            }
        }
        if (!deleted) {
            pPrevious = pEntry;
        }
        pEntry = pPrevious->Flink;
    }

    UnlockSerializedList(&_ServerInfoList);

quit:
    DEBUG_LEAVE(0);
}


VOID
INTERNET_HANDLE_OBJECT::PurgeKeepAlives(
    IN DWORD dwForce
    )

/*++

Routine Description:

    Throw out any KEEP_ALIVE entries from any CServerInfo that have expired or
    which have failed authentication or which are unused, depending on dwForce

Arguments:

    dwForce - force to apply when purging. Value can be:

                PKA_NO_FORCE    - only purge timed-out sockets or sockets in
                                  close-wait state (default)

                PKA_NOW         - purge all sockets

                PKA_AUTH_FAILED - purge sockets that have been marked as failing
                                  authentication

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "PurgeKeepAlives",
                 "%s [%d]",
                 (dwForce == PKA_NO_FORCE) ? "NO_FORCE"
                 : (dwForce == PKA_NOW) ? "NOW"
                 : (dwForce == PKA_AUTH_FAILED) ? "AUTH_FAILED"
                 : "?",
                 dwForce
                 ));

    if (!LockSerializedList(&_ServerInfoList))
    {
        goto quit;
    }

    PLIST_ENTRY pEntry = HeadOfSerializedList(&_ServerInfoList);

    while (pEntry != (PLIST_ENTRY)SlSelf(&_ServerInfoList)) {

        CServerInfo * lpServerInfo = ContainingServerInfo(pEntry);

        lpServerInfo->PurgeKeepAlives(dwForce);
        pEntry = pEntry->Flink;
    }

    UnlockSerializedList(&_ServerInfoList);

quit:
    DEBUG_LEAVE(0);
}


//
// methods
//


CServerInfo::CServerInfo(
    IN SERIALIZED_LIST * ServerInfoList,
    IN LPSTR lpszHostName,
    OUT DWORD* pdwError,
    IN DWORD dwService,
    IN DWORD dwMaxConnections
    )

/*++

Routine Description:

    CServerInfo constructor

Arguments:

    lpszHostName        - server for which to create CServerInfo

    dwService           - which service to create CServerInfo for

    dwMaxConnections    - maximum number of simultaneous connections to this
                          server

Return Value:

    None.

--*/

{
    UNREFERENCED_PARAMETER(dwService);
    DEBUG_ENTER((DBG_OBJECTS,
                 None,
                 "CServerInfo::CServerInfo",
                 "%q, %s (%d), %d",
                 lpszHostName,
                 InternetMapService(dwService),
                 dwService,
                 dwMaxConnections
                 ));

    INIT_SERVER_INFO();

    m_ServerInfoList = ServerInfoList;
    *pdwError = ERROR_SUCCESS;

    InitializeListHead(&m_List);
    m_Expires = 0;
    m_Wrap = 0;
    m_ReferenceCount = 1;
    m_HostName = lpszHostName;
    if (!m_HostName.StringAddress())
    {
        goto error;
    }
    
    m_HostName.MakeLowerCase();
    m_Hash = CalculateHashValue(m_HostName.StringAddress());
    m_Services.Word = 0;
    m_HttpSupport.Word = 0;
    m_Flags.Word = 0;
    m_ProxyLink = NULL;

    INET_ASSERT(dwService == INTERNET_SERVICE_HTTP);
    SetHTTP();

    //
    // only initialize the keep-alive and connection limit lists if we are
    // creating the server info entry for a HTTP server (or CERN proxy)
    //

    //
    // BUGBUG - we only want to do this on demand
    //

    //if (IsHTTP()) {
    InitializeSerializedList(&m_KeepAliveList);
    SetKeepAliveListInitialized();

    //
    // the maximum number of connections per server is initialized to the
    // default (registry) value unless overridden by the caller
    //

    if (dwMaxConnections == 0) 
    {
        dwMaxConnections = DEFAULT_MAX_CONNECTIONS_PER_SERVER;
    }
    
    m_ConnectionLimit = dwMaxConnections;
    //} else {
    //    m_ConnectionLimit = UNLIMITED_CONNECTIONS;
    //}
    //dprintf("*** %s: limit = %d\n", GetHostName(), m_ConnectionLimit);
    //
    // BUGBUG - only create event if limiting connections. Need method to manage
    //          connection limit count/event creation
    //

    m_NewLimit = m_ConnectionLimit;
    m_ConnectionsAvailable = m_ConnectionLimit;
    //m_ActiveConnections = 0;
    m_LastActiveTime = 0;
    m_ConnectTime = (DWORD)-1;
    m_RTT = 0;
    m_dwError = ERROR_SUCCESS;

    //
    // add to the global list. We are assuming here that the caller has already
    // checked for dupes
    //

    if (!InsertAtHeadOfSerializedList(m_ServerInfoList, &m_List))
        *pdwError = ERROR_NOT_ENOUGH_MEMORY;

quit:
    DEBUG_LEAVE(0);
    return;

error:
    *pdwError = ERROR_NOT_ENOUGH_MEMORY;
    goto quit;
    
}


CServerInfo::~CServerInfo()

/*++

Routine Description:

    CServerInfo destructor

Arguments:

    None.

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_OBJECTS,
                 None,
                 "CServerInfo::~CServerInfo",
                 "{%q}",
                 GetHostName()
                 ));

    CHECK_SERVER_INFO();

    //GlobalServerInfoDeAllocCount++;

    // unlink if we have a nested obj
    if ( m_ProxyLink )
    {
        CServerInfo *pDerefObj = NULL;

        // will leak if unable to dereference
        if (LockSerializedList(m_ServerInfoList))
        {
            pDerefObj = m_ProxyLink;
            m_ProxyLink = NULL;
            UnlockSerializedList(m_ServerInfoList);
        }

        if (pDerefObj)
        {
            pDerefObj->Dereference();
        }
    }

    RemoveFromSerializedList(m_ServerInfoList, &m_List);

    INET_ASSERT(m_ReferenceCount == 0);

    if (IsKeepAliveListInitialized() && LockSerializedList(&m_KeepAliveList))
    {
        while (!IsSerializedListEmpty(&m_KeepAliveList))
        {
//dprintf("%#x ~S-I killing K-A %#x\n", GetCurrentThreadId(), HeadOfSerializedList(&m_KeepAliveList));

            LPVOID pEntry = SlDequeueHead(&m_KeepAliveList);

            INET_ASSERT(pEntry != NULL);

            if (pEntry != NULL) {

                ICSocket * pSocket = ContainingICSocket(pEntry);

//dprintf("~CServerInfo: destroying socket %#x\n", pSocket->GetSocket());
                pSocket->Destroy();
            }
        }
        UnlockSerializedList(&m_KeepAliveList);
        TerminateSerializedList(&m_KeepAliveList);
    }

    DEBUG_LEAVE(0);
}


VOID
CServerInfo::Reference(
    VOID
    )

/*++

Routine Description:

    Increments the reference count for the CServerInfo

Arguments:

    None.

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::Reference",
                 "{%q}",
                 GetHostName()
                 ));

    CHECK_SERVER_INFO();
    INET_ASSERT(m_ReferenceCount > 0);

    InterlockedIncrement(&m_ReferenceCount);
//dprintf("CServerInfo %s - %d\n", GetHostName(), m_ReferenceCount);

    DEBUG_PRINT(SESSION,
                INFO,
                ("Reference count = %d\n",
                ReferenceCount()
                ));

    DEBUG_LEAVE(0);
}


BOOL
CServerInfo::Dereference(
    VOID
    )

/*++

Routine Description:

    Dereferences the SESSION_INFO. If the reference count goes to zero then this
    entry is deleted. If the reference count goes to 1 then the expiry timer is
    started

Arguments:

    None.

Return Value:

    BOOL
        TRUE    - entry was deleted

        FALSE   - entry was not deleted

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::Dereference",
                 "{%q}",
                 GetHostName()
                 ));

    CHECK_SERVER_INFO();
    INET_ASSERT(m_ReferenceCount > 0);

    //
    // we need to grab the list - we may be removing this entry or updating
    // the reference count and expiry fields which must be done atomically
    //

    SERIALIZED_LIST *   ServerInfoList = m_ServerInfoList;
    BOOL deleted = FALSE;

    if (!LockSerializedList(ServerInfoList))
        goto quit;

    LONG result = InterlockedDecrement(&m_ReferenceCount);
//dprintf("CServerInfo %s - %d\n", GetHostName(), m_ReferenceCount);

    DEBUG_PRINT(SESSION,
                INFO,
                ("Reference count = %d\n",
                ReferenceCount()
                ));

    if (result == 0) {
        delete this;
        deleted = TRUE;
    } else if (result == 1) {

        //
        // start expiration proceedings...
        //

        SetExpiryTime();
    }

    UnlockSerializedList(ServerInfoList);

quit:
    DEBUG_LEAVE(deleted);

    return deleted;
}


DWORD    
CServerInfo::SetCachedProxyServerInfo(
    IN CServerInfo * pProxyServer,
    IN DWORD dwProxyVersion,
    IN BOOL fUseProxy,
    IN INTERNET_SCHEME HostScheme,
    IN INTERNET_PORT HostPort,
    IN INTERNET_SCHEME ProxyScheme,
    IN INTERNET_PORT ProxyPort
    )
/*++

Routine Description:

    If the Version information match up, copies
     the proxy information and links this server object
     to the appopriate proxy server object

    Assumes that this is called on successful use of the proxy
      object.

Arguments:

    None.

Return Value:

    DWORD
        ERROR_SUCCESS

        ERROR_NOT_ENOUGH_MEMORY   - entry was not deleted because there
                                    wasn't available memory to obtain lock

--*/


{
    DWORD error=ERROR_SUCCESS;

    if (!LockSerializedList(m_ServerInfoList))
    {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto quit;
    }

    if ( dwProxyVersion != GlobalProxyVersionCount ) 
    {
        SetProxyScriptCached(FALSE);
        goto cleanup; // bail, we don't accept out of date additions to the cache
    }

    if ( m_ProxyLink )
    {
        if ( IsProxyScriptCached() && 
             HostScheme == m_HostScheme &&
             HostPort == m_HostPort &&
             fUseProxy )
        {
            if ( pProxyServer == m_ProxyLink ) {            
                INET_ASSERT(dwProxyVersion == GlobalProxyVersionCount);
                m_dwProxyVersion = dwProxyVersion; // we're now up to date
                goto cleanup; // match, no version or host changes
            }

            INET_ASSERT(pProxyServer != m_ProxyLink );            
        }
        //
        // unlink, because we have a new entry to save,
        //  and the previous entry is bad
        //
        m_ProxyLink->Dereference();
        m_ProxyLink = NULL;
    }

    //
    // Add new cached entry
    //

    SetProxyScriptCached(TRUE);

    m_HostScheme     = HostScheme;
    m_HostPort       = HostPort;

    m_dwProxyVersion = dwProxyVersion; // we're now up to date

    if ( fUseProxy )
    {
        INET_ASSERT(this != pProxyServer);

        m_ProxyLink = pProxyServer;
        m_ProxyLink->Reference();

        m_ProxyLink->m_HostScheme = ProxyScheme;
        m_ProxyLink->m_HostPort   = ProxyPort;

        switch (ProxyScheme)
        {
            case INTERNET_SCHEME_HTTP:
                m_ProxyLink->SetCernProxy();
                break;
            case INTERNET_SCHEME_SOCKS: 
                m_ProxyLink->SetSocksGateway();
                break;
        }
    }

cleanup:

    UnlockSerializedList(m_ServerInfoList);        

quit:

    return error;
}

CServerInfo * 
CServerInfo::GetCachedProxyServerInfo(
    IN INTERNET_SCHEME HostScheme,
    IN INTERNET_PORT HostPort,
    OUT BOOL *pfCachedEntry
    )

/*++

Routine Description:

   Retrieves a cached server object, that indicates
    a probable proxy to use

   On Success, the return has an additional increment
    on its ref count, assumition that caller derefs

Arguments:

    None.

Return Value:

    CServerInfo *     
        NULL on failure

--*/

{
    CServerInfo *pProxyServer = NULL;

    if (!LockSerializedList(m_ServerInfoList))
        return NULL;

    *pfCachedEntry = FALSE; 

    if ( IsProxyScriptCached() )
    {        
        //
        // Examine Version Count
        //

        if ( GlobalProxyVersionCount == m_dwProxyVersion &&
             HostScheme == m_HostScheme &&
             HostPort == m_HostPort
             )
        {
            *pfCachedEntry = TRUE;

            if ( m_ProxyLink ) {
                // matched cached entry
                m_ProxyLink->Reference();
                pProxyServer = m_ProxyLink;                    
            }
        }
        else
        {
            // version is expired, remove reference
            SetProxyScriptCached(FALSE);
            if ( m_ProxyLink ) {                
                m_ProxyLink->Dereference();
                m_ProxyLink = NULL;
            }
        }            
    }
        
    UnlockSerializedList(m_ServerInfoList);        
    return pProxyServer;
}

BOOL    
CServerInfo::CopyCachedProxyInfoToProxyMsg(
    IN OUT AUTO_PROXY_ASYNC_MSG *pQueryForProxyInfo
    )

/*++

Routine Description:

   Retrieves Cached Proxy info from object

Arguments:

    None.

Return Value:

    BOOL
        TRUE - sucess

--*/

{
    BOOL fSuccess = FALSE;

    // really only need to lock to proctect m_HostPort && m_HostScheme
    if (!LockSerializedList(m_ServerInfoList))
        return FALSE;

    pQueryForProxyInfo->SetUseProxy(FALSE);
    pQueryForProxyInfo->_lpszProxyHostName =  
        m_HostName.StringAddress() ? 
        NewString(m_HostName.StringAddress()) :
        NULL;

    if ( pQueryForProxyInfo->_lpszProxyHostName != NULL ) {
        // copy out cached entry to proxy message structure
        pQueryForProxyInfo->_nProxyHostPort        = m_HostPort;
        pQueryForProxyInfo->_tProxyScheme          = m_HostScheme;
        pQueryForProxyInfo->_bFreeProxyHostName    = TRUE;
        pQueryForProxyInfo->_dwProxyHostNameLength = 
            strlen((pQueryForProxyInfo)->_lpszProxyHostName);
        pQueryForProxyInfo->SetUseProxy(TRUE);
        fSuccess = TRUE; // success
    }

    UnlockSerializedList(m_ServerInfoList);        
    return fSuccess;
}




VOID
CServerInfo::UpdateConnectTime(
    IN DWORD dwConnectTime
    )

/*++

Routine Description:

    Calculates average connect time

Arguments:

    dwConnectTime   - current connect time

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::UpdateConnectTime",
                 "{%q} %d",
                 GetHostName(),
                 dwConnectTime
                 ));

    DWORD connectTime = m_ConnectTime;

    if (connectTime == (DWORD)-1) {
        connectTime = dwConnectTime;
    } else {
        connectTime = (connectTime + dwConnectTime) / 2;
    }
//dprintf("%s: connect time = %d, ave = %d\n", GetHostName(), dwConnectTime, connectTime);

    DEBUG_PRINT(SESSION,
                INFO,
                ("average connect time = %d mSec\n",
                connectTime
                ));

    InterlockedExchange((LPLONG)&m_ConnectTime, connectTime);

    DEBUG_LEAVE(0);
}


VOID
CServerInfo::UpdateRTT(
    IN DWORD dwRTT
    )

/*++

Routine Description:

    Calculates rolling average round-trip time

Arguments:

    dwRTT   - current round-trip time

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::UpdateRTT",
                 "{%q} %d",
                 GetHostName(),
                 dwRTT
                 ));

    DWORD RTT = m_RTT;

    if (RTT == 0) {
        RTT = dwRTT;
    } else {
        RTT = (RTT + dwRTT) / 2;
    }
//dprintf("%s: RTT = %d, ave = %d\n", GetHostName(), dwRTT, RTT);

    DEBUG_PRINT(SESSION,
                INFO,
                ("average round trip time = %d mSec\n",
                RTT
                ));

    InterlockedExchange((LPLONG)&m_RTT, RTT);

    DEBUG_LEAVE(0);
}



DWORD
CFsm_GetConnection::RunSM(
    IN CFsm * Fsm
    )

/*++

Routine Description:

    Runs next CFsm_GetConnection state

Arguments:

    Fsm - FSM controlling operation

Return Value:

    DWORD
        Success - ERROR_SUCCESS

        Failure -

--*/

{
//dprintf("%#x: %s FSM %#x state %s\n", GetCurrentThreadId(), Fsm->MapType(), Fsm, Fsm->MapState());
    DEBUG_ENTER((DBG_SESSION,
                 Dword,
                 "CFsm_GetConnection::RunSM",
                 "%#x",
                 Fsm
                 ));

    CServerInfo * pServerInfo = (CServerInfo *)Fsm->GetContext();
    CFsm_GetConnection * stateMachine = (CFsm_GetConnection *)Fsm;
    DWORD error;

    switch (Fsm->GetState()) {
    case FSM_STATE_INIT:
        stateMachine->StartTimer();

        //
        // fall through
        //

    case FSM_STATE_CONTINUE:

#ifdef NEW_CONNECTION_SCHEME
    case FSM_STATE_ERROR:
#endif
        error = pServerInfo->GetConnection_Fsm(stateMachine);
        break;

#ifndef NEW_CONNECTION_SCHEME

    case FSM_STATE_ERROR:

        INET_ASSERT((Fsm->GetError() == ERROR_WINHTTP_TIMEOUT)
                    || (Fsm->GetError() == ERROR_WINHTTP_OPERATION_CANCELLED));

        pServerInfo->RemoveWaiter((DWORD_PTR)Fsm);
        error = Fsm->GetError();
        Fsm->SetDone();
//dprintf("%#x: FSM_STATE_ERROR - %d\n", GetCurrentThreadId(), error);
        break;

#endif

    default:
        error = ERROR_WINHTTP_INTERNAL_ERROR;
        Fsm->SetDone(ERROR_WINHTTP_INTERNAL_ERROR);

        INET_ASSERT(FALSE);

        break;
    }

    DEBUG_LEAVE(error);

    return error;
}



DWORD
CServerInfo::GetConnection_Fsm(
    IN CFsm_GetConnection * Fsm
    )

/*++

Routine Description:

    Tries to get a connection of requested type for caller. If no connection is
    available then one of the following happens:

        * If there are available keep-alive connections of a different type then
          one is closed and the caller allowed to create a new connection

        * If this is an async request, the FSM is blocked and the thread returns
          to the pool if a worker, or back to the app if an app thread

        * If this is a sync request, we wait on an event for a connection to be
          made available, or the connect timeout to elapse

Arguments:

    Fsm - get connection FSM

Return Value:

    DWORD
        Success - ERROR_SUCCESS
                    Depending on *lplpSocket, we either returned the socket to
                    use, or its okay to create a new connection

                  ERROR_IO_PENDING
                    Request will complete asynchronously

        Failure - ERROR_WINHTTP_TIMEOUT
                    Failed to get connection in time allowed

                  ERROR_WINHTTP_INTERNAL_ERROR
                    Something unexpected happened

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 Dword,
                 "CServerInfo::GetConnection_Fsm",
                 "{%q [%d+%d/%d]} %#x(%#x, %d, %d)",
                 GetHostName(),
                 m_ConnectionsAvailable,
                 ElementsOnSerializedList(&m_KeepAliveList),
                 m_ConnectionLimit,
                 Fsm,
                 Fsm->m_dwSocketFlags,
                 Fsm->m_nPort,
                 Fsm->m_dwTimeout
                 ));

    PERF_ENTER(GetConnection);

    DWORD error = ERROR_SUCCESS;
    CFsm_GetConnection & fsm = *Fsm;
    ICSocket * pSocket = NULL;
    LPINTERNET_THREAD_INFO lpThreadInfo = fsm.GetThreadInfo();
    HANDLE hEvent = NULL;
    BOOL bUnlockList = TRUE;
    BOOL bKeepAliveWaiters;

    INET_ASSERT(lpThreadInfo != NULL);
    INET_ASSERT(lpThreadInfo->hObjectMapped != NULL);
    INET_ASSERT(((HANDLE_OBJECT *)lpThreadInfo->hObjectMapped)->
                GetHandleType() == TypeHttpRequestHandle);

    if ((lpThreadInfo == NULL) || (lpThreadInfo->hObjectMapped == NULL)) {
        error = ERROR_WINHTTP_INTERNAL_ERROR;
        goto quit;
    }

    BOOL bAsyncRequest;

    bAsyncRequest = lpThreadInfo->IsAsyncWorkerThread
                    || ((INTERNET_HANDLE_OBJECT *)lpThreadInfo->hObjectMapped)->
                        IsAsyncHandle();

    *fsm.m_lplpSocket = NULL;

try_again:

    bUnlockList = TRUE;

    //
    // use m_Waiters to serialize access. N.B. - we will acquire m_KeepAliveList
    // from within m_Waiters
    //

    if (!m_Waiters.Acquire()) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto quit;
    }

    if (IsNewLimit()) {
        UpdateConnectionLimit();
    }
    bKeepAliveWaiters = KeepAliveWaiters();
    if (fsm.m_dwSocketFlags & SF_KEEP_ALIVE) {

        //
        // maintain requester order - if there are already waiters then queue
        // this request, else try to satisfy the requester. HOWEVER, only check
        // for existing requesters the FIRST time through. If we're here with
        // FSM_STATE_CONTINUE then we've been unblocked and we can ignore any
        // waiters that came after us
        //

        if ((fsm.GetState() == FSM_STATE_CONTINUE) || !bKeepAliveWaiters) {

            DEBUG_PRINT(SESSION,
                        INFO,
                        ("no current waiters for K-A connections\n"
                        ));


            if (g_pGlobalServerInfoPool != NULL           &&
                UnlimitedConnections())
            {
                CServerInfo * pGlobalServerInfo = g_pGlobalServerInfoPool->FindServerInfo(GetHostName());

                if (pGlobalServerInfo)
                {
                    while ((pSocket = pGlobalServerInfo->FindKeepAliveConnection(
                                        fsm.m_dwSocketFlags,
                                        fsm.m_nPort,
                                        fsm.m_lpszSecureTunnelHost,
                                        fsm.m_dwSecureProtocols)) != NULL)
                    {
                        INET_ASSERT(pSocket->IsInGlobalKeepAlivePool());

                        if (pSocket->HasExpired() || pSocket->IsReset())
                        {

                            DEBUG_PRINT(SESSION,
                                        INFO,
                                        ("Global K-A connection %#x [%#x/%d] is reset (%B) or expired (%B)\n",
                                        pSocket,
                                        pSocket->GetSocket(),
                                        pSocket->GetSourcePort(),
                                        pSocket->IsReset(),
                                        pSocket->HasExpired()
                                        ));

                            pSocket->SetLinger(FALSE, 0);
                            pSocket->Shutdown(2);
                            pSocket->Destroy();
                            pSocket = NULL;
                        }
                        else
                        {
                            DPRINTF("%#x: %#x: *** matched %#x, %#x\n",
                                    GetCurrentThreadId(),
                                    Fsm,
                                    pSocket->GetSocket(),
                                    pSocket->GetFlags()
                                    );
                            //
                            // Push out this global serverinfo's expiry time to
                            // current time + GlobalServerInfoTimeout.
                            //
                            pGlobalServerInfo->SetExpiryTime();

                            break;
                        }
                    }

                    ReleaseServerInfo(pGlobalServerInfo);
                }
            }
            
            if (pSocket == NULL)
            {
                while ((pSocket = FindKeepAliveConnection(fsm.m_dwSocketFlags,
                                                         fsm.m_nPort,
                                                         fsm.m_lpszSecureTunnelHost,
                                                         fsm.m_dwSecureProtocols))!= NULL)
                {
                    INET_ASSERT(!(pSocket->IsInGlobalKeepAlivePool()));

                    if (pSocket->IsReset() || pSocket->HasExpired()) {

                        DPRINTF("%#x: %#x: ********* socket %#x is closed already\n",
                                GetCurrentThreadId(),
                                Fsm,
                                pSocket->GetSocket()
                                );

                        DEBUG_PRINT(SESSION,
                                    INFO,
                                    ("K-A connection %#x [%#x/%d] is reset (%B) or expired (%B)\n",
                                    pSocket,
                                    pSocket->GetSocket(),
                                    pSocket->GetSourcePort(),
                                    pSocket->IsReset(),
                                    pSocket->HasExpired()
                                    ));

                        pSocket->SetLinger(FALSE, 0);
                        pSocket->Shutdown(2);
    //dprintf("GetConnection: destroying reset socket %#x\n", pSocket->GetSocket());
                        pSocket->Destroy();
                        pSocket = NULL;
                        if (!UnlimitedConnections()) {
                            ++m_ConnectionsAvailable;
                        }
                        CHECK_CONNECTION_COUNT();
                    } else {

                        DPRINTF("%#x: %#x: *** matched %#x, %#x\n",
                                GetCurrentThreadId(),
                                Fsm,
                                pSocket->GetSocket(),
                                pSocket->GetFlags()
                                );

                        break;
                    }
                }
            }
            if (pSocket == NULL) {

                DEBUG_PRINT(SESSION,
                            INFO,
                            ("no available K-A connections\n"
                            ));

                /*
                //
                // if all connections are in use as keep-alive connections then
                // since we're here, we want a keep-alive connection that doesn't
                // match the currently available keep-alive connections. Terminate
                // the oldest keep-alive connection (at the head of the queue)
                // and generate a new connection
                //

                LockSerializedList(&m_KeepAliveList);
                if (ElementsOnSerializedList(&m_KeepAliveList) == m_ConnectionLimit) {
                    pSocket = ContainingICSocket(SlDequeueHead(&m_KeepAliveList));
                    pSocket->SetLinger(FALSE, 0);
                    pSocket->Shutdown(2);
                    pSocket->Destroy();
                    if (!UnlimitedConnections()) {
                        ++m_ConnectionsAvailable;
                    }
                    CHECK_CONNECTION_COUNT();
                }
                UnlockSerializedList(&m_KeepAliveList);
                */
            }
        } else {

            DEBUG_PRINT(SESSION,
                        INFO,
                        ("%d waiters for K-A connection to %q\n",
                        ElementsOnSerializedList(&m_KeepAliveList),
                        GetHostName()
                        ));

        }
    }

    //
    // if we found a matching keep-alive connection or we are not limiting
    // connections then we're done
    //

    if ((pSocket != NULL) || UnlimitedConnections()) {

        INET_ASSERT(error == ERROR_SUCCESS);

        goto exit;
    }

    //
    // no keep-alive connections matched, or there are already waiters for
    // keep-alive connections
    //

    INET_ASSERT(m_ConnectionsAvailable <= m_ConnectionLimit);

    if (m_ConnectionsAvailable > 0) {

        if (fsm.m_lpszSecureTunnelHost)
            goto exit;  // don't create a connection here for SSL tunneling

        //
        // can create a connection
        //

        DEBUG_PRINT(SESSION,
                    INFO,
                    ("OK to create new connection\n"
                    ));

        DPRINTF("%#x: %#x: *** %s OK to create connection %d/%d\n",
                GetCurrentThreadId(),
                Fsm,
                GetHostName(),
                m_ConnectionsAvailable,
                m_ConnectionLimit
                );

        --m_ConnectionsAvailable;
    } else if (fsm.GetElapsedTime() > fsm.m_dwTimeout) {
        error = ERROR_WINHTTP_TIMEOUT;
    } else {
        
        //
        // if there are keep-alive connections but no keep-alive waiters
        // then either we don't want a keep-alive connection, or the ones
        // available don't match our requirements.
        // If we need a connection of a different type - e.g. SSL when all
        // we have is non-SSL then close a connection & generate a new one.
        // If we need a non-keep-alive connection then its okay to return
        // a current keep-alive connection, the understanding being that the
        // caller will not add Connection: Keep-Alive header (HTTP 1.0) or
        // will add Connection: Close header (HTTP 1.1)
        //

        //
        // BUGBUG - what about waiters for non-keep-alive connections?
        //
        // scenario - limit of 1 connection:
        //
        //  A. request for k-a
        //      continue & create connection
        //  B. request non-k-a
        //      none available; wait
        //  C. release k-a connection; unblock sync waiter B
        //  D. request non-k-a
        //      k-a available; return it; caller converts to non-k-a
        //  E. unblocked waiter B request non-k-a
        //      none available; wait
        //
        // If this situation continues, eventually B will time-out, whereas it
        // could have had the connection taken by D. Request D is younger and
        // therefore can afford to wait while B continues with the connection
        //

        BOOL fHaveConnection = FALSE;

        if (!bKeepAliveWaiters || (fsm.GetState() == FSM_STATE_CONTINUE)) {
            if (!LockSerializedList(&m_KeepAliveList)) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                goto exit;
            }
            if (ElementsOnSerializedList(&m_KeepAliveList) != 0) {
                pSocket = ContainingICSocket(SlDequeueHead(&m_KeepAliveList));
                fHaveConnection = TRUE;

                DWORD dwSocketTypeFlags = pSocket->GetFlags() & SOCK_FLAGS;
                DWORD dwRequestTypeFlags = fsm.m_dwSocketFlags & SOCK_FLAGS;

                if ((dwSocketTypeFlags ^ dwRequestTypeFlags)
                    || (fsm.m_nPort != pSocket->GetPort())) {

                    DEBUG_PRINT(SESSION,
                                INFO,
                                ("different socket types (%#x, %#x) or ports (%d, %d) requested\n",
                                fsm.m_dwSocketFlags,
                                pSocket->GetFlags(),
                                fsm.m_nPort,
                                pSocket->GetPort()
                                ));

                    DPRINTF("%#x: %#x: *** closing socket %#x: %#x vs. %#x\n",
                            GetCurrentThreadId(),
                            Fsm,
                            pSocket->GetSocket(),
                            pSocket->GetFlags(),
                            fsm.m_dwSocketFlags
                            );

                    pSocket->SetLinger(FALSE, 0);
                    pSocket->Shutdown(2);
//dprintf("GetConnection: destroying different type socket %#x\n", pSocket->GetSocket());
                    pSocket->Destroy();
                    pSocket = NULL;
                    // If we were trying to wait for established SSL tunnel,
                    // but one wasn't found, then this connection is open
                    // for anyone.
                    if (!UnlimitedConnections() && fsm.m_lpszSecureTunnelHost) {
                        ++m_ConnectionsAvailable;
                    }
                } else {

                    DPRINTF("%#x: %#x: *** returning k-a connection %#x as non-k-a\n",
                            GetCurrentThreadId(),
                            Fsm,
                            pSocket->GetSocket()
                            );

                }
                CHECK_CONNECTION_COUNT();
            }
            UnlockSerializedList(&m_KeepAliveList);
            if (fHaveConnection) {
                goto exit;
            }
        }

        DPRINTF("%#x: %#x: blocking %s FSM %#x state %s %d/%d\n",
                GetCurrentThreadId(),
                Fsm,
                Fsm->MapType(),
                Fsm,
                Fsm->MapState(),
                m_ConnectionsAvailable,
                m_ConnectionLimit
                );

        //
        // we have to wait for a connection to become available. If we are an
        // async request then we queue this FSM & return the thread to the pool
        // or, if app thread, return pending indication to the app. If this is
        // a sync request (in an app thread) then we block on an event waiting
        // for a connection to become available
        //

        if (!bAsyncRequest) {

            //
            // create unnamed, initially unsignalled, auto-reset event
            //

            hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
            if (hEvent == NULL) {
                error = GetLastError();
                goto exit;
            }
        }

        CConnectionWaiter * pWaiter;
        DWORD dwStatus = ERROR_SUCCESS;

#if INET_DEBUG

        for (pWaiter = (CConnectionWaiter *)m_Waiters.Head();
             pWaiter != (CConnectionWaiter *)m_Waiters.Self();
             pWaiter = (CConnectionWaiter *)pWaiter->Next()) {

            INET_ASSERT(pWaiter->Id() != (DWORD_PTR)(bAsyncRequest ? (DWORD_PTR)Fsm : lpThreadInfo->ThreadId));
        }
#endif

        pWaiter = New CConnectionWaiter(&m_Waiters,
                                        !bAsyncRequest,
                                        (fsm.m_dwSocketFlags & SF_KEEP_ALIVE)
                                            ? TRUE
                                            : FALSE,
                                        bAsyncRequest
                                            ? (DWORD_PTR)Fsm
                                            : lpThreadInfo->ThreadId,
                                        hEvent,

                                        //
                                        // priority in request handle object
                                        // controls relative position in list
                                        // of waiters
                                        //

                                        ((HTTP_REQUEST_HANDLE_OBJECT *)
                                            lpThreadInfo->hObjectMapped)->
                                                GetPriority(),

                                        &dwStatus
                                        );

        DPRINTF("%#x: %#x: new waiter %#x: as=%B, K-A=%B, id=%#x, hE=%#x, pri=%d, status=%#x, sf=%#x, preq=%#x ssl=%s\n",
                GetCurrentThreadId(),
                Fsm,
                pWaiter,
                bAsyncRequest,
                (fsm.m_dwSocketFlags & SF_KEEP_ALIVE)
                    ? TRUE
                    : FALSE,
                bAsyncRequest
                    ? (DWORD_PTR)Fsm
                    : lpThreadInfo->ThreadId,
                hEvent,
                ((HTTP_REQUEST_HANDLE_OBJECT *)lpThreadInfo->hObjectMapped)->
                    GetPriority(),
                dwStatus,
                fsm.m_dwSocketFlags,
                ((HTTP_REQUEST_HANDLE_OBJECT *)lpThreadInfo->hObjectMapped),
                fsm.m_lpszSecureTunnelHost ? fsm.m_lpszSecureTunnelHost : ""
                );

        if (pWaiter == NULL) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto exit;
        }
        else if (dwStatus != ERROR_SUCCESS) {
            error = dwStatus;
            delete pWaiter;  // free since it wasn't inserted
            goto exit;
        }
        
        if (bAsyncRequest) {

            //
            // ensure that when the FSM is unblocked normally, the new state
            // is STATE_CONTINUE
            //

            Fsm->SetState(FSM_STATE_CONTINUE);
            error = BlockWorkItem(Fsm,
                                  (DWORD_PTR)pWaiter,
                                  fsm.m_dwTimeout
                                  );
            if (error == ERROR_SUCCESS) {
                error = ERROR_IO_PENDING;
            }
        } 
        else
        {
            m_Waiters.Release();
            bUnlockList = FALSE;

            DPRINTF("%#x: %#x: %s FSM %#x %s waiting %d msec\n",
                    GetCurrentThreadId(),
                    Fsm,
                    Fsm->MapType(),
                    Fsm,
                    Fsm->MapState(),
                    fsm.m_dwTimeout
                    );

            DWORD dwWaitTime = (fsm.m_dwTimeout != INFINITE) ? 
                                (fsm.m_dwTimeout - fsm.GetElapsedTime()) : 
                                  INFINITE;

            if (((int)dwWaitTime <= 0) && (dwWaitTime != INFINITE)) {

                DEBUG_PRINT(SESSION,
                            ERROR,
                            ("SYNC wait timed out (%d mSec)\n",
                            fsm.m_dwTimeout
                            ));

                error = ERROR_WINHTTP_TIMEOUT;
            } else {

                DEBUG_PRINT(SESSION,
                            INFO,
                            ("waiting %d mSec for SYNC event %#x\n",
                            dwWaitTime,
                            hEvent
                            ));

                //
                // we'd better not be doing a sync wait if we are in the
                // context of an app thread making an async request
                //

                INET_ASSERT(lpThreadInfo->IsAsyncWorkerThread
                            || !((INTERNET_HANDLE_OBJECT *)lpThreadInfo->
                                hObjectMapped)->IsAsyncHandle());

                //INET_ASSERT(dwWaitTime <= 60000);

                error = WaitForSingleObject(hEvent, dwWaitTime);

                DPRINTF("%#x: %#x: sync waiter unblocked - error = %d\n",
                        GetCurrentThreadId(),
                        Fsm,
                        error
                        );

            }
            if (error == STATUS_TIMEOUT) {

                DPRINTF("%#x: %#x: %s: %d+%d/%d: timed out %#x (%s FSM %#x %s)\n",
                        GetCurrentThreadId(),
                        Fsm,
                        GetHostName(),
                        m_ConnectionsAvailable,
                        ElementsOnSerializedList(&m_KeepAliveList),
                        m_ConnectionLimit,
                        GetCurrentThreadId(),
                        Fsm->MapType(),
                        Fsm,
                        Fsm->MapState()
                        );

                RemoveWaiter(lpThreadInfo->ThreadId);
                error = ERROR_WINHTTP_TIMEOUT;
            }

            BOOL bOk;

            bOk = CloseHandle(hEvent);

            INET_ASSERT(bOk);

            if (error == WAIT_OBJECT_0) {

                DPRINTF("%#x: %#x: sync requester trying again\n",
                        GetCurrentThreadId(),
                        Fsm
                        );

                fsm.SetState(FSM_STATE_CONTINUE);
                goto try_again;
            }
        }
    }

exit:

    //
    // if we are returning a (keep-alive) socket that has a different blocking
    // mode from that requested, change it
    //

    if (pSocket != NULL) {
        if ((pSocket->GetFlags() & SF_NON_BLOCKING)
            ^ (fsm.m_dwSocketFlags & SF_NON_BLOCKING)) {

            DEBUG_PRINT(SESSION,
                        INFO,
                        ("different blocking modes requested: %#x, %#x\n",
                        fsm.m_dwSocketFlags,
                        pSocket->GetFlags()
                        ));

            DPRINTF("%#x: %#x: *** changing socket %#x to %sBLOCKING\n",
                    GetCurrentThreadId(),
                    Fsm,
                    pSocket->GetSocket(),
                    fsm.m_dwSocketFlags & SF_NON_BLOCKING ? "NON-" : ""
                    );

            if (!(GlobalRunningNovellClient32 && !GlobalNonBlockingClient32)) {
                pSocket->SetNonBlockingMode(fsm.m_dwSocketFlags & SF_NON_BLOCKING);
            }
        }
        *fsm.m_lplpSocket = pSocket;
    }

    if (bUnlockList) {
        m_Waiters.Release();
    }

quit:

    if (error != ERROR_IO_PENDING) {
        fsm.SetDone();
    }

    DPRINTF("%#x: %#x: %s: %d+%d/%d: get: %d, %#x, %d\n",
            GetCurrentThreadId(),
            Fsm,
            GetHostName(),
            m_ConnectionsAvailable,
            ElementsOnSerializedList(&m_KeepAliveList),
            m_ConnectionLimit,
            error,
            pSocket ? pSocket->GetSocket() : 0,
            m_Waiters.Count()
            );

    PERF_LEAVE(GetConnection);

    DEBUG_LEAVE(error);

    return error;
}


DWORD
CServerInfo::ReleaseConnection(
    IN ICSocket * lpSocket OPTIONAL
    )

/*++

Routine Description:

    Returns a keep-alive connection to the pool, or allows another requester to
    create a connection

Arguments:

    lpSocket    - pointer to ICSocket if we are returning a keep-alive connection

Return Value:

    DWORD
        Success - ERROR_SUCCESS

        Failure -

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 Dword,
                 "CServerInfo::ReleaseConnection",
                 "{%q [%d+%d/%d]} %#x [%#x]",
                 GetHostName(),
                 AvailableConnections(),
                 KeepAliveConnections(),
                 ConnectionLimit(),
                 lpSocket,
                 lpSocket ? lpSocket->GetSocket() : 0
                 ));

    PERF_ENTER(ReleaseConnection);

    DWORD error = ERROR_SUCCESS;
    BOOL bRelease = FALSE;

    CServerInfo *   pGlobalServerInfo;
    BOOL            bInGlobalServerPool = InGlobalServerInfoPool();


    if (!bInGlobalServerPool)
    {
        if (!m_Waiters.Acquire())
        {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto quit;
        }

        //
        // quite often (at least with catapult proxy based on IIS) the server may
        // drop the connection even though it indicated it would keep it open. This
        // typically happens on 304 (frequent) and 302 (less so) responses. If we
        // determine the server has dropped the connection then throw it away and
        // allow the app to create a new one
        //

        if (lpSocket != NULL) {

            lpSocket->ResetInGlobalKeepAlivePool();

            if (lpSocket->IsClosed() || lpSocket->IsReset()) {

                DEBUG_PRINT(SESSION,
                            INFO,
                            ("socket %#x already dead - throwing it out\n",
                            lpSocket->GetSocket()
                            ));

                DPRINTF("%#x: socket %#x: already reset\n",
                        GetCurrentThreadId(),
                        lpSocket->GetSocket()
                        );

    //dprintf("ReleaseConnection: destroying already closed socket %#x\n", lpSocket->GetSocket());
                BOOL bDestroyed = lpSocket->Dereference();
                UNREFERENCED_PARAMETER(bDestroyed); // avoid C4189 on free builds
                INET_ASSERT(bDestroyed);

                lpSocket = NULL;
            } else {

                //
                // if we are returning a keep-alive socket, put it in non-blocking
                // mode if not already. Typically, Internet Explorer uses non-blocking
                // sockets. In the infrequent cases where we want a blocking socket
                // - mainly when doing java downloads - we will convert the socket
                // to blocking mode when we get it from the pool
                //

                if (!lpSocket->IsNonBlocking()) {

                    DPRINTF("%#x: ***** WARNING: releasing BLOCKING k-a socket %#x\n",
                            GetCurrentThreadId(),
                            lpSocket->GetSocket()
                            );

                    lpSocket->SetNonBlockingMode(TRUE);
                }
            }
        }
    }


    if ((lpSocket != NULL)                          &&
        !bInGlobalServerPool                        &&
        ( (lpSocket->GetFlags() & SOCK_FLAGS) == 0    ||
          !(((ICSecureSocket *)lpSocket)->GetCertContextArray() &&
            ((ICSecureSocket *)lpSocket)->GetCertContextArray()->GetSelectedCertContext()) ) &&
        !lpSocket->IsAuthorized()                   &&
        UnlimitedConnections()                      &&
        (g_pGlobalServerInfoPool != NULL)           &&
        ((pGlobalServerInfo = g_pGlobalServerInfoPool->GetServerInfo(GetHostName()))!=NULL)
        )
    {
        INET_ASSERT(!lpSocket->IsAuthenticated());

        //
        // Release the socket connection into the global pool...
        //
        lpSocket->SetInGlobalKeepAlivePool();

        error = pGlobalServerInfo->ReleaseConnection(lpSocket);

        //
        // Push out this global serverinfo's expiry time to
        // current time + GlobalServerInfoTimeout.
        //
        pGlobalServerInfo->SetExpiryTime();

        ReleaseServerInfo(pGlobalServerInfo);
        pGlobalServerInfo = NULL;

        g_pGlobalServerInfoPool->ScavengeList();
    }
    else if (lpSocket != NULL) {

        DPRINTF("%#x: releasing K-A %#x (%d+%d/%d)\n",
                GetCurrentThreadId(),
                lpSocket ? lpSocket->GetSocket() : 0,
                AvailableConnections(),
                KeepAliveConnections(),
                ConnectionLimit()
                );

        INET_ASSERT(lpSocket->IsOpen());
        INET_ASSERT(!lpSocket->IsOnList());
        //INET_ASSERT(!lpSocket->IsReset());

        lpSocket->SetKeepAlive();

        DEBUG_PRINT(SESSION,
                    INFO,
                    ("releasing keep-alive socket %#x\n",
                    lpSocket->GetSocket()
                    ));

        lpSocket->SetExpiryTime(GlobalKeepAliveSocketTimeout);

        INET_ASSERT(!IsOnSerializedList(&m_KeepAliveList, lpSocket->List()));

        if (!InsertAtTailOfSerializedList(&m_KeepAliveList, lpSocket->List()))
        {
            DEBUG_PRINT(SESSION,
                        INFO,
                        ("not enough memory to release %#x to k-a pool\n",
                        lpSocket->GetSocket()
                        ));

            lpSocket->Dereference();

            if (!UnlimitedConnections())
            {
                ++m_ConnectionsAvailable;
            }
        }

        lpSocket = NULL;

        INET_ASSERT(UnlimitedConnections()
            ? TRUE
            : (KeepAliveConnections() <= ConnectionLimit()));

        bRelease = TRUE;
    } else {

        DPRINTF("%#x: releasing connection (%d+%d/%d)\n",
                GetCurrentThreadId(),
                AvailableConnections(),
                KeepAliveConnections(),
                ConnectionLimit()
                );

        if (!UnlimitedConnections()) {
            ++m_ConnectionsAvailable;
        }

        CHECK_CONNECTION_COUNT();

        bRelease = TRUE;
    }
    if (bRelease && !UnlimitedConnections()) {

        CHECK_CONNECTION_COUNT();

        CConnectionWaiter * pWaiter = (CConnectionWaiter *)m_Waiters.RemoveHead();

        if (pWaiter != NULL) {

            DEBUG_PRINT(SESSION,
                        INFO,
                        ("unblocking %s waiter %#x, pri=%d\n",
                        pWaiter->IsSync() ? "SYNC" : "ASYNC",
                        pWaiter->Id(),
                        pWaiter->GetPriority()
                        ));

            DPRINTF("%#x: Unblocking %s connection waiter %#x, pri=%d\n",
                    GetCurrentThreadId(),
                    pWaiter->IsSync() ? "Sync" : "Async",
                    pWaiter->Id(),
                    pWaiter->GetPriority()
                    );

            if (pWaiter->IsSync()) {
                pWaiter->Signal();
            } else {

                DWORD n = UnblockWorkItems(1, (DWORD_PTR)pWaiter, ERROR_SUCCESS);
                UNREFERENCED_PARAMETER(n);
                //INET_ASSERT(n == 1);
            }
            delete pWaiter;
        } else {

            DEBUG_PRINT(SESSION,
                        INFO,
                        ("no waiters\n"
                        ));

            DPRINTF("%#x: !!! NOT unblocking connection waiter\n",
                    GetCurrentThreadId()
                    );

        }
    } else {

        DPRINTF("%#x: !!! NOT releasing or unlimited?\n",
                GetCurrentThreadId()
                );

        DEBUG_PRINT(SESSION,
                    INFO,
                    ("bRelease = %B, UnlimitedConnections() = %B\n",
                    bRelease,
                    UnlimitedConnections()
                    ));

    }

    DEBUG_PRINT(SESSION,
                INFO,
                ("avail+k-a/limit = %d+%d/%d\n",
                AvailableConnections(),
                KeepAliveConnections(),
                ConnectionLimit()
                ));

    if (IsNewLimit()) {
        UpdateConnectionLimit();
    }

    if (!bInGlobalServerPool)
    {
        m_Waiters.Release();
    }

quit:
    PERF_LEAVE(ReleaseConnection);

    DEBUG_LEAVE(error);

    DPRINTF("%#x: %s: %d+%d/%d: rls %#x: %d, %d\n",
            GetCurrentThreadId(),
            GetHostName(),
            AvailableConnections(),
            KeepAliveConnections(),
            ConnectionLimit(),
            lpSocket ? lpSocket->GetSocket() : 0,
            error,
            m_Waiters.Count()
            );

    return error;
}



VOID
CServerInfo::RemoveWaiter(
    IN DWORD_PTR dwId
    )

/*++

Routine Description:

    Removes a CConnectionWaiter corresponding to the FSM

Arguments:

    dwId    - waiter id to match

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::RemoveWaiter",
                 "%#x",
                 dwId
                 ));

    if (!m_Waiters.Acquire())
        goto quit;

    CConnectionWaiter * pWaiter;
    BOOL found = FALSE;

    for (pWaiter = (CConnectionWaiter *)m_Waiters.Head();
         pWaiter != (CConnectionWaiter *)m_Waiters.Self();
         pWaiter = (CConnectionWaiter *)pWaiter->Next()) {

        if (pWaiter->Id() == dwId) {
            m_Waiters.Remove((CPriorityListEntry *)pWaiter);
            delete pWaiter;
            found = TRUE;
            break;
        }
    }
    m_Waiters.Release();

quit:
    //INET_ASSERT(found);

    DEBUG_LEAVE(0);
}

//
// private CServerInfo methods
//


ICSocket *
CServerInfo::FindKeepAliveConnection(
    IN DWORD dwSocketFlags,
    IN INTERNET_PORT nPort,
    IN LPSTR pszTunnelServer,
    IN DWORD dwSecureProtocols
    )

/*++

Routine Description:

    Find a keep-alive connection with the requested attributes and port number

Arguments:

    dwSocketFlags     - socket type flags (e.g. SF_SECURE)

    nPort             - port to server

    pszTunnelServer   - hostname of server through SSL tunnel, or
                        NULL if not checked.

    dwSecureProtocols - Enabled SSL protocols on keep-alive must match
                        the requested values.

Return Value:

    ICSocket *

--*/

{
    DPRINTF("%#x: *** looking for K-A connection\n", GetCurrentThreadId());

    DEBUG_ENTER((DBG_SESSION,
                 Pointer,
                 "CServerInfo::FindKeepAliveConnection",
                 "{%q} %#x, %d, %q, %#x",
                 GetHostName(),
                 dwSocketFlags,
                 nPort,
                 pszTunnelServer ? pszTunnelServer : "NULL",
                 dwSecureProtocols
                 ));

    ICSocket * pSocket = NULL;
    BOOL bFound = FALSE;

    //
    // don't check whether socket is non-blocking - we only really want to match
    // on secure/non-secure. Possible flags to check on are:
    //
    //  SF_ENCRYPT          - should be subsumed by SF_SECURE
    //  SF_DECRYPT          - should be subsumed by SF_SECURE
    //  SF_NON_BLOCKING     - this isn't criterion for match
    //  SF_CONNECTIONLESS   - not implemented?
    //  SF_AUTHORIZED       - must be set if authorized & in pool
    //  SF_SECURE           - opened for SSL/PCT if set
    //  SF_KEEP_ALIVE       - must be set
    //  SF_TUNNEL           - must be set if we're looking for a CONNECT tunnel to proxy
    //  SF_OVERLAPPED       - set if overlapped.

    dwSocketFlags &= ~SF_NON_BLOCKING;

    if (!LockSerializedList(&m_KeepAliveList))
        goto quit;

    PLIST_ENTRY pEntry;

    for (pEntry = HeadOfSerializedList(&m_KeepAliveList);
         pEntry != (PLIST_ENTRY)SlSelf(&m_KeepAliveList);
         pEntry = pEntry->Flink) {

        pSocket = ContainingICSocket(pEntry);

        INET_ASSERT(pSocket->IsKeepAlive());

        //
        // We make sure the socket we request is the correct socket,
        //  Match() is a bit confusing and needs a bit of explaining,
        //  Match IS NOT AN EXACT MATCH, it mearly checks to make sure 
        //  that the requesting flags (dwSocketFlags) are found in the 
        //  socket flags.  So this can lead to a secure socket being returned
        //  on a non-secure open request, now realistically this doesn't happen
        //  because of the port number.  But in the case of tunnelling this may be
        //  an issue, so we add an additional check to make sure that we only
        //  get a tunneled socket to a proxy if we specifically request one.
        //

        if (pSocket->Match(dwSocketFlags&~SF_OVERLAPPED)
        && (pSocket->GetPort() == nPort)
        &&  pSocket->MatchSecureProtocolSemantics(dwSocketFlags, pszTunnelServer, dwSecureProtocols)
        &&  pSocket->MatchOverlappedSemantics(dwSocketFlags)
        &&  RemoveFromSerializedList(&m_KeepAliveList, pSocket->List())) {

            INET_ASSERT(!IsOnSerializedList(&m_KeepAliveList, pSocket->List()));

            bFound = TRUE;

            DEBUG_PRINT(SESSION,
                        INFO,
                        ("returning keep-alive socket %#x\n",
                        pSocket->GetSocket()
                        ));

            DPRINTF("%#x: *** %s keep-alive connection %#x (%d/%d), wantf=%#x, gotf=%#x\n",
                    GetCurrentThreadId(),
                    GetHostName(),
                    pSocket->GetSocket(),
                    AvailableConnections(),
                    ConnectionLimit(),
                    dwSocketFlags,
                    pSocket->GetFlags()
                    );

            break;
        }
    }
    UnlockSerializedList(&m_KeepAliveList);
    if (!bFound) {
        pSocket = NULL;
    }

quit:
    DEBUG_LEAVE(pSocket);

    return pSocket;
}


BOOL
CServerInfo::KeepAliveWaiters(
    VOID
    )

/*++

Routine Description:

    Determine if any of the waiters on the list are for keep-alive connections

Arguments:

    None.

Return Value:

    BOOL

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 Bool,
                 "CServerInfo::KeepAliveWaiters",
                 NULL
                 ));

    BOOL found = FALSE;
    CConnectionWaiter * pWaiter;

    if (!m_Waiters.Acquire())
        goto quit;

    for (pWaiter = (CConnectionWaiter *)m_Waiters.Head();
         pWaiter != (CConnectionWaiter *)m_Waiters.Self();
         pWaiter = (CConnectionWaiter *)pWaiter->Next()) {

        if (pWaiter->IsKeepAlive()) {
            found = TRUE;
            break;
        }
    }
    m_Waiters.Release();

quit:
    DEBUG_LEAVE(found);

    return found;
}



VOID
CServerInfo::UpdateConnectionLimit(
    VOID
    )

/*++

Routine Description:

    Change connection limit to new limit

    Assumes: 1. Caller has acquired this object before calling this function

Arguments:

    None.

Return Value:

    None.

--*/

{
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::UpdateConnectionLimit",
                 "{%q: %d=>%d (%d+%d)}",
                 GetHostName(),
                 ConnectionLimit(),
                 GetNewLimit(),
                 AvailableConnections(),
                 KeepAliveConnections()
                 ));

    LONG difference = GetNewLimit() - ConnectionLimit();

    //
    // BUGBUG - only handling increases in limit for now
    //

    INET_ASSERT(difference > 0);

    if (difference > 0) {
        m_ConnectionsAvailable += difference;
    }
    m_ConnectionLimit = m_NewLimit;

    DEBUG_PRINT(SESSION,
                INFO,
                ("%q: new: %d+%d/%d\n",
                GetHostName(),
                AvailableConnections(),
                KeepAliveConnections(),
                ConnectionLimit()
                ));

    DEBUG_LEAVE(0);
}


VOID
CServerInfo::PurgeKeepAlives(
    IN DWORD dwForce
    )

/*++

Routine Description:

    Purges any timed-out keep-alive connections

Arguments:

    dwForce - force to apply when purging. Value can be:

                PKA_NO_FORCE    - only purge timed-out sockets or sockets in
                                  close-wait state (default)

                PKA_NOW         - purge all sockets

                PKA_AUTH_FAILED - purge sockets that have been marked as failing
                                  authentication

Return Value:

    None.

--*/

{
//dprintf("%#x PurgeKeepAlives(%d)\n", GetCurrentThreadId(), dwForce);
    DEBUG_ENTER((DBG_SESSION,
                 None,
                 "CServerInfo::PurgeKeepAlives",
                 "{%q [ref=%d, k-a=%d]} %s [%d]",
                 GetHostName(),
                 ReferenceCount(),
                 KeepAliveConnections(),
                 (dwForce == PKA_NO_FORCE) ? "NO_FORCE"
                 : (dwForce == PKA_NOW) ? "NOW"
                 : (dwForce == PKA_AUTH_FAILED) ? "AUTH_FAILED"
                 : "?",
                 dwForce
                 ));

    if (IsKeepAliveListInitialized()) {

        INET_ASSERT(ReferenceCount() >= 1);

        if (!m_Waiters.Acquire())
            goto quit;

        if (!LockSerializedList(&m_KeepAliveList))
            goto Cleanup;

        PLIST_ENTRY last = (PLIST_ENTRY)SlSelf(&m_KeepAliveList);
        DWORD ticks = GetTickCountWrap();

        for (PLIST_ENTRY pEntry = HeadOfSerializedList(&m_KeepAliveList);
            pEntry != (PLIST_ENTRY)SlSelf(&m_KeepAliveList);
            pEntry = last->Flink) {

            ICSocket * pSocket = ContainingICSocket(pEntry);
            BOOL bDelete;

            if (pSocket->IsReset()) {
//dprintf("%q: socket %#x/%d CLOSE-WAIT\n", GetHostName(), pSocket->GetSocket(), pSocket->GetSourcePort());
                bDelete = TRUE;
            } else if (dwForce == PKA_NO_FORCE) {
                bDelete = pSocket->HasExpired(ticks);
            } else if (dwForce == PKA_NOW) {
                bDelete = TRUE;
            } else if (dwForce == PKA_AUTH_FAILED) {
                bDelete = pSocket->IsAuthorized();
            } else {
                INET_ASSERT(FALSE); // invalid value for dwForce!
                bDelete = TRUE;
            }
            if (bDelete) {
//dprintf("%q: socket %#x/%d. Close-Wait=%B, Expired=%B, Now=%B, Auth=%B\n",
//        GetHostName(),
//        pSocket->GetSocket(),
//        pSocket->GetSourcePort(),
//        pSocket->IsReset(),
//        pSocket->HasExpired(ticks),
//        (dwForce == PKA_NOW),
//        (dwForce == PKA_AUTH_FAILED) && pSocket->IsAuthorized()
//        );

                DEBUG_PRINT(SESSION,
                            INFO,
                            ("purging keep-alive socket %#x/%d: Close-Wait=%B, Expired=%B, Now=%B, Auth=%B\n",
                            pSocket->GetSocket(),
                            pSocket->GetSourcePort(),
                            pSocket->IsReset(),
                            pSocket->HasExpired(ticks),
                            (dwForce == PKA_NOW),
                            (dwForce == PKA_AUTH_FAILED) && pSocket->IsAuthorized()
                            ));

                if (RemoveFromSerializedList(&m_KeepAliveList, pEntry))
                {
                    BOOL bDestroyed;

                    bDestroyed = pSocket->Dereference();

                    INET_ASSERT(bDestroyed);

                    if (!UnlimitedConnections()) {
                        ++m_ConnectionsAvailable;

                        INET_ASSERT(m_ConnectionsAvailable <= m_ConnectionLimit);

                    }
                }
                else
                {
                    DEBUG_PRINT(SESSION,
                                INFO,
                                ("k-a socket %#x couldn't be removed from the list\n",
                                pSocket->GetSocket()
                                ));
                }
            } else {

                DEBUG_PRINT(SESSION,
                            INFO,
                            ("socket %#x/%d expires in %d mSec\n",
                            pSocket->GetSocket(),
                            pSocket->GetSourcePort(),
                            pSocket->GetExpiryTime() - ticks
                            ));

                last = pEntry;
            }
        }

        UnlockSerializedList(&m_KeepAliveList);
Cleanup:
        m_Waiters.Release();
    }

quit:
    DEBUG_LEAVE(0);
}

//
// friend functions
//


CServerInfo *
ContainingServerInfo(
    IN LPVOID lpAddress
    )

/*++

Routine Description:

    Returns address of CServerInfo given address of m_List

Arguments:

    lpAddress   - address of m_List

Return Value:

    CServerInfo *

--*/

{
    return CONTAINING_RECORD(lpAddress, CServerInfo, m_List);
}



CGlobalServerInfoPool *  g_pGlobalServerInfoPool;


BOOL
CGlobalServerInfoPool::Initialize()
{
    SetNextScavengeTime();
    return InitializeSerializedList(&_GlobalServerInfoList);
}

void
CGlobalServerInfoPool::Terminate()
{
    if (LockSerializedList(&_GlobalServerInfoList))
    {
        PLIST_ENTRY pEntry = HeadOfSerializedList(&_GlobalServerInfoList);
        PLIST_ENTRY pPrevious = (PLIST_ENTRY)SlSelf(&_GlobalServerInfoList);

        for (;;)
        {
            if (pEntry == (PLIST_ENTRY)SlSelf(&_GlobalServerInfoList))
            {
                break;
            }

            CServerInfo * pServerInfo;

            pServerInfo = ContainingServerInfo(pEntry);

            BOOL deleted = pServerInfo->Dereference();

            INET_ASSERT(deleted);

            if (!deleted)
            {
                pPrevious = pEntry;
            }
            pEntry = pPrevious->Flink;
        }

        UnlockSerializedList(&_GlobalServerInfoList);
    }

    TerminateSerializedList(&_GlobalServerInfoList);
}


CServerInfo *
CGlobalServerInfoPool::FindServerInfo(LPSTR lpszHostName)
{
    ICSTRING        hostName(lpszHostName);
    CServerInfo *   pServerInfo = NULL;

    if (hostName.HaveString())
    {
        hostName.MakeLowerCase();

        lpszHostName = hostName.StringAddress();

        DWORD hashHostName = CalculateHashValue(lpszHostName);

        if (LockSerializedList(&_GlobalServerInfoList))
        {
            BOOL    found = FALSE;

            for (pServerInfo = (CServerInfo *)HeadOfSerializedList(&_GlobalServerInfoList);
                pServerInfo != (CServerInfo *)SlSelf(&_GlobalServerInfoList);
                pServerInfo = pServerInfo->Next())
            {
                if (pServerInfo->Match(hashHostName, lpszHostName))
                {
                    found = TRUE;
                    pServerInfo->Reference();
                    break;
                }
            }

            if (!found)
            {
                pServerInfo = NULL;
            }

            UnlockSerializedList(&_GlobalServerInfoList);
        }
    }

    return pServerInfo;
}


CServerInfo *
CGlobalServerInfoPool::GetServerInfo(LPSTR lpszHostName)
{
    CServerInfo *   pServerInfo = NULL;
    
    if (LockSerializedList(&_GlobalServerInfoList))
    {
        pServerInfo = FindServerInfo(lpszHostName);

        if (!pServerInfo)
        {
            DWORD   dwError = ERROR_SUCCESS;

            pServerInfo = New CServerInfo(&_GlobalServerInfoList,
                                lpszHostName,
                                &dwError,
                                INTERNET_SERVICE_HTTP,
                                WINHTTP_CONNS_PER_SERVER_UNLIMITED);

            if (pServerInfo)
            {
                if (dwError == ERROR_SUCCESS)
                {
                    pServerInfo->SetInGlobalServerInfoPool();
                    pServerInfo->Reference();
                }
                else
                {
                    BOOL bDeleted = pServerInfo->Dereference();
                    UNREFERENCED_PARAMETER(bDeleted);
                    INET_ASSERT(bDeleted);
                    pServerInfo = NULL;
                }
            }
        }

        UnlockSerializedList(&_GlobalServerInfoList);
    }

    return pServerInfo;
}

#define MAX_SCAVENGE_TIME       150                 // 150 milliseconds
#define SCAVENGE_DELTA         (2 * 60 * 1000)      // 2 minutes


void CGlobalServerInfoPool::ScavengeList()
{
    if (LockSerializedList(&_GlobalServerInfoList))
    {
        DWORD   StartTime = GetTickCount();
        DWORD   CurrentTime;
        DWORD   EndTime = StartTime + MAX_SCAVENGE_TIME; 
        BOOL    bWrap = (EndTime < StartTime);

        CurrentTime = StartTime;

        if (TimeToScavenge(CurrentTime))
        {
            PLIST_ENTRY pEntry = HeadOfSerializedList(&_GlobalServerInfoList);
            PLIST_ENTRY pPrevious = (PLIST_ENTRY)SlSelf(&_GlobalServerInfoList);

            for (;;)
            {
                if (pEntry == (PLIST_ENTRY)SlSelf(&_GlobalServerInfoList))
                {
                    break;
                }

                CServerInfo *   pServerInfo = ContainingServerInfo(pEntry);
                BOOL            deleted     = FALSE;

                if (pServerInfo->Expired())
                {
                    // Delete ServerInfo if ref count is 1
                    deleted = pServerInfo->Dereference();
                }
                else
                {
                    pServerInfo->PurgeKeepAlives(PKA_NO_FORCE);
                }

                if (!deleted)
                {
                    pPrevious = pEntry;
                }

                pEntry = pPrevious->Flink;

                CurrentTime = GetTickCount();

                if (!bWrap)
                {
                    if ((CurrentTime >= EndTime) || (CurrentTime < StartTime))
                        break;
                }
                else
                {
                    if ((CurrentTime >= EndTime) && (CurrentTime < StartTime))
                        break;
                }
                       
            }

            SetNextScavengeTime();
        }

        UnlockSerializedList(&_GlobalServerInfoList);
    }
}


BOOL CGlobalServerInfoPool::TimeToScavenge(DWORD CurrentTime) const
{
    if (!_bWrap)
    {
        if ((CurrentTime >= _NextScavengeTime) || (CurrentTime < _PreviousScavengeTime))
            return TRUE;
    }
    else
    {
        if ((CurrentTime >= _NextScavengeTime) && (CurrentTime < _PreviousScavengeTime))
            return TRUE;
    }
    
    return FALSE;
}


void CGlobalServerInfoPool::SetNextScavengeTime()
{
    _PreviousScavengeTime = GetTickCount();
    _NextScavengeTime    = _PreviousScavengeTime + SCAVENGE_DELTA;
    _bWrap = (_NextScavengeTime < _PreviousScavengeTime);
}
