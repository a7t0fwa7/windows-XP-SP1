/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:

    qsnap.hxx

Abstract:

    Declaration of CVssQueuedSnapshot


    Adi Oltean  [aoltean]  10/18/1999

TBD:
	
	Add comments.

Revision History:

    Name        Date        Comments
    aoltean     10/18/1999  Created

--*/

#ifndef __CVSSQUEUEDSNAPSHOT_HXX__
#define __CVSSQUEUEDSNAPSHOT_HXX__

#if _MSC_VER > 1000
#pragma once
#endif

////////////////////////////////////////////////////////////////////////
//  Standard foo for file name aliasing.  This code block must be after
//  all includes of VSS header files.
//
#ifdef VSS_FILE_ALIAS
#undef VSS_FILE_ALIAS
#endif
#define VSS_FILE_ALIAS "SPRQSNPH"
//
////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Constants
//


// The GUID that corresponds to the format used to store the
// Backup Snapshot Application Info in Server SKU
// {BAE53126-BC65-41d6-86CC-3D56A5CEE693}
const GUID VOLSNAP_APPINFO_GUID_BACKUP_SERVER_SKU = 
{ 0xbae53126, 0xbc65, 0x41d6, { 0x86, 0xcc, 0x3d, 0x56, 0xa5, 0xce, 0xe6, 0x93 } };


// The GUID that corresponds to the format used to store the
// Hidden (Inaccessible) Snapshot Application Info
// {F12142B4-9A4B-49af-A851-700C42FDC2BE}
static const GUID VOLSNAP_APPINFO_GUID_HIDDEN = 
{ 0xf12142b4, 0x9a4b, 0x49af, { 0xa8, 0x51, 0x70, 0xc, 0x42, 0xfd, 0xc2, 0xbe } };



// The maximum number of attempts in order to find 
// a volume name for a snapshot.
// Maximum wait = 10 minutes (500*1200)
const nNumberOfMountMgrRetries = 1200;		

// The number of milliseconds between retries to wait until 
// the Mount Manager does its work.
const nMillisecondsBetweenMountMgrRetries = 500;

// a volume name for a snapshot.
// Maximum wait = 1 minute (500*120)
const nNumberOfPnPRetries = 120;		

// The number of milliseconds between retries to wait until 
// the Mount Manager does its work.
const nMillisecondsBetweenPnPRetries = 500;

//	The default initial allocation for a Snapsot diff area.
//  This value is hardcoded and the client should not rely on that except for very dumb clients
//  Normally a snapshot requestor will:
//		1) Get the average write I/O traffic on all involved volumes
//		2) Get the average free space on all volumess where a diff area can reside
//		3) Allocate the appropriate diff areas.
const nDefaultInitialSnapshotAllocation = 100 * 1024 * 1024; // By default 100 Mb.



// 
//  Prefix constants for the global root.
//
const WCHAR wszGlobalRootPrefix[] = L"\\\\?\\GLOBALROOT";
const size_t nGlobalRootPrefixLength = sizeof(wszGlobalRootPrefix)/sizeof(WCHAR) - 1;


// Internal flags
const LONG VSS_INTF_HIDDEN = 0x00000001;


/////////////////////////////////////////////////////////////////////////////
// Forward declarations
//


class CVssSnapIterator;
class CVssGenericSnapProperties;


/////////////////////////////////////////////////////////////////////////////
// CVssQueuedSnapshot
//

/*++

Description: 

	This C++ class is used to represent a C++ wrapper around a snapshot object.

	It is used in three places:
	1) as a object in the list of queued snapshots for commit (at creation time)
	2) as a object used to represent snapshot data in a IVssCnapshot COM object.
	3) both

	It is pretty misnamed... sorry.

	Implements:
	a) A set of routines for sending IOCTLS
	b) A set of routies for managing snapshot state.
	c) A set of routines for life-management, useful to mix 1) with 2) above.
	d) Routines to "queue" the snapshot in a list of snapshots.
	e) Data members for keeping snapshot data

--*/

class CVssQueuedSnapshot: public IUnknown
{
// Typedefs and enums
public:

// Constructors and destructors
private:
    CVssQueuedSnapshot(const CVssQueuedSnapshot&);

public:
    CVssQueuedSnapshot();	// Declared but not defined only to allow CComPtr<CVssQueuedSnapshot>

    CVssQueuedSnapshot(
		IN  VSS_OBJECT_PROP_Ptr& ptrSnap,	// Ownership passed to the Constructor
        IN  VSS_ID ProviderInstanceId,
        IN  LONG lContext
	);

	~CVssQueuedSnapshot();

// Operations
public:

	// Open IOCTLS methods

	void OpenVolumeChannel() throw(HRESULT);

	void OpenSnapshotChannel() throw(HRESULT);

	CVssIOCTLChannel& GetSnapshotChannel() {
	    BS_ASSERT(m_snapIChannel.IsOpen());
	    return m_snapIChannel;
	}

	// Create snapshot methods

	void PrepareForSnapshotIoctl() throw(HRESULT);

	void CommitSnapshotIoctl() throw(HRESULT);

	void EndCommitSnapshotIoctl(
    	IN	PVSS_SNAPSHOT_PROP pProp
		) throw(HRESULT);

	HRESULT AbortPreparedSnapshotIoctl();

	// Save methods

	static void SaveStructure(
        IN  CVssIOCTLChannel& channel,
    	IN	PVSS_SNAPSHOT_PROP pProp,
       	IN  LONG lContext,
       	IN  bool bHidden
		) throw(HRESULT);

	// Load methods 

	void LoadSnapshotProperties() throw(HRESULT);

	static void LoadStructure(
        IN  CVssIOCTLChannel& channel,
    	OUT PVSS_SNAPSHOT_PROP pProp,
       	OUT LONG * plContext,
       	OUT bool * pbHidden,
    	IN  bool bIDsAlreadyLoaded  = false
		) throw(HRESULT);

	static void LoadOriginalVolumeNameIoctl(
        IN  CVssIOCTLChannel & snapshotIChannel,
        OUT LPWSTR * wszOriginalVolumeName
	    ) throw(HRESULT);

    static void LoadTimestampIoctl(
        IN  CVssIOCTLChannel &  snapshotIChannel,
        OUT VSS_TIMESTAMP    *  pTimestamp
        ) throw(HRESULT);
    
	// Find methods

	static bool FindPersistedSnapshotByID(
        IN  VSS_ID SnapshotID,
    	IN  LONG lContext,
        OUT LPWSTR * pwszSnapshotDeviceObject
    	) throw(HRESULT);

	static void EnumerateSnapshots(
	    IN  bool bSearchBySnapshotID,
	    IN  VSS_ID SnapshotID,
		IN  LONG lContext,
		IN OUT	VSS_OBJECT_PROP_Array* pArray
		) throw(HRESULT);

    static bool EnumerateSnapshotsOnVolume(
		IN  VSS_PWSZ wszVolumeName,
	    IN  bool bSearchBySnapshotID,
	    IN  VSS_ID SnapshotID,
		IN  LONG lContext,
		IN OUT	VSS_OBJECT_PROP_Array* pArray,
	    IN  bool bThrowOnError = false  // By default do not throw on error
	    ) throw(HRESULT);
    
	// State related
	
	PVSS_SNAPSHOT_PROP GetSnapshotProperties();

	bool IsDuringCreation() throw(HRESULT);

	void SetInitialAllocation(
		IN	LONGLONG llInitialAllocation
		);

	LONGLONG GetInitialAllocation();

	VSS_SNAPSHOT_STATE GetStatus();

	VSS_ID GetSnapshotID();
	
	VSS_ID GetSnapshotSetID();
	
	void MarkAsProcessingPrepare();

	void MarkAsPrepared();

	void MarkAsProcessingPreCommit();

	void MarkAsPreCommitted();

	void MarkAsProcessingCommit();

	void MarkAsCommitted();

	void MarkAsProcessingPostCommit();

	void MarkAsCreated();

	void MarkAsAborted();

    void SetPostcommitInfo(
        IN  LONG lSnapshotsCount
        );
    
	void ResetAsPreparing();

	void ResetSnapshotProperties() throw(HRESULT);

	VSS_ID GetProviderInstanceId();

	// Queuing

	void AttachToGlobalList() throw(HRESULT);


	void DetachFromGlobalList();

	// Context

	LONG GetContextInternal() const { return m_lContext; };

// Life-management
public:

	STDMETHOD(QueryInterface)(
		IN	REFIID, 
		OUT	void**
		) 
	{ 
		BS_ASSERT(false); 
		return E_NOTIMPL; 
	};

	STDMETHOD_(ULONG, AddRef)()
	{ 
		return InterlockedIncrement(&m_lRefCount); 
	};

	STDMETHOD_(ULONG, Release)()
	{ 
		if (InterlockedDecrement(&m_lRefCount) != 0)
			return m_lRefCount;
		delete this;
		return 0;
	};

// Data Members
private:

	// Global list of queued snapshots
    static CVssDLList<CVssQueuedSnapshot*>	 m_list;
	VSS_COOKIE					m_cookie;

	// The standard snapshot structure
	VSS_OBJECT_PROP_Ptr			m_ptrSnap;			// Keep some snapshot attributes 

	// Differential Software provider dependent information
	LONGLONG					m_llInitialAllocation;

	// The IOCTL channels

	// For the volume object
    CVssIOCTLChannel			m_volumeIChannel;	

	// For the snapshot object
    CVssIOCTLChannel			m_snapIChannel;		

	// Life-management 
	LONG						m_lRefCount;

    // The provider instance ID
    VSS_ID                      m_ProviderInstanceId;

    // The snapshot context (comes from BeginPrepareSnapshot or on snapshot load)
    LONG                        m_lContext;

	friend class CVssSnapIterator;
};


/////////////////////////////////////////////////////////////////////////////
// CVssSnapIterator
//

/*++

Description:

	An iterator used to enumerate the queued snapshots that corresponds to a certain snapshot set.

--*/

class CVssSnapIterator: public CVssDLListIterator<CVssQueuedSnapshot*>
{
// Constructors/destructors
private:
	CVssSnapIterator(const CVssSnapIterator&);

public:
	CVssSnapIterator();

// Operations
public:
	CVssQueuedSnapshot* GetNextBySnapshotSet(
		IN		VSS_ID SSID
		);

	CVssQueuedSnapshot* GetNextBySnapshot(
		IN		VSS_ID SSID
		);

	CVssQueuedSnapshot* GetNextByProviderInstance(
		IN		VSS_ID ProviderInstanceID
		);

	CVssQueuedSnapshot* GetNext();
};


#endif // __CVSSQUEUEDSNAPSHOT_HXX__
