/*++


   Copyright    (c) 1998-2001    Microsoft Corporation

   Module  Name :
       LKRhash.h

   Abstract:
       Declares LKRhash: a fast, scalable, cache- and MP-friendly hash table

       Deprecated: Use the version in %sdxroot%\inetsrv\iis\iisrearc\core\inc
       instead.

   Author:
       Paul (Per-Ake) Larson, palarson@microsoft.com, July 1997
       Murali R. Krishnan    (MuraliK)
       George V. Reilly      (GeorgeRe)     06-Jan-1998

   Environment:
       Win32 - User Mode

   Project:
       Internet Information Server RunTime Library

   Revision History:
       10/01/1998 - Change name from LKhash to LKRhash

--*/


#define LKR_COMPACT_DELETE 1
#define LKR_NEWCODE 1
// #define LKR_FIND_FIRST 1
#define LKR_SUBTABLE 1
#define LKR_MASK 1


#ifndef __LKRHASH_H__
#define __LKRHASH_H__

//=====================================================================
//  The class CLKRLinearHashTable defined in this file provides dynamic hash
//  tables, i.e. tables that grow and shrink dynamically with
//  the number of records in the table.
//  The basic method used is linear hashing, as explained in:
//
//    P.-A. Larson, Dynamic Hash Tables, Comm. of the ACM, 31, 4 (1988)
//
//  This version has the following characteristics:
//  - It is thread-safe and uses spin locks for synchronization.
//  - It was designed to support very high rates of concurrent
//    operations (inserts/deletes/lookups).  It achieves this by
//    (a) partitioning a CLKRHashTable into a collection of
//        CLKRLinearHashTables to reduce contention on the global table lock.
//    (b) minimizing the hold time on a table lock, preferring to lock
//        down a bucket chain instead.
//  - The design is L1 cache-conscious.  See CNodeClump.
//  - It is designed for sets varying in size from a dozen
//    elements to several million.
//
//  Main classes:
//    CLKRLinearHashTable: thread-safe linear hash table
//    CLKRHashTable:       collection of CLKRLinearHashTables
//    CTypedHashTable:    typesafe wrapper for CLKRHashTable
//
//
//  Paul Larson, palarson@microsoft.com, July 1997
//   Original implementation with input from Murali R. Krishnan,
//   muralik@microsoft.com.
//
//  George V. Reilly, georgere@microsoft.com, Dec 1997-Jan 1998
//   Massive cleanup and rewrite.  Added templates.
//=====================================================================


// 1) Linear Hashing
// ------------------
//
// Linear hash tables grow and shrink dynamically with the number of
// records in the table.  The growth or shrinkage is smooth: logically,
// one bucket at a time but physically in larger increments
// (64 buckets).  An insertion (deletion) may cause an expansion
// (contraction) of the table.  This causes relocation of a small number
// of records (at most one bucket worth).  All operations (insert,
// delete, lookup) take constant expected time, regardless of the
// current size or the growth of the table.
//
// 2) LKR extensions to Linear hash table
// --------------------------------------
//
// Larson-Krishnan-Reilly extensions to Linear hash tables for multiprocessor
// scalability and improved cache performance.
//
// Traditional implementations of linear hash tables use one global lock
// to prevent interference between concurrent operations
// (insert/delete/lookup) on the table.  The single lock easily becomes
// the bottleneck in SMP scenarios when multiple threads are used.
//
// Traditionally, a (hash) bucket is implemented as a chain of
// single-item nodes.  Every operation results in chasing down a chain
// looking for an item. However, pointer chasing is very slow on modern
// systems because almost every jump results in a cache miss. L2 (or L3)
// cache misses are very expensive in missed CPU cycles and the cost is
// increasing (going to 100s of cycles in the future).
//
// LKR extensions offer
//    1) Partitioning (by hashing) of records among multiple subtables.
//       Each subtable has locks but there is no global lock.  Each
//       subtable receives a much lower rate of operations, resulting in
//       fewer conflicts.
//
//    2) Improved cache locality by grouping keys and their hash values
//       into contigous chunks that fit exactly into one (or a few)
//       cache lines.
//
// Specifically the implementation that exists here achieves this using
// the following techniques.
//
// Class CLKRHashTable is the top-level data structure that dynamically
// creates m_cSubTables linear hash tables. The CLKRLinearHashTables act as
// the subtables to which items and accesses are fanned out. A good
// hash function multiplexes requests uniformly to various subtables,
// thus minimizing traffic to any single subtable. The implemenation
// uses a home-grown version of bounded spinlocks, that is, a thread
// does not spin on a lock indefinitely, instead yielding after a
// predetermined number of loops.
//
// Each CLKRLinearHashTable consists of a CDirEntry pointing to segments
// each holding m_dwSegSize CBuckets. Each CBucket in turn consists of a
// chain of CNodeClumps.  Each CNodeClump contains a group of
// NODES_PER_CLUMP hash values (aka hash keys or signatures) and
// pointers to the associated data items.  Keeping the signatures
// together increases the cache locality in scans for lookup.
//
// Traditionally, people store a link-list element right inside the
// object that is hashed and use this link-list for the chaining of data
// blocks.  However, keeping just the pointers to the data object and
// not chaining through them limits the need for bringing in the data
// object to the cache.  We need to access the data object only if the
// hash values match. This limits the cache-thrashing behaviour
// exhibited by conventional implementations.  It has the additional
// benefit that the objects themselves do not need to be modified
// in order to be collected in the hash table (i.e., it's non-invasive).


//--------------------------------------------------------------------
// TODO
// * Debugging support for iisprobe and inetdbg?
// * Use auto_ptrs.
// * Provide ReplaceRecord and DeleteRecord methods on iterators.
// * Sloppy iterators
// * Provide implementations of the STL collection classes, map, set,
//   multimap, and multiset.
// * Make exception safe.
//--------------------------------------------------------------------


#include <irtldbg.h>
#include <lstentry.h>
#include <hashfn.h>
#include <limits.h>



#ifdef __LKRHASH_NAMESPACE__
namespace LKRHash {
#endif // __LKRHASH_NAMESPACE__

enum LK_TABLESIZE {
    LK_SMALL_TABLESIZE=  1,     // < 200 elements
    LK_MEDIUM_TABLESIZE= 2,     // 200...10,000 elements
    LK_LARGE_TABLESIZE=  3,     // 10,000+ elements
};

// Default values for the hashtable constructors
enum {
    LK_DFLT_MAXLOAD=     4, // Default upperbound on average chain length.
    LK_DFLT_INITSIZE=LK_MEDIUM_TABLESIZE, // Default initial size of hash table
    LK_DFLT_NUM_SUBTBLS= 0, // Use a heuristic to choose #subtables
};

// build fix hack
enum {
    DFLT_LK_MAXLOAD=     LK_DFLT_MAXLOAD,
    DFLT_LK_INITSIZE=    LK_DFLT_INITSIZE,
    DFLT_LK_NUM_SUBTBLS= LK_DFLT_NUM_SUBTBLS,
};

//--------------------------------------------------------------------
// forward declarations

class IRTL_DLLEXP CLKRLinearHashTable;

class IRTL_DLLEXP CLKRHashTable;

template <class _Der, class _Rcd, class _Ky, class _HT, class _Iter>
class CTypedHashTable;


//--------------------------------------------------------------------
// Possible return codes from public member functions of
// CLKRLinearHashTable, CLKRHashTable, and CTypedHashTable

enum LK_RETCODE {
    // severe errors < 0
    LK_UNUSABLE = -99,  // Table corrupted: all bets are off
    LK_ALLOC_FAIL,      // ran out of memory
    LK_BAD_ITERATOR,    // invalid iterator; e.g., points to another table
    LK_BAD_RECORD,      // invalid record; e.g., NULL for InsertRecord
    LK_BAD_PARAMETERS,  // invalid parameters; e.g., NULL fnptrs to ctor
    LK_NOT_INITIALIZED, // LKRHashTableInit was not called

    LK_SUCCESS = 0,     // everything's okay
    LK_KEY_EXISTS,      // key already present for InsertRecord(no-overwrite)
    LK_NO_SUCH_KEY,     // key not found
    LK_NO_MORE_ELEMENTS,// iterator exhausted
};

#define LK_SUCCEEDED(lkrc)  ((lkrc) >= LK_SUCCESS)


//--------------------------------------------------------------------
// Return codes from PFnRecordPred.

enum LK_PREDICATE {
    LKP_ABORT = 1,           // Stop walking the table immediately
    LKP_NO_ACTION = 2,       // No action, just keep walking
    LKP_PERFORM = 3,         // Perform action and continue walking
    LKP_PERFORM_STOP = 4,    // Perform action, then stop
    LKP_DELETE = 5,          // Delete record and keep walking
    LKP_DELETE_STOP = 6,     // Delete record, then stop
};


//--------------------------------------------------------------------
// Return codes from PFnRecordAction.

enum LK_ACTION {
    LKA_ABORT = 1,          // Stop walking the table immediately
    LKA_FAILED = 2,         // Action failed; continue walking the table
    LKA_SUCCEEDED = 3,      // Action succeeded; continue walking the table
};


//--------------------------------------------------------------------
// Parameter to Apply and ApplyIf.

enum LK_LOCKTYPE {
    LKL_READLOCK = 1,       // Lock the table for reading (for constness)
    LKL_WRITELOCK = 2,      // Lock the table for writing
};



//--------------------------------------------------------------------
// Global table lock code.  This is only used to measure how much a
// slowdown having a global lock on the CLKRHashTable causes.  It is *never*
// used in production code.


// #define LKRHASH_GLOBAL_LOCK CCritSec

#ifdef LKRHASH_GLOBAL_LOCK

# define LKRHASH_GLOBAL_LOCK_DECLARATIONS()         \
    typedef LKRHASH_GLOBAL_LOCK GlobalLock;  \
    mutable GlobalLock m_lkGlobal;

# define LKRHASH_GLOBAL_READ_LOCK()     m_lkGlobal.ReadLock()
# define LKRHASH_GLOBAL_WRITE_LOCK()    m_lkGlobal.WriteLock()
# define LKRHASH_GLOBAL_READ_UNLOCK()   m_lkGlobal.ReadUnlock()
# define LKRHASH_GLOBAL_WRITE_UNLOCK()  m_lkGlobal.WriteUnlock()

#else // !LKRHASH_GLOBAL_LOCK

# define LKRHASH_GLOBAL_LOCK_DECLARATIONS()

// These ones will be optimized away by the compiler
# define LKRHASH_GLOBAL_READ_LOCK()     ((void)0)
# define LKRHASH_GLOBAL_WRITE_LOCK()    ((void)0)
# define LKRHASH_GLOBAL_READ_UNLOCK()   ((void)0)
# define LKRHASH_GLOBAL_WRITE_UNLOCK()  ((void)0)

#endif // !LKRHASH_GLOBAL_LOCK



//--------------------------------------------------------------------
// Statistical information returned by GetStatistics
//--------------------------------------------------------------------

#ifdef LOCK_INSTRUMENTATION

class IRTL_DLLEXP CAveragedLockStats : public CLockStatistics
{
public:
    int m_nItems;

    CAveragedLockStats()
        : m_nItems(1)
    {}
};

#endif // LOCK_INSTRUMENTATION



class IRTL_DLLEXP CLKRHashTableStats
{
public:
    int      RecordCount;           // number of records in the table
    int      TableSize;             // table size in number of slots
    int      DirectorySize;         // number of entries in directory
    int      LongestChain;          // longest hash chain in the table
    int      EmptySlots;            // number of unused hash slots
    double   SplitFactor;           // fraction of buckets split
    double   AvgSearchLength;       // average length of a successful search
    double   ExpSearchLength;       // theoretically expected length
    double   AvgUSearchLength;      // average length of an unsuccessful search
    double   ExpUSearchLength;      // theoretically expected length
    int      NodeClumpSize;         // number of slots in a node clump
    int      CBucketSize;           // sizeof(CBucket)

#ifdef LOCK_INSTRUMENTATION
    CAveragedLockStats      m_alsTable;  // stats for table lock
    CAveragedLockStats      m_alsBucketsAvg; // avg of stats for bucket locks
    CGlobalLockStatistics   m_gls;      // global statistics for all locks
#endif // LOCK_INSTRUMENTATION

    enum {
        MAX_BUCKETS = 40,
    };

    // histogram of bucket lengths
    LONG    m_aBucketLenHistogram[MAX_BUCKETS];

    CLKRHashTableStats()
        : RecordCount(0),
          TableSize(0),
          DirectorySize(0),
          LongestChain(0),
          EmptySlots(0),
          SplitFactor(0.0),
          AvgSearchLength(0.0),
          ExpSearchLength(0.0),
          AvgUSearchLength(0.0),
          ExpUSearchLength(0.0),
          NodeClumpSize(1),
          CBucketSize(0)
    {
        for (int i = MAX_BUCKETS;  --i >= 0;  )
            m_aBucketLenHistogram[i] = 0;
    }

    static const LONG*
    BucketSizes()
    {
        static const LONG  s_aBucketSizes[MAX_BUCKETS] = {
                   1,    2,    3,    4,    5,    6,    7,      8,        9,
            10,   11,   12,   13,   14,   15,   16,   17,     18,       19,
            20,   21,   22,   23,   24,   25,   30,   40,     50,       60,
            70,   80,   90,  100,  200,  500, 1000,10000, 100000, LONG_MAX,
        };

        return s_aBucketSizes;
    }

    static LONG
    BucketSize(
        LONG nBucketIndex)
    {
        IRTLASSERT(0 <= nBucketIndex  &&  nBucketIndex < MAX_BUCKETS);
        return BucketSizes()[nBucketIndex];
    }

    static LONG
    BucketIndex(
        LONG nBucketLength)
    {
        const LONG* palBucketSizes = BucketSizes();
        LONG i = 0;
        while (palBucketSizes[i] < nBucketLength)
            ++i;
        if (i == MAX_BUCKETS  ||  palBucketSizes[i] > nBucketLength)
            --i;
        IRTLASSERT(0 <= i  &&  i < MAX_BUCKETS);
        return i;
    }
};



//--------------------------------------------------------------------
// CLKRLinearHashTable deals with void* records.  These typedefs
// provide prototypes for functions that manipulate instances of
// those records.  CTypedHashTable and CStringTestHashTable (below) show a
// way to encapsulate these in typesafe wrappers.
//--------------------------------------------------------------------

// Given a record, return its key.  Assumes that the key is embedded in
// the record, or at least somehow derivable from the record.  For
// completely unrelated keys & values, a wrapper class should use
// something like STL's pair<key,value> template to aggregate them
// into a record.
typedef const DWORD_PTR (WINAPI *PFnExtractKey)  (const void* pvRecord);

// Given a key, return its hash signature.  The hashing functions in
// hashfn.h (or something that builds upon them) are suggested.
typedef DWORD       (WINAPI *PFnCalcKeyHash) (const DWORD_PTR pnKey);

// Compare two keys for equality; e.g., _stricmp, memcmp, operator==
typedef bool        (WINAPI *PFnEqualKeys)   (const DWORD_PTR pnKey1,
                                              const DWORD_PTR pnKey2);

// Increment the reference count of a record before returning it from
// FindKey.  It's necessary to do it in FindKey itself while the bucket
// is still locked, rather than one of the wrappers, to avoid race
// conditions.  Similarly, the reference count is incremented in
// InsertRecord and decremented in DeleteKey.  Finally, if an old record
// is overwritten in InsertRecord, its reference count is decremented.
//
// It's up to you to decrement the reference count when you're finished
// with it after retrieving it via FindKey and to determine the
// semantics of what this means.  The hashtable itself has no notion of
// reference counts; this is merely to help with the lifetime management
// of the record objects.
typedef void        (WINAPI *PFnAddRefRecord)(const void* pvRecord, int nIncr);

// ApplyIf() and DeleteIf(): Does the record match the predicate?
typedef LK_PREDICATE (WINAPI *PFnRecordPred) (const void* pvRecord,
                                              void* pvState);

// Apply() et al: Perform action on record.
typedef LK_ACTION   (WINAPI *PFnRecordAction)(const void* pvRecord,
                                              void* pvState);



//--------------------------------------------------------------------
// Custom memory allocators
//--------------------------------------------------------------------


#define LKRHASH_ACACHE 1
// #define LKRHASH_MANODEL 1
// #define LKRHASH_MADEL 1

// #define LKRHASH_MEM_DEFAULT_ALIGN 32

#ifndef LKRHASH_MEM_DEFAULT_ALIGN
# define LKRHASH_MEM_DEFAULT_ALIGN 8
#endif

#if defined(LKRHASH_ACACHE)

# include <acache.hxx>
  typedef ALLOC_CACHE_HANDLER  CLKRhashAllocator;
# define LKRHASH_ALLOCATOR_NEW(C, N)                            \
    const ALLOC_CACHE_CONFIGURATION acc = { 1, N, sizeof(C) };  \
    C::sm_palloc = new ALLOC_CACHE_HANDLER("IISRTL:" #C, &acc);

#elif defined(LKRHASH_MANODEL)

# include <manodel.hxx>
  typedef MEMORY_ALLOC_NO_DELETE  CLKRhashAllocator;
# define LKRHASH_ALLOCATOR_NEW(C, N)                            \
    C::sm_palloc = new MEMORY_ALLOC_NO_DELETE(sizeof(C),        \
                                              LKRHASH_MEM_DEFAULT_ALIGN);

#elif defined(LKRHASH_MADEL)

# include <madel.hxx>
  typedef MEMORY_ALLOC_DELETE  CLKRhashAllocator;
# define LKRHASH_ALLOCATOR_NEW(C, N)                            \
    C::sm_palloc = new MEMORY_ALLOC_DELETE(sizeof(C),           \
                                           LKRHASH_MEM_DEFAULT_ALIGN, N);

#else // no custom allocator

# undef LKRHASH_ALLOCATOR_NEW

#endif // no custom allocator



// Used to initialize and destroy custom allocators
bool LKRHashTableInit();
void LKRHashTableUninit();


#ifdef LKRHASH_ALLOCATOR_NEW

// placed inline in the declaration of class C
# define LKRHASH_ALLOCATOR_DEFINITIONS(C)                       \
    protected:                                                  \
        static CLKRhashAllocator* sm_palloc;                    \
        friend bool LKRHashTableInit();                         \
        friend void LKRHashTableUninit();                       \
    public:                                                     \
        static void*  operator new(size_t s)                    \
        {                                                       \
          IRTLASSERT(s == sizeof(C));                           \
          IRTLASSERT(sm_palloc != NULL);                        \
          return sm_palloc->Alloc();                            \
        }                                                       \
        static void   operator delete(void* pv)                 \
        {                                                       \
          IRTLASSERT(pv != NULL);                               \
          IRTLASSERT(sm_palloc != NULL);                        \
          sm_palloc->Free(pv);                                  \
        }


// used in LKRHashTableInit()
# define LKRHASH_ALLOCATOR_INIT(C, N, f)                        \
    {                                                           \
        if (f)                                                  \
        {                                                       \
            IRTLASSERT(C::sm_palloc == NULL);                   \
            LKRHASH_ALLOCATOR_NEW(C, N);                        \
            f = (C::sm_palloc != NULL);                         \
        }                                                       \
    }


// used in LKRHashTableUninit()
# define LKRHASH_ALLOCATOR_UNINIT(C)                            \
    {                                                           \
        if (C::sm_palloc != NULL)                               \
        {                                                       \
            delete C::sm_palloc;                                \
            C::sm_palloc = NULL;                                \
        }                                                       \
    }


#else // !LKRHASH_ALLOCATOR_NEW

# define LKRHASH_ALLOCATOR_DEFINITIONS(C)
# define LKRHASH_ALLOCATOR_INIT(C, N, f)
# define LKRHASH_ALLOCATOR_UNINIT(C)

#endif // !LKRHASH_ALLOCATOR_NEW



//--------------------------------------------------------------------
// CLKRLinearHashTable
//
// A thread-safe linear hash table.
//--------------------------------------------------------------------

class IRTL_DLLEXP CLKRLinearHashTable
{
public:
    // typedef CSmallSpinLock   TableLock;
    // typedef CFakeLock        TableLock;
    // typedef CSpinLock        TableLock;
    // typedef CSpinLock2       TableLock;
    // typedef CSpinLock3       TableLock;
    // typedef CShareLock       TableLock;
    // typedef CReaderWriterLock  TableLock;
    // typedef CReaderWriterLock2 TableLock;
    typedef CReaderWriterLock3 TableLock;

    typedef CSmallSpinLock   BucketLock;
    // typedef CFakeLock        BucketLock;
    // typedef CSpinLock        BucketLock;
    // typedef CSpinLock2       BucketLock;
    // typedef CSpinLock3       BucketLock;
    // typedef CShareLock       BucketLock;
    // typedef CReaderWriterLock  BucketLock;
    // typedef CReaderWriterLock2 BucketLock;
    // typedef CReaderWriterLock3 BucketLock;

    class CIterator;
    friend class CLKRLinearHashTable::CIterator;

private:
    class CNodeClump;
    friend class CLKRLinearHashTable::CNodeClump;

    friend class CLKRHashTable;

#ifdef LKRHASH_ALLOCATOR_NEW
    friend bool LKRHashTableInit();
    friend void LKRHashTableUninit();
#endif // LKRHASH_ALLOCATOR_NEW

#ifdef LKRHASH_INSTRUMENTATION
    // TODO
#endif // LKRHASH_INSTRUMENTATION

    enum {
        // Given M = A % B, A and B unsigned 32-bit integers greater than zero,
        // there are no values of A or B which yield M = 2^32-1.  Why?  Because
        // M must be less than B.
        HASH_INVALID_SIGNATURE = ULONG_MAX,
    };


    // Class for nodes on a bucket chain.  Instead of a node containing
    // one (signature, record-pointer, next-tuple-pointer) tuple, it
    // contains _N_ such tuples.  (N-1 next-tuple-pointers are omitted.)
    // This improves locality of reference greatly; i.e., it's L1
    // cache-friendly.  It also reduces memory fragmentation and memory
    // allocator overhead.  It does complicate the chain traversal code
    // slightly, admittedly.
    //
    // This theory is beautiful.  In practice, however, CNodeClumps
    // are *not* perfectly aligned on 32-byte boundaries by the memory
    // allocators.  Experimental results indicate that we get a 2-3%
    // speed improvement by using 32-byte-aligned blocks, but this must
    // be considered against the average of 16 bytes wasted per block.

    class CNodeClump
    {
    public:
        // Record slots per chunk - set so a chunk matches (one or
        // two) cache lines.  2 ==> 28 bytes, 6 ==> 60 bytes
        // Note: the default max load factor is 4.0, which implies that
        // there will seldom be more than one node clump in a chain.
        enum {
            BUCKET_BYTE_SIZE = 64,
            BUCKET_OVERHEAD  = sizeof(BucketLock) + sizeof(CNodeClump*),
            NODE_SIZE        = sizeof(const void*) + sizeof(DWORD),
            NODES_PER_CLUMP  = (BUCKET_BYTE_SIZE - BUCKET_OVERHEAD) / NODE_SIZE
        };

        DWORD  m_dwKeySigs[NODES_PER_CLUMP]; // hash values computed from keys
        CNodeClump* m_pncNext;               // next node clump on the chain
        const void* m_pvNode[NODES_PER_CLUMP];// pointers to records

        CNodeClump()
        {
            Clear();
        }

        void
        Clear()
        {
            m_pncNext = NULL;  // no dangling pointers
            for (DWORD i = 0;  i < NODES_PER_CLUMP;  ++i)
            {
                m_dwKeySigs[i] = HASH_INVALID_SIGNATURE;
                m_pvNode[i] = NULL;
            }
        }

        bool
        InvalidSignature(
            int i) const
        {
            IRTLASSERT(0 <= i  &&  i < NODES_PER_CLUMP);
            return (m_dwKeySigs[i] == HASH_INVALID_SIGNATURE);
        }

        bool
        IsEmptyNode(
            int i) const
        {
            IRTLASSERT(0 <= i  &&  i < NODES_PER_CLUMP);
            return (m_pvNode[i] == NULL);
        }

        bool
        IsLastClump() const
        {
            return (m_pncNext == NULL);
        }

#ifdef _DEBUG
        // Don't want overhead of calls to dtor in retail build
        ~CNodeClump()
        {
            IRTLASSERT(IsLastClump());  // no dangling pointers
            for (DWORD i = 0;  i < NODES_PER_CLUMP;  ++i)
                IRTLASSERT(InvalidSignature(i)  &&  IsEmptyNode(i));
        }
#endif // _DEBUG

        LKRHASH_ALLOCATOR_DEFINITIONS(CNodeClump);
    };


    // Class for bucket chains of the hash table.  Note that the first
    // nodeclump is actually included in the bucket and not dynamically
    // allocated, which increases space requirements slightly but does
    // improve performance.
    class CBucket
    {
    private:
        mutable BucketLock m_Lock;       // lock protecting this bucket

#ifdef LOCK_INSTRUMENTATION
        static LONG sm_cBuckets;

        static const char*
        _LockName()
        {
            LONG l = ++sm_cBuckets;
            // possible race condition but we don't care, as this is never
            // used in production code
            static char s_szName[CLockStatistics::L_NAMELEN];
            wsprintf(s_szName, "B%06x", 0xFFFFFF & l);
            return s_szName;
        }
#endif // LOCK_INSTRUMENTATION

    public:
        CNodeClump    m_ncFirst;    // first CNodeClump of this bucket

#if defined(LOCK_INSTRUMENTATION) || defined(_DEBUG)
        CBucket()
#ifdef LOCK_INSTRUMENTATION
            : m_Lock(_LockName())
#endif // LOCK_INSTRUMENTATION
        {
#ifdef _DEBUG
            LOCK_LOCKTYPE lt = BucketLock::LockType();
            if (lt == LOCK_SPINLOCK  ||  lt == LOCK_FAKELOCK)
                IRTLASSERT(sizeof(*this) <= 64);
#endif _DEBUG
        }
#endif // LOCK_INSTRUMENTATION || _DEBUG

        void  WriteLock()           { m_Lock.WriteLock(); }
        void  ReadLock() const      { m_Lock.ReadLock(); }
        void  WriteUnlock() const   { m_Lock.WriteUnlock();   }
        void  ReadUnlock() const    { m_Lock.ReadUnlock();   }
        bool  IsWriteLocked() const { return m_Lock.IsWriteLocked(); }
        bool  IsReadLocked() const  { return m_Lock.IsReadLocked(); }
        bool  IsWriteUnlocked() const { return m_Lock.IsWriteUnlocked(); }
        bool  IsReadUnlocked() const  { return m_Lock.IsReadUnlocked(); }
        void  SetSpinCount(WORD wSpins) { m_Lock.SetSpinCount(wSpins); }
        WORD  GetSpinCount() const  { return m_Lock.GetSpinCount(); }
#ifdef LOCK_INSTRUMENTATION
        CLockStatistics LockStats() const {return m_Lock.Statistics();}
#endif // LOCK_INSTRUMENTATION
    };


    // The hash table space is divided into fixed-size segments (arrays of
    // CBuckets) and physically grows/shrinks one segment at a time.

    // We provide small, medium, and large segments to better tune the
    // overall memory requirements of the hash table according to the
    // expected usage of an instance.

    class CSegment
    {
    public:
        virtual ~CSegment() {}; // link fails if this is pure virtual
        virtual DWORD Bits() const = 0;
        virtual DWORD Size() const = 0;
        virtual DWORD Mask() const = 0;
        virtual DWORD InitSize() const = 0;
        virtual CBucket& Slot(DWORD i) = 0;
    };


    // Small-sized segments contain 2^3 = 8 buckets => ~0.5Kb
    class CSmallSegment : public CSegment
    {
    public:
        // Maximum table size equals MAX_DIRSIZE * SEGSIZE buckets.
        enum {
            SEGBITS  =            3,// number of bits extracted from a hash
                                    // address for offset within a segment
            SEGSIZE  = (1<<SEGBITS),// segment size
            SEGMASK  = (SEGSIZE-1), // mask used for extracting offset bit
            INITSIZE = 1 * SEGSIZE, // #segments to allocate initially
        };

    private:
        CBucket m_bktSlots[SEGSIZE];

    public:
        virtual ~CSmallSegment()        {}
        virtual DWORD Bits() const      { return SEGBITS; }
        virtual DWORD Size() const      { return SEGSIZE; }
        virtual DWORD Mask() const      { return SEGMASK; }
        virtual DWORD InitSize() const  { return INITSIZE;}
        virtual CBucket& Slot(DWORD i)
        { IRTLASSERT(i < SEGSIZE); return m_bktSlots[i]; }

#ifdef _DEBUG
        CSmallSegment()
        {
            IRTLASSERT(((DWORD_PTR)this & (LKRHASH_MEM_DEFAULT_ALIGN-1)) == 0);
            IRTLASSERT(sizeof(*this)
                       == SEGSIZE * sizeof(CBucket) + sizeof(void*));
        }
#endif // _DEBUG

        LKRHASH_ALLOCATOR_DEFINITIONS(CSmallSegment);
    };


    // Medium-sized segments contain 2^6 = 64 buckets => ~4Kb
    class CMediumSegment : public CSegment
    {
    public:
        enum {
            SEGBITS  =            6,
            SEGSIZE  = (1<<SEGBITS),
            SEGMASK  = (SEGSIZE-1),
            INITSIZE = 2 * SEGSIZE,
        };

    private:
        CBucket m_bktSlots[SEGSIZE];

    public:
        virtual ~CMediumSegment()       {}
        virtual DWORD Bits() const      { return SEGBITS; }
        virtual DWORD Size() const      { return SEGSIZE; }
        virtual DWORD Mask() const      { return SEGMASK; }
        virtual DWORD InitSize() const  { return INITSIZE;}
        virtual CBucket& Slot(DWORD i)
        { IRTLASSERT(i < SEGSIZE); return m_bktSlots[i]; }

#ifdef _DEBUG
        CMediumSegment()
        {
            IRTLASSERT(((DWORD_PTR)this & (LKRHASH_MEM_DEFAULT_ALIGN-1)) == 0);
            IRTLASSERT(sizeof(*this)
                       == SEGSIZE * sizeof(CBucket) + sizeof(void*));
        }
#endif // _DEBUG

        LKRHASH_ALLOCATOR_DEFINITIONS(CMediumSegment);
    };

    // Large-sized segments contain 2^9 = 512 buckets => ~32Kb
    class CLargeSegment : public CSegment
    {
    public:
        enum {
            SEGBITS  =            9,
            SEGSIZE  = (1<<SEGBITS),
            SEGMASK  = (SEGSIZE-1),
            INITSIZE = 4 * SEGSIZE,
        };

    private:
        CBucket m_bktSlots[SEGSIZE];

    public:
        virtual ~CLargeSegment()        {}
        virtual DWORD Bits() const      { return SEGBITS; }
        virtual DWORD Size() const      { return SEGSIZE; }
        virtual DWORD Mask() const      { return SEGMASK; }
        virtual DWORD InitSize() const  { return INITSIZE;}
        virtual CBucket& Slot(DWORD i)
        { IRTLASSERT(i < SEGSIZE); return m_bktSlots[i]; }


#ifdef _DEBUG
        CLargeSegment()
        {
            IRTLASSERT(((DWORD_PTR)this & (LKRHASH_MEM_DEFAULT_ALIGN-1)) == 0);
            IRTLASSERT(sizeof(*this)
                       == SEGSIZE * sizeof(CBucket) + sizeof(void*));
        }
#endif // _DEBUG

        LKRHASH_ALLOCATOR_DEFINITIONS(CLargeSegment);
    };


    // A directory keeps track of the segments comprising the hash table.
    // The directory is just a variable-sized array of pointers to
    // segments (CDirEntrys).
    class CDirEntry
    {
    public:
        // MIN_DIRSIZE and MAX_DIRSIZE can be changed independently
        // of anything else.  Should be powers of two.
        enum {
            MIN_DIRSIZE =  (1<<3),   // minimum directory size
            MAX_DIRSIZE = (1<<20),   // maximum directory size
        };

        CSegment* m_pseg;

        CDirEntry()
            : m_pseg(NULL)
        {}

        ~CDirEntry()
        { delete m_pseg; }
    };

public:

    // aliases for convenience
    enum {
        NODES_PER_CLUMP = CNodeClump::NODES_PER_CLUMP,
        MIN_DIRSIZE     = CDirEntry::MIN_DIRSIZE,
        MAX_DIRSIZE     = CDirEntry::MAX_DIRSIZE,
        NAME_SIZE       = 16,
    };


private:

    // Miscellaneous helper functions

    // Convert a hash signature to a bucket address
    inline DWORD _BucketAddress(DWORD dwSignature) const
    {
        DWORD dwBktAddr = _H0(dwSignature);
        // Has this bucket been split already?
        if (dwBktAddr < m_iExpansionIdx)
            dwBktAddr = _H1(dwSignature);
        IRTLASSERT(dwBktAddr < m_cActiveBuckets);
        IRTLASSERT(dwBktAddr < (m_cDirSegs << m_dwSegBits));
        return dwBktAddr;
    }

    // See the Linear Hashing paper
    static DWORD _H0(DWORD dwSignature, DWORD dwBktAddrMask)
    { return dwSignature & dwBktAddrMask; }

    DWORD        _H0(DWORD dwSignature) const
    { return _H0(dwSignature, m_dwBktAddrMask); }

    // See the Linear Hashing paper.  Preserves one bit more than _H0.
    static DWORD _H1(DWORD dwSignature, DWORD dwBktAddrMask)
    { return dwSignature & ((dwBktAddrMask << 1) | 1); }

    DWORD        _H1(DWORD dwSignature) const
#ifdef LKR_MASK
    { return _H0(dwSignature, m_dwBktAddrMask1); }
#else // !LKR_MASK
    { return _H1(dwSignature, m_dwBktAddrMask); }
#endif // !LKR_MASK

    // In which segment within the directory does the bucketaddress lie?
    // (Return type must be lvalue so that it can be assigned to.)
    CSegment*&   _Segment(DWORD dwBucketAddr) const
    {
        const DWORD iSeg = dwBucketAddr >> m_dwSegBits;
        IRTLASSERT(m_paDirSegs != NULL  &&  iSeg < m_cDirSegs);
        return m_paDirSegs[iSeg].m_pseg;
    }

    // Offset within the segment of the bucketaddress
    DWORD        _SegIndex(DWORD dwBucketAddr) const
    { return (dwBucketAddr & m_dwSegMask); }

    // Convert a bucketaddress to a CBucket*
    inline CBucket* _Bucket(DWORD dwBucketAddr) const
    {
        IRTLASSERT(dwBucketAddr < m_cActiveBuckets);
        CSegment* const pseg = _Segment(dwBucketAddr);
        IRTLASSERT(pseg != NULL);
        return &(pseg->Slot(_SegIndex(dwBucketAddr)));
    }

    // Extract the key from a record
    const DWORD_PTR  _ExtractKey(const void* pvRecord) const
    {
        IRTLASSERT(pvRecord != NULL);
        IRTLASSERT(m_pfnExtractKey != NULL);
#ifdef LKR_SUBTABLE
        return (*m_pfnExtractKey)(pvRecord);
#else
        return (pvRecord != NULL)  ?  (*m_pfnExtractKey)(pvRecord)  :  NULL;
#endif
    }

    // Hash a key
    DWORD        _CalcKeyHash(const DWORD_PTR pnKey) const
    {
        // Note pnKey==0 is acceptable, as the real key type could be an int
        IRTLASSERT(m_pfnCalcKeyHash != NULL);
        DWORD dwHash = (*m_pfnCalcKeyHash)(pnKey);
        // We forcibly scramble the result to help ensure a better distribution
        dwHash = HashScramble(dwHash);
        IRTLASSERT(dwHash != HASH_INVALID_SIGNATURE);
        return dwHash;
    }

    // Compare two keys for equality
    bool       _EqualKeys(const DWORD_PTR pnKey1, const DWORD_PTR pnKey2) const
    {
        IRTLASSERT(m_pfnEqualKeys != NULL);
        return (*m_pfnEqualKeys)(pnKey1, pnKey2);
    }

    // AddRef or Release a record.
    void         _AddRefRecord(const void* pvRecord, int nIncr) const
    {
        IRTLASSERT(pvRecord != NULL  &&  (nIncr == -1  ||  nIncr == +1));
        (*m_pfnAddRefRecord)(pvRecord, nIncr);
    }

    // Find a bucket, given its signature. The bucket is locked before
    // returning. Assumes table is already locked, to avoid race conditions.
    CBucket*     _FindBucket(DWORD dwSignature, bool fLockForWrite) const
    {
        IRTLASSERT(IsValid());
        IRTLASSERT(m_dwBktAddrMask > 0);
        IRTLASSERT((m_dwBktAddrMask & (m_dwBktAddrMask+1)) == 0); // 00011..111
        IRTLASSERT(m_dwBktAddrMask == (1U << m_nLevel) - 1);
#ifdef LKR_MASK
        IRTLASSERT(m_dwBktAddrMask1 == ((m_dwBktAddrMask << 1) | 1));
        IRTLASSERT((m_dwBktAddrMask1 & (m_dwBktAddrMask1+1)) == 0);
#endif // LKR_MASK
        IRTLASSERT(m_iExpansionIdx <= m_dwBktAddrMask);
        IRTLASSERT(0 < m_dwSegBits  &&  m_dwSegBits < 20
                   &&  m_dwSegSize == (1U << m_dwSegBits)
                   &&  m_dwSegMask == (m_dwSegSize - 1));
        IRTLASSERT(IsReadLocked()  ||  IsWriteLocked());

        const DWORD dwBktAddr = _BucketAddress(dwSignature);
        IRTLASSERT(dwBktAddr < m_cActiveBuckets);

        CBucket* const pbkt = _Bucket(dwBktAddr);
        IRTLASSERT(pbkt != NULL);

        if (fLockForWrite)
            pbkt->WriteLock();
        else
            pbkt->ReadLock();

        return pbkt;
    }

    // Memory allocation wrappers to allow us to simulate allocation
    // failures during testing
    static CDirEntry* const
    _AllocateSegmentDirectory(
        size_t n);

    static bool
    _FreeSegmentDirectory(
        CDirEntry* paDirSegs);

    static CNodeClump* const
    _AllocateNodeClump();

    static bool
    _FreeNodeClump(
        CNodeClump* pnc);

    CSegment* const
    _AllocateSegment() const;

    bool
    _FreeSegment(
        CSegment* pseg) const;

#ifdef LOCK_INSTRUMENTATION
    static LONG sm_cTables;

    static const char*
    _LockName()
    {
        LONG l = ++sm_cTables;
        // possible race condition but we don't care, as this is never
        // used in production code
        static char s_szName[CLockStatistics::L_NAMELEN];
        wsprintf(s_szName, "LH%05x", 0xFFFFF & l);
        return s_szName;
    }

    // Statistics for the table lock
    CLockStatistics _LockStats() const
    { return m_Lock.Statistics(); }
#endif // LOCK_INSTRUMENTATION

private:

    // Fields are ordered so as to minimize number of cache lines touched

    DWORD         m_dwSignature;    // debugging: id & corruption check
    CHAR          m_szName[NAME_SIZE];  // an identifier for debugging
    mutable LK_RETCODE m_lkrcState; // Internal state of table
    mutable TableLock m_Lock;       // Lock on entire linear hash table

    // type-specific function pointers
    PFnExtractKey   m_pfnExtractKey;    // Extract key from record
    PFnCalcKeyHash  m_pfnCalcKeyHash;   // Calculate hash signature of key
    PFnEqualKeys    m_pfnEqualKeys;     // Compare two keys
    PFnAddRefRecord m_pfnAddRefRecord;  // AddRef a record

    LK_TABLESIZE  m_lkts;           // "size" of table: small, medium, or large
    DWORD         m_dwSegBits;      // C{Small,Medium,Large}Segment::SEGBITS
    DWORD         m_dwSegSize;      // C{Small,Medium,Large}Segment::SEGSIZE
    DWORD         m_dwSegMask;      // C{Small,Medium,Large}Segment::SEGMASK
    double        m_MaxLoad;        // max load factor (average chain length)

    DWORD         m_dwBktAddrMask;  // mask used for address calculation
#ifdef LKR_MASK
    DWORD         m_dwBktAddrMask1; // used in _H1 calculation
#endif // LKR_MASK
    DWORD         m_iExpansionIdx;  // address of next bucket to be expanded
    CDirEntry*    m_paDirSegs;      // directory of table segments
    DWORD         m_nLevel;         // number of table doublings performed
    DWORD         m_cDirSegs;       // segment directory size: varies between
                                    // MIN_DIRSIZE and MAX_DIRSIZE
    DWORD         m_cRecords;       // number of records in the table
    DWORD         m_cActiveBuckets; // number of buckets in use (table size)

    WORD          m_wBucketLockSpins;// default spin count for bucket locks

#ifdef LKR_NEWCODE
    const BYTE    m_nTableLockType; // for debugging: LOCK_SPINLOCK, etc
    const BYTE    m_nBucketLockType;// for debugging: LOCK_SPINLOCK, etc
    const CLKRHashTable* const m_phtParent;// Owning table. NULL => standalone

    static CLockedDoubleList sm_llGlobalList;// All active CLKRLinearHashTables
    CListEntry    m_leGlobalList;

    void        _InsertThisIntoGlobalList()
    {
        // Only add standalone CLKRLinearHashTables to global list.
        // CLKRHashTables have their own global list.
        if (m_phtParent == NULL)
            sm_llGlobalList.InsertHead(&m_leGlobalList);
    }

    void        _RemoveThisFromGlobalList()
    {
        if (m_phtParent == NULL)
            sm_llGlobalList.RemoveEntry(&m_leGlobalList);
    }
#endif // LKR_NEWCODE

    // Non-trivial implementation functions
    LK_RETCODE   _InsertRecord(const void* pvRecord, DWORD dwSignature,
                               bool fOverwrite);
    LK_RETCODE   _DeleteKey(const DWORD_PTR pnKey, DWORD dwSignature);
    LK_RETCODE   _DeleteRecord(const void* pvRecord, DWORD dwSignature);
    bool         _DeleteNode(CBucket* pbkt, CNodeClump*& rpnc,
                             CNodeClump*& rpncPrev, int& riNode);
    LK_RETCODE   _FindKey(const DWORD_PTR pnKey, DWORD dwSignature,
                          const void** ppvRecord) const;
    LK_RETCODE   _FindRecord(const void* pvRecord, DWORD dwSignature) const;

#ifdef LKR_COMPACT_DELETE
    // returns count of errors in compacted state => 0 is good
    int          _IsNodeCompact(CBucket* const pbkt) const;
#endif // LKR_COMPACT_DELETE


    // Predicate functions
    static LK_PREDICATE _PredTrue(const void* /*pvRecord*/, void* /*pvState*/)
    { return LKP_PERFORM; }

    DWORD        _Apply(PFnRecordAction pfnAction, void* pvState,
                        LK_LOCKTYPE lkl, LK_PREDICATE& rlkp);
    DWORD        _ApplyIf(PFnRecordPred   pfnPredicate,
                          PFnRecordAction pfnAction, void* pvState,
                          LK_LOCKTYPE lkl, LK_PREDICATE& rlkp);
    DWORD        _DeleteIf(PFnRecordPred pfnPredicate, void* pvState,
                           LK_PREDICATE& rlkp);
    void         _Clear(bool fShrinkDirectory);


    void         _SetSegVars(LK_TABLESIZE lkts);
    LK_RETCODE   _Expand();
    LK_RETCODE   _Contract();
    LK_RETCODE   _SplitRecordSet(CNodeClump* pncOldTarget,
                                 CNodeClump* pncNewTarget,
                                 DWORD       iExpansionIdx,
                                 DWORD       dwBktAddrMask,
                                 DWORD       dwNewBkt,
                                 CNodeClump* pncFreeList);
    LK_RETCODE   _MergeRecordSets(CBucket*    pbktNewTarget,
                                  CNodeClump* pncOldList,
                                  CNodeClump* pncFreeList);

    // Private copy ctor and op= to prevent compiler synthesizing them.
    // Must provide a (bad) implementation because we export instantiations.
    // TODO: implement these properly; they could be useful.
    CLKRLinearHashTable(const CLKRLinearHashTable&)
        : m_dwSignature(SIGNATURE_FREE)
#ifdef LOCK_INSTRUMENTATION
        , m_Lock(NULL)
#endif // LOCK_INSTRUMENTATION
#ifdef LKR_NEWCODE
        , m_nTableLockType(0),
          m_nBucketLockType(0),
          m_phtParent(NULL)
#endif // LKR_NEWCODE
    {*(BYTE*)NULL;}

    CLKRLinearHashTable& operator=(const CLKRLinearHashTable&)
    {return *(CLKRLinearHashTable*)NULL;}

#ifdef LKR_NEWCODE
private:
    // This ctor is used by CLKRHashTable
    CLKRLinearHashTable(
        LPCSTR          pszName,        // An identifier for debugging
        PFnExtractKey   pfnExtractKey,  // Extract key from record
        PFnCalcKeyHash  pfnCalcKeyHash, // Calculate hash signature of key
        PFnEqualKeys    pfnEqualKeys,   // Compare two keys
        PFnAddRefRecord pfnAddRefRecord,// AddRef in FindKey, etc
        double          maxload,        // Upperbound on average chain length
        DWORD           initsize,       // Initial size of hash table.
        CLKRHashTable*  phtParent       // Owning table.
        );
#endif // LKR_NEWCODE

    LK_RETCODE
    _Initialize(
        PFnExtractKey   pfnExtractKey,
        PFnCalcKeyHash  pfnCalcKeyHash,
        PFnEqualKeys    pfnEqualKeys,
        PFnAddRefRecord pfnAddRefRecord,
        LPCSTR          pszName,
        double          maxload,
        DWORD           initsize);

public:
    CLKRLinearHashTable(
        LPCSTR          pszName,        // An identifier for debugging
        PFnExtractKey   pfnExtractKey,  // Extract key from record
        PFnCalcKeyHash  pfnCalcKeyHash, // Calculate hash signature of key
        PFnEqualKeys    pfnEqualKeys,   // Compare two keys
        PFnAddRefRecord pfnAddRefRecord,// AddRef in FindKey, etc
        double   maxload=LK_DFLT_MAXLOAD,// Upperbound on average chain length
        DWORD    initsize=LK_DFLT_INITSIZE, // Initial size of hash table.
        DWORD    num_subtbls=LK_DFLT_NUM_SUBTBLS  // for signature compatiblity
                                                  // with CLKRHashTable
        );

    ~CLKRLinearHashTable();

    static const char* ClassName()           {return "CLKRLinearHashTable";}
    int                NumSubTables() const  {return 1;}

    static LK_TABLESIZE NumSubTables(DWORD& rinitsize, DWORD& rnum_subtbls);

    // Insert a new record into hash table.
    // Returns LK_SUCCESS if all OK, LK_KEY_EXISTS if same key already
    // exists (unless fOverwrite), LK_ALLOC_FAIL if out of space,
    // or LK_BAD_RECORD for a bad record.
    LK_RETCODE     InsertRecord(const void* pvRecord, bool fOverwrite=false)
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (pvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        return _InsertRecord(pvRecord, _CalcKeyHash(_ExtractKey(pvRecord)),
                             fOverwrite);
    }

    // Delete record with the given key.
    // Returns LK_SUCCESS if all OK, or LK_NO_SUCH_KEY if not found
    LK_RETCODE     DeleteKey(const DWORD_PTR pnKey)
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;
#endif

        return _DeleteKey(pnKey, _CalcKeyHash(pnKey));
    }

    // Delete a record from the table, if present.
    // Returns LK_SUCCESS if all OK, or LK_NO_SUCH_KEY if not found
    LK_RETCODE     DeleteRecord(const void* pvRecord)
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (pvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        return _DeleteRecord(pvRecord, _CalcKeyHash(_ExtractKey(pvRecord)));
    }

    // Find record with given key.
    // Returns:  LK_SUCCESS, if record found (record is returned in *ppvRecord)
    //           LK_BAD_RECORD, if ppvRecord is invalid
    //           LK_NO_SUCH_KEY, if no record with given key value was found
    //           LK_UNUSABLE, if hash table not in usable state
    // Note: the record is AddRef'd.  You must decrement the reference
    // count when you are finished with the record (if you're implementing
    // refcounting semantics).
    LK_RETCODE     FindKey(const DWORD_PTR pnKey,
                           const void** ppvRecord) const
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (ppvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        return _FindKey(pnKey, _CalcKeyHash(pnKey), ppvRecord);
    }

    // Sees if the record is contained in the table
    // Returns:  LK_SUCCESS, if record found
    //           LK_BAD_RECORD, if pvRecord is invalid
    //           LK_NO_SUCH_KEY, if record is not in the table
    //           LK_UNUSABLE, if hash table not in usable state
    // Note: the record is *not* AddRef'd.
    LK_RETCODE     FindRecord(const void* pvRecord) const
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (pvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        return _FindRecord(pvRecord, _CalcKeyHash(_ExtractKey(pvRecord)));
    }

    // Walk the hash table, applying pfnAction to all records.
    // Locks the whole table for the duration with either a (possibly
    // shared) readlock or a writelock, according to lkl.
    // Loop is aborted if pfnAction returns LKA_ABORT.
    // Returns the number of successful applications.
    DWORD          Apply(PFnRecordAction pfnAction,
                         void*           pvState=NULL,
                         LK_LOCKTYPE     lkl=LKL_READLOCK);

    // Walk the hash table, applying pfnAction to any records that match
    // pfnPredicate.  Locks the whole table for the duration with either
    // a (possibly shared) readlock or a writelock, according to lkl.
    // Loop is aborted if pfnAction returns LKA_ABORT.
    // Returns the number of successful applications.
    DWORD          ApplyIf(PFnRecordPred   pfnPredicate,
                           PFnRecordAction pfnAction,
                           void*           pvState=NULL,
                           LK_LOCKTYPE     lkl=LKL_READLOCK);

    // Delete any records that match pfnPredicate.
    // Locks the table for the duration with a writelock.
    // Returns the number of deletions.
    //
    // Do *not* walk the hash table by hand with an iterator and call
    // DeleteKey.  The iterator will end up pointing to garbage.
    DWORD          DeleteIf(PFnRecordPred pfnPredicate,
                            void*         pvState=NULL);

    // Check table for consistency.  Returns 0 if okay, or the number of
    // errors otherwise.
    int            CheckTable() const;

    // Prints the table (to where??)
    void           Print() const;

    // Remove all data from the table
    void           Clear()
    {
        WriteLock();
        _Clear(true);
        WriteUnlock();
    }

    // Number of elements in the table
    DWORD          Size() const
    { return m_cRecords; }

    // Maximum possible number of elements in the table
    DWORD          MaxSize() const
    { return static_cast<DWORD>(m_MaxLoad * MAX_DIRSIZE * m_dwSegSize); }

    // Get hash table statistics
    CLKRHashTableStats GetStatistics() const;

    // Is the hash table usable?
    bool           IsUsable() const
    { return (m_lkrcState == LK_SUCCESS); }

    // Is the hash table consistent and correct?
    bool           IsValid() const
    {
        bool f = (m_lkrcState == LK_SUCCESS     // serious internal failure?
                  &&  m_paDirSegs != NULL
                  &&  (MIN_DIRSIZE & (MIN_DIRSIZE-1)) == 0  // == (1 << N)
                  &&  (MAX_DIRSIZE & (MAX_DIRSIZE-1)) == 0
                  &&  MAX_DIRSIZE > MIN_DIRSIZE
                  &&  MIN_DIRSIZE <= m_cDirSegs  &&  m_cDirSegs <= MAX_DIRSIZE
                  &&  (m_cDirSegs & (m_cDirSegs-1)) == 0
                  &&  m_pfnExtractKey != NULL
                  &&  m_pfnCalcKeyHash != NULL
                  &&  m_pfnEqualKeys != NULL
                  &&  m_cActiveBuckets > 0
                  &&  ValidSignature()
                  );
        if (!f)
            m_lkrcState = LK_UNUSABLE;
        return f;
    }

    // Set the spin count on the table lock
    void        SetTableLockSpinCount(WORD wSpins)
    { m_Lock.SetSpinCount(wSpins); }

    // Get the spin count on the table lock
    WORD        GetTableLockSpinCount()
    { return m_Lock.GetSpinCount(); }

    // Set/Get the spin count on the bucket locks
    void        SetBucketLockSpinCount(WORD wSpins);
    WORD        GetBucketLockSpinCount();

    enum {
        SIGNATURE =      (('L') | ('K' << 8) | ('L' << 16) | ('H' << 24)),
        SIGNATURE_FREE = (('L') | ('K' << 8) | ('L' << 16) | ('x' << 24)),
    };

    bool
    ValidSignature() const
    { return m_dwSignature == SIGNATURE;}


    // Lock manipulators

    // Lock the table (exclusively) for writing
    void        WriteLock()
    { m_Lock.WriteLock(); }

    // Lock the table (possibly shared) for reading
    void        ReadLock() const
    { m_Lock.ReadLock(); }

    // Unlock the table for writing
    void        WriteUnlock() const
    { m_Lock.WriteUnlock(); }

    // Unlock the table for reading
    void        ReadUnlock() const
    { m_Lock.ReadUnlock(); }

    // Is the table already locked for writing?
    bool        IsWriteLocked() const
    { return m_Lock.IsWriteLocked(); }

    // Is the table already locked for reading?
    bool        IsReadLocked() const
    { return m_Lock.IsReadLocked(); }

    // Is the table unlocked for writing?
    bool        IsWriteUnlocked() const
    { return m_Lock.IsWriteUnlocked(); }

    // Is the table unlocked for reading?
    bool        IsReadUnlocked() const
    { return m_Lock.IsReadUnlocked(); }

    // LKRHASH_ALLOCATOR_DEFINITIONS(CLKRLinearHashTable);

public:

    // Iterators can be used to walk the table.  To ensure a consistent
    // view of the data, the iterator locks the whole table.  This can
    // have a negative effect upon performance, because no other thread
    // can do anything with the table.  Use with care.
    //
    // You should not use an iterator to walk the table, calling DeleteKey,
    // as the iterator will end up pointing to garbage.
    //
    // Use Apply, ApplyIf, or DeleteIf instead of iterators to safely
    // walk the tree.
    //
    // Note that iterators acquire a reference to the record pointed to
    // and release that reference as soon as the iterator is incremented.
    // In other words, this code is safe:
    //     lkrc = ht.IncrementIterator(&iter);
    //     // assume lkrc == LK_SUCCESS for the sake of this example
    //     CMyHashTable::Record* pRec = iter.Record();
    //     Foo(pRec);  // uses pRec but doesn't hang on to it
    //     lkrc = ht.IncrementIterator(&iter);
    //
    // But this code is not safe because pRec is used out of the scope of the
    // iterator that provided it:
    //     lkrc = ht.IncrementIterator(&iter);
    //     CMyHashTable::Record* pRec = iter.Record();
    //     // Should call ht.AddRefRecord(pRec, +1) here
    //     lkrc = ht.IncrementIterator(&iter);
    //     Foo(pRec);
    //
    // If record has no reference-counting semantics, then you can ignore
    // the above remarks about scope.


    class CIterator
    {
    protected:
        friend class CLKRLinearHashTable;

        CLKRLinearHashTable* m_plht;        // which linear hash table?
        DWORD               m_dwBucketAddr; // bucket index
        CNodeClump*         m_pnc;          // a CNodeClump in bucket
        int                 m_iNode;        // offset within m_pnc
        LK_LOCKTYPE         m_lkl;          // readlock or writelock?

    private:
        // Private copy ctor and op= to prevent compiler synthesizing them.
        // Must provide (bad) implementation because we export instantiations.
        CIterator(const CIterator&)             {*(BYTE*)NULL;}
        CIterator& operator=(const CIterator&)  {return *(CIterator*)NULL;}

    public:
        CIterator(
            LK_LOCKTYPE lkl=LKL_WRITELOCK)
            : m_plht(NULL),
              m_dwBucketAddr(0),
              m_pnc(NULL),
              m_iNode(-1),
              m_lkl(lkl)
        {}

        // Return the record associated with this iterator
        const void* Record() const
        {
            IRTLASSERT(IsValid());

            return ((m_pnc != NULL
                        &&  m_iNode >= 0
                        &&  m_iNode < CLKRLinearHashTable::NODES_PER_CLUMP)
                    ?  m_pnc->m_pvNode[m_iNode]
                    :  NULL);
        }

        // Return the key associated with this iterator
        const DWORD_PTR Key() const
        {
            IRTLASSERT(m_plht != NULL);
            const void* pRec = Record();
            return ((pRec != NULL  &&  m_plht != NULL)
                    ?  m_plht->_ExtractKey(pRec)
                    :  NULL);
        }

        bool IsValid() const
        {
            return ((m_plht != NULL)
                    &&  (m_pnc != NULL)
                    &&  (0 <= m_iNode
                         &&  m_iNode < CLKRLinearHashTable::NODES_PER_CLUMP)
                    &&  (!m_pnc->IsEmptyNode(m_iNode)));
        }

        // Delete the record that the iterator points to.  Does an implicit
        // IncrementIterator after deletion.
        LK_RETCODE     DeleteRecord();

        // Change the record that the iterator points to.  The new record
        // must have the same key as the old one.
        LK_RETCODE     ChangeRecord(const void* pNewRec);
    };

    // Const iterators for readonly access.  You must use these with
    // const CLKRLinearHashTables.
    class CConstIterator : public CIterator
    {
    private:
        // Private, unimplemented copy ctor and op= to prevent
        // compiler synthesizing them.
        CConstIterator(const CConstIterator&);
        CConstIterator& operator=(const CConstIterator&);

    public:
        CConstIterator()
            : CIterator(LKL_READLOCK)
        {}
    };

private:
    // The public APIs lock the table.  The private ones, which are used
    // directly by CLKRHashTable, don't.
    LK_RETCODE     _InitializeIterator(CIterator* piter);
    LK_RETCODE     _CloseIterator(CIterator* piter);

public:
    // Initialize the iterator to point to the first item in the hash table
    // Returns LK_SUCCESS, LK_NO_MORE_ELEMENTS, or LK_BAD_ITERATOR.
    LK_RETCODE     InitializeIterator(CIterator* piter)
    {
        IRTLASSERT(piter != NULL  &&  piter->m_plht == NULL);
        if (piter == NULL  ||  piter->m_plht != NULL)
            return LK_BAD_ITERATOR;

        if (piter->m_lkl == LKL_WRITELOCK)
            WriteLock();
        else
            ReadLock();

        return _InitializeIterator(piter);
    }

    // The const iterator version
    LK_RETCODE     InitializeIterator(CConstIterator* piter) const
    {
        IRTLASSERT(piter != NULL  &&  piter->m_plht == NULL);
        IRTLASSERT(piter->m_lkl != LKL_WRITELOCK);

        if (piter == NULL  ||  piter->m_plht != NULL
            ||  piter->m_lkl == LKL_WRITELOCK)
            return LK_BAD_ITERATOR;

        ReadLock();
        return const_cast<CLKRLinearHashTable*>(this)
                    ->_InitializeIterator(static_cast<CIterator*>(piter));
    }

    // Move the iterator on to the next item in the table.
    // Returns LK_SUCCESS, LK_NO_MORE_ELEMENTS, or LK_BAD_ITERATOR.
    LK_RETCODE     IncrementIterator(CIterator* piter);

    LK_RETCODE     IncrementIterator(CConstIterator* piter) const
    {
        IRTLASSERT(piter != NULL  &&  piter->m_plht == this);
        IRTLASSERT(piter->m_lkl != LKL_WRITELOCK);

        if (piter == NULL  ||  piter->m_plht != this
            ||  piter->m_lkl == LKL_WRITELOCK)
            return LK_BAD_ITERATOR;

        return const_cast<CLKRLinearHashTable*>(this)
                    ->IncrementIterator(static_cast<CIterator*>(piter));
    }

    // Close the iterator.
    LK_RETCODE     CloseIterator(CIterator* piter)
    {
        IRTLASSERT(piter != NULL  &&  piter->m_plht == this);
        if (piter == NULL  ||  piter->m_plht != this)
            return LK_BAD_ITERATOR;
        _CloseIterator(piter);

        if (piter->m_lkl == LKL_WRITELOCK)
            WriteUnlock();
        else
            ReadUnlock();

        return LK_SUCCESS;
    };

    // Close the CConstIterator
    LK_RETCODE     CloseIterator(CConstIterator* piter) const
    {
        IRTLASSERT(piter != NULL  &&  piter->m_plht == this);
        IRTLASSERT(piter->m_lkl != LKL_WRITELOCK);

        if (piter == NULL  ||  piter->m_plht != this
            ||  piter->m_lkl == LKL_WRITELOCK)
            return LK_BAD_ITERATOR;

        const_cast<CLKRLinearHashTable*>(this)
             ->_CloseIterator(static_cast<CIterator*>(piter));

        ReadUnlock();
        return LK_SUCCESS;
    };
};



//--------------------------------------------------------------------
// CLKRHashTable
//
// To improve concurrency, a hash table is divided into a number of
// (independent) subtables. Each subtable is a linear hash table. The
// number of subtables is defined when the table is created and remains
// fixed thereafter. Records are assigned to subtables based on their
// hashed key.
//
// For small or low-contention hashtables, you can bypass this
// thin wrapper and use CLKRLinearHashTable directly.  The methods are
// documented in the declarations for CLKRHashTable (above).
//--------------------------------------------------------------------

class IRTL_DLLEXP CLKRHashTable
{
private:
    typedef CLKRLinearHashTable SubTable;

public:
    typedef SubTable::TableLock  TableLock;
    typedef SubTable::BucketLock BucketLock;

    class CIterator;
    friend class CLKRHashTable::CIterator;
    friend class CLKRLinearHashTable;

    // aliases for convenience
    enum {
        NAME_SIZE = SubTable::NAME_SIZE,
        HASH_INVALID_SIGNATURE = SubTable::HASH_INVALID_SIGNATURE,
        NODES_PER_CLUMP = SubTable::NODES_PER_CLUMP,
    };

private:
    // Hash table parameters
    DWORD          m_dwSignature;   // debugging: id & corruption check
    CHAR           m_szName[NAME_SIZE]; // an identifier for debugging
    DWORD          m_cSubTables;    // number of subtables
    SubTable**     m_palhtDir;      // array of subtables

    // type-specific function pointers
    PFnExtractKey  m_pfnExtractKey;
    PFnCalcKeyHash m_pfnCalcKeyHash;
    mutable LK_RETCODE m_lkrcState;     // Internal state of table

#ifdef LKR_NEWCODE
    static CLockedDoubleList sm_llGlobalList; // All active CLKRHashTables
    CListEntry     m_leGlobalList;

    void
    _InsertThisIntoGlobalList()
    {
        sm_llGlobalList.InsertHead(&m_leGlobalList);
    }

    void
    _RemoveThisFromGlobalList()
    {
        sm_llGlobalList.RemoveEntry(&m_leGlobalList);
    }
#endif // LKR_NEWCODE

    LKRHASH_GLOBAL_LOCK_DECLARATIONS();

    // Private copy ctor and op= to prevent compiler synthesizing them.
    // Must provide a (bad) implementation because we export instantiations.
    // TODO: implement these properly; they could be useful.
    CLKRHashTable(const CLKRHashTable&)             {*(BYTE*)NULL;}
    CLKRHashTable& operator=(const CLKRHashTable&)  {return *(CLKRHashTable*)NULL;}


    // Extract the key from the record
    const DWORD_PTR  _ExtractKey(const void* pvRecord) const
    {
        IRTLASSERT(pvRecord != NULL);
        IRTLASSERT(m_pfnExtractKey != NULL);
#ifdef LKR_SUBTABLE
        return (*m_pfnExtractKey)(pvRecord);
#else
        return (pvRecord != NULL)  ?  (*m_pfnExtractKey)(pvRecord)  :  NULL;
#endif
    }

    // Hash the key
    DWORD        _CalcKeyHash(const DWORD_PTR pnKey) const
    {
        // Note pnKey==0 is acceptable, as the real key type could be an int
        IRTLASSERT(m_pfnCalcKeyHash != NULL);
        DWORD dwHash = (*m_pfnCalcKeyHash)(pnKey);
        // We forcibly scramble the result to help ensure a better distribution
        dwHash = HashScramble(dwHash);
        IRTLASSERT(dwHash != HASH_INVALID_SIGNATURE);
        return dwHash;
    }

    // Use the key's hash signature to multiplex into a subtable
    SubTable*    _SubTable(DWORD dwSignature) const
    {
        IRTLASSERT(m_lkrcState == LK_SUCCESS
                   && m_palhtDir != NULL  &&  m_cSubTables > 0);
#ifndef LKR_SUBTABLE
        if (m_lkrcState == LK_SUCCESS)
        {
#endif
            const DWORD PRIME = 1048583UL;  // used to scramble the hash sig
            DWORD       index = (dwSignature % PRIME) % m_cSubTables;
            return m_palhtDir[index];
#ifndef LKR_SUBTABLE
        }
        else
            return NULL;
#endif
    }

    // Memory allocation wrappers to allow us to simulate allocation
    // failures during testing
    static SubTable** const
    _AllocateSubTableArray(
        size_t n);

    static bool
    _FreeSubTableArray(
        SubTable** palht);

    static SubTable* const
    _AllocateSubTable(
        LPCSTR          pszName,        // An identifier for debugging
        PFnExtractKey   pfnExtractKey,  // Extract key from record
        PFnCalcKeyHash  pfnCalcKeyHash, // Calculate hash signature of key
        PFnEqualKeys    pfnEqualKeys,   // Compare two keys
        PFnAddRefRecord pfnAddRefRecord,// AddRef in FindKey, etc
        double          maxload,        // Upperbound on average chain length
        DWORD           initsize,       // Initial size of hash table.
        CLKRHashTable*  phtParent       // Owning table.
    );

    static bool
    _FreeSubTable(
        SubTable* plht);

public:
    CLKRHashTable(
        LPCSTR   pszName,               // An identifier for debugging
        PFnExtractKey   pfnExtractKey,  // Extract key from record
        PFnCalcKeyHash  pfnCalcKeyHash, // Calculate hash signature of key
        PFnEqualKeys    pfnEqualKeys,   // Compare two keys
        PFnAddRefRecord pfnAddRefRecord,// AddRef in FindKey, etc
        double    maxload=LK_DFLT_MAXLOAD,      // bound on avg chain length
        DWORD     initsize=LK_DFLT_INITSIZE,    // Initial size of hash table.
        DWORD     num_subtbls=LK_DFLT_NUM_SUBTBLS  // #subordinate hash tables.
        );

    ~CLKRHashTable();

    static const char* ClassName()           {return "CLKRHashTable";}
    int                NumSubTables() const  {return m_cSubTables;}

    static LK_TABLESIZE NumSubTables(DWORD& rinitsize, DWORD& rnum_subtbls);


    // Thin wrappers for the corresponding methods in CLKRLinearHashTable
    LK_RETCODE     InsertRecord(const void* pvRecord, bool fOverwrite=false)
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (pvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        LKRHASH_GLOBAL_WRITE_LOCK();    // usu. no-op
        DWORD     hash_val  = _CalcKeyHash(_ExtractKey(pvRecord));
        SubTable* const pst = _SubTable(hash_val);
#ifdef LKR_SUBTABLE
        LK_RETCODE lk = pst->_InsertRecord(pvRecord, hash_val, fOverwrite);
#else
        LK_RETCODE lk       = (pst != NULL
                               ?  pst->_InsertRecord(pvRecord, hash_val,
                                                     fOverwrite)
                               :  LK_UNUSABLE);
#endif
        LKRHASH_GLOBAL_WRITE_UNLOCK();    // usu. no-op
        return lk;
    }

    LK_RETCODE     DeleteKey(const DWORD_PTR pnKey)
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;
#endif

        LKRHASH_GLOBAL_WRITE_LOCK();    // usu. no-op
        DWORD     hash_val  = _CalcKeyHash(pnKey);
        SubTable* const pst = _SubTable(hash_val);
#ifdef LKR_SUBTABLE
        LK_RETCODE lk       = pst->_DeleteKey(pnKey, hash_val);
#else
        LK_RETCODE lk       = (pst != NULL
                               ?  pst->_DeleteKey(pnKey, hash_val)
                               :  LK_UNUSABLE);
#endif
        LKRHASH_GLOBAL_WRITE_UNLOCK();    // usu. no-op
        return lk;
    }

    LK_RETCODE     DeleteRecord(const void* pvRecord)
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (pvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        LKRHASH_GLOBAL_WRITE_LOCK();    // usu. no-op
        DWORD     hash_val  = _CalcKeyHash(_ExtractKey(pvRecord));
        SubTable* const pst = _SubTable(hash_val);
#ifdef LKR_SUBTABLE
        LK_RETCODE lk       = pst->_DeleteRecord(pvRecord, hash_val);
#else
        LK_RETCODE lk       = (pst != NULL
                               ?  pst->_DeleteRecord(pvRecord, hash_val)
                               :  LK_UNUSABLE);
#endif
        LKRHASH_GLOBAL_WRITE_UNLOCK();    // usu. no-op
        return lk;
    }

    LK_RETCODE     FindKey(const DWORD_PTR pnKey,
                           const void** ppvRecord) const
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (ppvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        LKRHASH_GLOBAL_READ_LOCK();    // usu. no-op
        DWORD     hash_val   = _CalcKeyHash(pnKey);
        SubTable* const pst  = _SubTable(hash_val);
#ifdef LKR_SUBTABLE
        LK_RETCODE lkrc      = pst->_FindKey(pnKey, hash_val, ppvRecord);
#else
        LK_RETCODE lkrc      = (pst != NULL
                                ?  pst->_FindKey(pnKey, hash_val, ppvRecord)
                                :  LK_UNUSABLE);
#endif
        LKRHASH_GLOBAL_READ_UNLOCK();    // usu. no-op
        return lkrc;
    }

    LK_RETCODE     FindRecord(const void* pvRecord) const
    {
#ifdef LKR_SUBTABLE
        if (!IsUsable())
            return m_lkrcState;

        if (pvRecord == NULL)
            return LK_BAD_RECORD;
#endif

        LKRHASH_GLOBAL_READ_LOCK();    // usu. no-op
        DWORD     hash_val   = _CalcKeyHash(_ExtractKey(pvRecord));
        SubTable* const pst  = _SubTable(hash_val);
#ifdef LKR_SUBTABLE
        LK_RETCODE lkrc      = pst->_FindRecord(pvRecord, hash_val);
#else
        LK_RETCODE lkrc      = (pst != NULL
                                ?  pst->_FindRecord(pvRecord, hash_val)
                                :  LK_UNUSABLE);
#endif
        LKRHASH_GLOBAL_READ_UNLOCK();    // usu. no-op
        return lkrc;
    }

    DWORD          Apply(PFnRecordAction pfnAction,
                         void*           pvState=NULL,
                         LK_LOCKTYPE     lkl=LKL_READLOCK);
    DWORD          ApplyIf(PFnRecordPred   pfnPredicate,
                           PFnRecordAction pfnAction,
                           void*           pvState=NULL,
                           LK_LOCKTYPE     lkl=LKL_READLOCK);
    DWORD          DeleteIf(PFnRecordPred pfnPredicate,
                            void*         pvState=NULL);
    void           Clear();

    int            CheckTable() const;
    void           Print() const;
    DWORD          Size() const;
    DWORD          MaxSize() const;
    CLKRHashTableStats GetStatistics() const;
    bool           IsValid() const;

    void           SetTableLockSpinCount(WORD wSpins);
    WORD           GetTableLockSpinCount();
    void           SetBucketLockSpinCount(WORD wSpins);
    WORD           GetBucketLockSpinCount();

    enum {
        SIGNATURE =      (('L') | ('K' << 8) | ('H' << 16) | ('T' << 24)),
        SIGNATURE_FREE = (('L') | ('K' << 8) | ('H' << 16) | ('x' << 24)),
    };

    bool
    ValidSignature() const
    { return m_dwSignature == SIGNATURE;}

    // Is the hash table usable?
    bool           IsUsable() const
    { return (m_lkrcState == LK_SUCCESS); }

    void        WriteLock();
    void        ReadLock() const;
    void        WriteUnlock() const;
    void        ReadUnlock() const;
    bool        IsWriteLocked() const;
    bool        IsReadLocked() const;
    bool        IsWriteUnlocked() const;
    bool        IsReadUnlocked() const;


    // LKRHASH_ALLOCATOR_DEFINITIONS(CLKRHashTable);

public:
    typedef SubTable::CIterator CLHTIterator;

    class CIterator : public CLHTIterator
    {
    protected:
        friend class CLKRHashTable;

        CLKRHashTable*  m_pht;  // which hash table?
        int             m_ist;  // which subtable

    private:
        // Private copy ctor and op= to prevent compiler synthesizing them.
        // Must provide (bad) implementation because we export instantiations.
        CIterator(const CIterator&)             {*(BYTE*)NULL;}
        CIterator& operator=(const CIterator&)  {return *(CIterator*)NULL;}

    public:
        CIterator(
            LK_LOCKTYPE lkl=LKL_WRITELOCK)
            : CLHTIterator(lkl),
              m_pht(NULL),
              m_ist(-1)
        {}

        const void* Record() const
        {
            IRTLASSERT(IsValid());

            // This is a hack to work around a compiler bug.  Calling
            // CLHTIterator::Record calls this function recursively until
            // the stack overflows.
            const CLHTIterator* pBase = static_cast<const CLHTIterator*>(this);
            return pBase->Record();
        }

        const DWORD_PTR Key() const
        {
            IRTLASSERT(IsValid());
            const CLHTIterator* pBase = static_cast<const CLHTIterator*>(this);
            return pBase->Key();
        }

        bool IsValid() const
        {
            const CLHTIterator* pBase = static_cast<const CLHTIterator*>(this);
            return (m_pht != NULL  &&  m_ist >= 0  &&  pBase->IsValid());
        }
    };

    // Const iterators for readonly access
    class CConstIterator : public CIterator
    {
    private:
        // Private, unimplemented copy ctor and op= to prevent
        // compiler synthesizing them.
        CConstIterator(const CConstIterator&);
        CConstIterator& operator=(const CConstIterator&);

    public:
        CConstIterator()
            : CIterator(LKL_READLOCK)
        {}
    };


public:
    LK_RETCODE     InitializeIterator(CIterator* piter);
    LK_RETCODE     IncrementIterator(CIterator* piter);
    LK_RETCODE     CloseIterator(CIterator* piter);

    LK_RETCODE     InitializeIterator(CConstIterator* piter) const
    {
        IRTLASSERT(piter != NULL  &&  piter->m_pht == NULL);
        IRTLASSERT(piter->m_lkl != LKL_WRITELOCK);

        if (piter == NULL  ||  piter->m_pht != NULL
            ||  piter->m_lkl == LKL_WRITELOCK)
            return LK_BAD_ITERATOR;

        return const_cast<CLKRHashTable*>(this)
                ->InitializeIterator(static_cast<CIterator*>(piter));
    }

    LK_RETCODE     IncrementIterator(CConstIterator* piter) const
    {
        IRTLASSERT(piter != NULL  &&  piter->m_pht == this);
        IRTLASSERT(piter->m_lkl != LKL_WRITELOCK);

        if (piter == NULL  ||  piter->m_pht != this
            ||  piter->m_lkl == LKL_WRITELOCK)
            return LK_BAD_ITERATOR;

        return const_cast<CLKRHashTable*>(this)
                ->IncrementIterator(static_cast<CIterator*>(piter));
    }

    LK_RETCODE     CloseIterator(CConstIterator* piter) const
    {
        IRTLASSERT(piter != NULL  &&  piter->m_pht == this);
        IRTLASSERT(piter->m_lkl != LKL_WRITELOCK);

        if (piter == NULL  ||  piter->m_pht != this
            ||  piter->m_lkl == LKL_WRITELOCK)
            return LK_BAD_ITERATOR;

        return const_cast<CLKRHashTable*>(this)
                ->CloseIterator(static_cast<CIterator*>(piter));
    }
};



//--------------------------------------------------------------------
// A typesafe wrapper for CLKRHashTable (or CLKRLinearHashTable).
//
// * _Derived must derive from CTypedHashTable and provide certain member
//   functions.  It's needed for various downcasting operations.  See
//   CStringTestHashTable and CNumberTestHashTable below.
// * _Record is the type of the record.  C{Linear}HashTable will store
//   pointers to _Record.
// * _Key is the type of the key.  _Key is used directly; i.e., it is
//   not assumed to be a pointer type.  C{Linear}HashTable assumes that
//   the key is stored in the associated record.  See the comments
//   at the declaration of PFnExtractKey for more details.
//
// (optional parameters):
// * _BaseHashTable is the base hash table: CLKRHashTable or
///   CLKRLinearHashTable
// * _BaseIterator is the iterator type, _BaseHashTable::CIterator
//
// CTypedHashTable could derive directly from CLKRLinearHashTable, if you
// don't need the extra overhead of CLKRHashTable (which is quite low).
//
// You may need to add the following line to your code to disable
// warning messages about truncating extremly long identifiers.
//   #pragma warning (disable : 4786)
//--------------------------------------------------------------------

#define LKRHASH_HACKY_CAST(T, pv)  ((T) (UINT_PTR) (pv))

template < class _Derived, class _Record, class _Key,
           class _BaseHashTable=CLKRHashTable,
           class _BaseIterator=_BaseHashTable::CIterator
         >
class CTypedHashTable : public _BaseHashTable
{
public:
    // convenient aliases
    typedef _Derived        Derived;
    typedef _Record         Record;
    typedef _Key            Key;
    typedef _BaseHashTable  BaseHashTable;
    typedef CTypedHashTable<_Derived, _Record, _Key,
                                 _BaseHashTable, _BaseIterator>
                            HashTable;
    typedef _BaseIterator   BaseIterator;

    // ApplyIf() and DeleteIf(): Does the record match the predicate?
    // Note: takes a Record*, not a const Record*.  You can modify the
    // record in Pred() or Action(), if you like, but if you do, you
    // should use LKL_WRITELOCK to lock the table.
    typedef LK_PREDICATE (WINAPI *PFnRecordPred) (Record* pRec, void* pvState);

    // Apply() et al: Perform action on record.
    typedef LK_ACTION   (WINAPI *PFnRecordAction)(Record* pRec, void* pvState);

private:

    // Wrappers for the typesafe methods exposed by the derived class

    static const DWORD_PTR
    _ExtractKey(const void* pvRecord)
    {
        const _Record* pRec = static_cast<const _Record*>(pvRecord);
        _Key           key  = static_cast<_Key>(_Derived::ExtractKey(pRec));
        // I would prefer to use reinterpret_cast here, but the stupid
        // Win64 compiler thinks it knows better than I do.
        return (const DWORD_PTR) key;
    }

    static DWORD
    _CalcKeyHash(const DWORD_PTR pnKey)
    {
        _Key key = LKRHASH_HACKY_CAST(_Key, pnKey);
        return _Derived::CalcKeyHash(key);
    }

    static bool
    _EqualKeys(const DWORD_PTR pnKey1, const DWORD_PTR pnKey2)
    {
        _Key key1 = LKRHASH_HACKY_CAST(_Key, pnKey1);
        _Key key2 = LKRHASH_HACKY_CAST(_Key, pnKey2);
        return _Derived::EqualKeys(key1, key2);
    }

    static void
    _AddRefRecord(const void* pvRecord, int nIncr)
    {
        _Record* pRec = static_cast<_Record*>(const_cast<void*>(pvRecord));
        _Derived::AddRefRecord(pRec, nIncr);
    }


    // Typesafe wrappers for Apply, ApplyIf, and DeleteIf.

    class CState
    {
    public:
        PFnRecordPred   m_pfnPred;
        PFnRecordAction m_pfnAction;
        void*           m_pvState;

        CState(
            PFnRecordPred   pfnPred,
            PFnRecordAction pfnAction,
            void*           pvState)
            : m_pfnPred(pfnPred), m_pfnAction(pfnAction), m_pvState(pvState)
        {}
    };

    static LK_PREDICATE
    _Pred(const void* pvRecord, void* pvState)
    {
        _Record* pRec = static_cast<_Record*>(const_cast<void*>(pvRecord));
        CState*  pState = static_cast<CState*>(pvState);

        return (*pState->m_pfnPred)(pRec, pState->m_pvState);
    }

    static LK_ACTION
    _Action(const void* pvRecord, void* pvState)
    {
        _Record* pRec = static_cast<_Record*>(const_cast<void*>(pvRecord));
        CState*  pState = static_cast<CState*>(pvState);

        return (*pState->m_pfnAction)(pRec, pState->m_pvState);
    }

public:
    CTypedHashTable(
        LPCSTR pszName,                       // An identifier for debugging
        double maxload=LK_DFLT_MAXLOAD,       // Upperbound on avg chain length
        DWORD  initsize=LK_DFLT_INITSIZE,     // Initial size of hash table.
        DWORD  num_subtbls=LK_DFLT_NUM_SUBTBLS// #subordinate hash tables.
        )
        : _BaseHashTable(pszName, _ExtractKey, _CalcKeyHash, _EqualKeys,
                            _AddRefRecord, maxload, initsize, num_subtbls)
    {}

    LK_RETCODE   InsertRecord(const _Record* pRec, bool fOverwrite=false)
    { return _BaseHashTable::InsertRecord(pRec, fOverwrite); }

    LK_RETCODE   DeleteKey(const _Key key)
    { return _BaseHashTable::DeleteKey(reinterpret_cast<const DWORD_PTR>(key));}

    LK_RETCODE   DeleteRecord(const _Record* pRec)
    { return _BaseHashTable::DeleteRecord(pRec);}

    // Note: returns a _Record**, not a const Record**.  Note that you
    // can use a const type for the template parameter to ensure constness.
    LK_RETCODE   FindKey(const _Key key, _Record** ppRec) const
    {
        if (ppRec == NULL)
            return LK_BAD_RECORD;
        *ppRec = NULL;
        const void* pvRec = NULL;
        const void* pvKey = reinterpret_cast<const void*>((DWORD_PTR)(key));
        DWORD_PTR pnKey = reinterpret_cast<DWORD_PTR>(pvKey);
        LK_RETCODE lkrc = _BaseHashTable::FindKey(pnKey, &pvRec);
        *ppRec = static_cast<_Record*>(const_cast<void*>(pvRec));
        return lkrc;
    }

    LK_RETCODE   FindRecord(const _Record* pRec) const
    { return _BaseHashTable::FindRecord(pRec);}


    // Other C{Linear}HashTable methods can be exposed without change
    // TODO: Print?


    // Typesafe wrappers for Apply et al

    DWORD        Apply(PFnRecordAction pfnAction,
                       void*           pvState=NULL,
                       LK_LOCKTYPE     lkl=LKL_READLOCK)
    {
        IRTLASSERT(pfnAction != NULL);
        if (pfnAction == NULL)
            return 0;

        CState   state(NULL, pfnAction, pvState);
        return   _BaseHashTable::Apply(_Action, &state, lkl);
    }

    DWORD        ApplyIf(PFnRecordPred   pfnPredicate,
                         PFnRecordAction pfnAction,
                         void*           pvState=NULL,
                         LK_LOCKTYPE     lkl=LKL_READLOCK)
    {
        IRTLASSERT(pfnPredicate != NULL  &&  pfnAction != NULL);
        if (pfnPredicate == NULL  ||  pfnAction == NULL)
            return 0;

        CState   state(pfnPredicate, pfnAction, pvState);
        return   _BaseHashTable::ApplyIf(_Pred, _Action, &state, lkl);
    }

    DWORD        DeleteIf(PFnRecordPred pfnPredicate, void* pvState=NULL)
    {
        IRTLASSERT(pfnPredicate != NULL);
        if (pfnPredicate == NULL)
            return 0;

        CState   state(pfnPredicate, NULL, pvState);
        return   _BaseHashTable::DeleteIf(_Pred, &state);
    }


    // Typesafe wrappers for iterators


    class CIterator : public _BaseIterator
    {
    private:
        // Private, unimplemented copy ctor and op= to prevent
        // compiler synthesizing them.
        CIterator(const CIterator&);
        CIterator& operator=(const CIterator&);

    public:
        CIterator(
            LK_LOCKTYPE lkl=LKL_WRITELOCK)
            : _BaseIterator(lkl)
        {}

        _Record*  Record() const
        {
            const _BaseIterator* pBase = static_cast<const _BaseIterator*>(this);
            return reinterpret_cast<_Record*>(const_cast<void*>(
                        pBase->Record()));
        }

        _Key      Key() const
        {
            const _BaseIterator* pBase = static_cast<const _BaseIterator*>(this);
            return reinterpret_cast<_Key>(reinterpret_cast<void*>(pBase->Key()));
        }
    };

    // readonly iterator
    class CConstIterator : public CIterator
    {
    private:
        // Private, unimplemented copy ctor and op= to prevent
        // compiler synthesizing them.
        CConstIterator(const CConstIterator&);
        CConstIterator& operator=(const CConstIterator&);

    public:
        CConstIterator()
            : CIterator(LKL_READLOCK)
        {}

        const _Record*  Record() const
        {
            return CIterator::Record();
        }

        const _Key      Key() const
        {
            return CIterator::Key();
        }
    };


public:
    LK_RETCODE     InitializeIterator(CIterator* piter)
    {
        return _BaseHashTable::InitializeIterator(piter);
    }

    LK_RETCODE     IncrementIterator(CIterator* piter)
    {
        return _BaseHashTable::IncrementIterator(piter);
    }

    LK_RETCODE     CloseIterator(CIterator* piter)
    {
        return _BaseHashTable::CloseIterator(piter);
    }

    LK_RETCODE     InitializeIterator(CConstIterator* piter) const
    {
        return const_cast<HashTable*>(this)
                ->InitializeIterator(static_cast<CIterator*>(piter));
    }

    LK_RETCODE     IncrementIterator(CConstIterator* piter) const
    {
        return const_cast<HashTable*>(this)
                ->IncrementIterator(static_cast<CIterator*>(piter));
    }

    LK_RETCODE     CloseIterator(CConstIterator* piter) const
    {
        return const_cast<HashTable*>(this)
                ->CloseIterator(static_cast<CIterator*>(piter));
    }

};



#ifdef __LKRHASH_NAMESPACE__
}
#endif // __LKRHASH_NAMESPACE__


#endif // __LKRHASH_H__


#ifdef SAMPLE_LKRHASH_TESTCLASS


//--------------------------------------------------------------------
// An example of how to create a wrapper for CLKRHashTable
//--------------------------------------------------------------------

// some random class

class CTest
{
public:
    enum {BUFFSIZE=20};

    int   m_n;                  // This will also be a key
    char  m_sz[BUFFSIZE];       // This will be the primary key
    bool  m_fWhatever;
    mutable LONG  m_cRefs;      // Reference count for lifetime management.
                                // Must be mutable to use 'const CTest*' in
                                // hashtables

    CTest(int n, const char* psz, bool f)
        : m_n(n), m_fWhatever(f), m_cRefs(0)
    {
        strncpy(m_sz, psz, BUFFSIZE-1);
        m_sz[BUFFSIZE-1] = '\0';
    }

    ~CTest()
    {
        IRTLASSERT(m_cRefs == 0);
    }
};



// A typed hash table of CTests, keyed on the string field.  Case-insensitive.

class CStringTestHashTable
    : public CTypedHashTable<CStringTestHashTable, const CTest, const char*>
{
public:
    CStringTestHashTable()
        : CTypedHashTable<CStringTestHashTable, const CTest,
                          const char*>("string", LK_DFLT_MAXLOAD,
                                       LK_SMALL_TABLESIZE)
    {}

    static const char*
    ExtractKey(const CTest* pTest)
    {
        return pTest->m_sz;
    }

    static DWORD
    CalcKeyHash(const char* pszKey)
    {
        return HashStringNoCase(pszKey);
    }

    static bool
    EqualKeys(const char* pszKey1, const char* pszKey2)
    {
        return _stricmp(pszKey1, pszKey2) == 0;
    }

    static void
    AddRefRecord(const CTest* pTest, int nIncr)
    {
        if (nIncr == +1)
        {
            // or, perhaps, pIFoo->AddRef() (watch out for marshalling)
            // or ++pTest->m_cRefs (single-threaded only)
            InterlockedIncrement(&pTest->m_cRefs);
        }
        else if (nIncr == -1)
        {
            // or, perhaps, pIFoo->Release() or --pTest->m_cRefs;
            LONG l = InterlockedDecrement(&pTest->m_cRefs);

            // For some hashtables, it may also make sense to add the following
            //      if (l == 0) delete pTest;
            // but that would typically only apply when InsertRecord was
            // used thus
            //      lkrc = ht.InsertRecord(new CTest(foo, bar));
        }
        else
            IRTLASSERT(0);

        TRACE("AddRef(%p, %s) %d, cRefs == %d\n",
              pTest, pTest->m_sz, nIncr, pTest->m_cRefs);
    }
};


// Another typed hash table of CTests.  This one is keyed on the numeric field.

class CNumberTestHashTable
    : public CTypedHashTable<CNumberTestHashTable, const CTest, int>
{
public:
    CNumberTestHashTable()
        : CTypedHashTable<CNumberTestHashTable, const CTest, int>("number") {}
    static int   ExtractKey(const CTest* pTest)        {return pTest->m_n;}
    static DWORD CalcKeyHash(int nKey)                 {return Hash(nKey);}
    static bool  EqualKeys(int nKey1, int nKey2)       {return nKey1 == nKey2;}
    static void  AddRefRecord(const CTest* pTest, int nIncr) {/* do nothing*/}
};


// A class to exercise ApplyIf()

class CApplyIfTest
{
public:
    static LK_PREDICATE
    Predicate(const CTest* pTest, void* pvState)
    {
        CApplyIfTest* pThis = static_cast<CApplyIfTest*>(pvState);
        ++pThis->m_cPreds;
        TRACE("CApplyIfTest::Predicate(%p (%s, %d), %p)\n",
              pTest, pTest->m_sz, pTest->m_n, pThis);
        return (pTest->m_n % 10 == 7)  ?  LKP_PERFORM  :   LKP_NO_ACTION;
    }

    static LK_ACTION
    Action(const CTest* pTest, void* pvState)
    {
        CApplyIfTest* pThis = static_cast<CApplyIfTest*>(pvState);
        ++pThis->m_cActions;
        LK_ACTION lka = (pTest->m_n > 30)  ?  LKA_SUCCEEDED  :  LKA_FAILED;

        TRACE("CApplyIfTest::Action(%p (%s, %d), %p) %s\n",
              pTest, pTest->m_sz, pTest->m_n, pThis,
              lka == LKA_SUCCEEDED ? "succeeded" : "failed");

        if (lka == LKA_SUCCEEDED)
            ++pThis->m_cSuccesses;
        else if (lka == LKA_FAILED)
            ++pThis->m_cFailures;

        return lka;
    }

    int m_cPreds;
    int m_cActions;
    int m_cSuccesses;
    int m_cFailures;

    CApplyIfTest()
        : m_cPreds(0), m_cActions(0), m_cSuccesses(0), m_cFailures(0)
    {}
};


// The Predicate and Action functions can be static member functions,
// but don't have to be

LK_PREDICATE
DeleteIfGt10(
    const CTest* pTest,
    void* pvState)
{
    TRACE("DeleteIfGt10(%p, %s, %p) = %d\n",
          pTest, pTest->m_sz, pvState, pTest->m_n);
    return (pTest->m_n > 10)  ?  LKP_PERFORM  :   LKP_NO_ACTION;
}


#include <stdio.h>
#include <string.h>

void Test(
    bool fVerbose)
{
    // Some objects for the hash tables
    CTest tl(5,  "Larson",   true);
    CTest tk(17, "Krishnan", false);
    CTest tr(37, "Reilly",   true);

    // A string-keyed hash table
    CStringTestHashTable stht;

    IRTLVERIFY(LK_SUCCESS == stht.InsertRecord(&tl));
    IRTLVERIFY(LK_SUCCESS == stht.InsertRecord(&tk));
    IRTLVERIFY(LK_SUCCESS == stht.InsertRecord(&tr));

    TRACE("Check the overwrite feature of InsertRecord\n");
    IRTLVERIFY(LK_KEY_EXISTS == stht.InsertRecord(&tr, false));
    IRTLASSERT(tr.m_cRefs == 1);
    IRTLVERIFY(LK_SUCCESS == stht.InsertRecord(&tr, true));
    IRTLASSERT(tr.m_cRefs == 1);    // 1+1-1 == 1

    TRACE("Check that the keys are really present in the table and that "
          "the refcounting works\n");
    const CTest* pTest = NULL;
    IRTLVERIFY(LK_SUCCESS == stht.FindKey(tl.m_sz, &pTest)  &&  pTest == &tl);
    IRTLASSERT(tl.m_cRefs == 2);
    IRTLVERIFY(LK_SUCCESS == stht.FindKey(tk.m_sz, &pTest)  &&  pTest == &tk);
    IRTLASSERT(tk.m_cRefs == 2);
    IRTLVERIFY(LK_SUCCESS == stht.FindKey(tr.m_sz, &pTest)  &&  pTest == &tr);
    IRTLASSERT(tr.m_cRefs == 2);
    IRTLVERIFY(LK_SUCCESS == stht.FindRecord(&tr));
    IRTLASSERT(tr.m_cRefs == 2);    // FindRecord does not addref

    TRACE("Look for a key under an alternate spelling (case-insensitive)\n");
    IRTLVERIFY(LK_SUCCESS == stht.FindKey("rEiLlY", &pTest)  &&  pTest == &tr);
    IRTLASSERT(tr.m_cRefs == 3);

    TRACE("Release the references added by FindKey\n");
    stht.AddRefRecord(&tl, -1);
    tk.m_cRefs--;
    tr.m_cRefs = 1;

    TRACE("Now build the numeric hash table\n");
    CNumberTestHashTable ntht;

    IRTLVERIFY(LK_SUCCESS == ntht.InsertRecord(&tl));
    IRTLVERIFY(LK_SUCCESS == ntht.InsertRecord(&tk));
    IRTLVERIFY(LK_SUCCESS == ntht.InsertRecord(&tr));

    TRACE("Test ApplyIf()\n");
    CApplyIfTest ait;

    IRTLVERIFY(1 == ntht.ApplyIf(ait.Predicate, ait.Action, &ait));
    IRTLASSERT(3 == ait.m_cPreds  &&  2 == ait.m_cActions
               &&  1 == ait.m_cSuccesses  &&  1 == ait.m_cFailures);

    TRACE("Test DeleteIf()\n");
    IRTLASSERT(3 == ntht.Size());
    ntht.DeleteIf(DeleteIfGt10, NULL);
    IRTLASSERT(1 == ntht.Size());

    TRACE("Check that the keys that were supposed to be deleted "
          "really are gone\n");
    IRTLASSERT(tl.m_n <= 10);
    IRTLVERIFY(LK_SUCCESS == ntht.FindKey(tl.m_n, &pTest)  &&  pTest == &tl);
    IRTLASSERT(tk.m_n >  10);
    IRTLVERIFY(LK_NO_SUCH_KEY == ntht.FindKey(tk.m_n, &pTest)
               &&  pTest == NULL);
    IRTLASSERT(tr.m_n >  10);
    IRTLVERIFY(LK_NO_SUCH_KEY == ntht.FindKey(tr.m_n, &pTest)
               &&  pTest == NULL);

    IRTLVERIFY(LK_SUCCESS == ntht.DeleteRecord(&tl));
    IRTLASSERT(0 == ntht.Size());

    TRACE("Check Iterators\n");
    DWORD  cRec = 0;
    CStringTestHashTable::CIterator iter;
    LK_RETCODE lkrc = stht.InitializeIterator(&iter);

    while (lkrc == LK_SUCCESS)
    {
        ++cRec;
        CStringTestHashTable::Key     pszKey = iter.Key();
        CStringTestHashTable::Record* pRec   = iter.Record();

        IRTLASSERT(pRec == &tl  ||  pRec == &tk  ||  pRec == &tr);
        if (fVerbose)
            printf("Record(%p) contains \"%s\"\n", pRec, pszKey);
        lkrc = stht.IncrementIterator(&iter);
    }

    IRTLASSERT(lkrc == LK_NO_MORE_ELEMENTS);

    lkrc = stht.CloseIterator(&iter);
    IRTLASSERT(lkrc == LK_SUCCESS);
    IRTLASSERT(cRec == stht.Size());

    TRACE("Check const iterators\n");
    const CStringTestHashTable& sthtConst = stht;
    CStringTestHashTable::CConstIterator iterConst;
    cRec = 0;

    lkrc = sthtConst.InitializeIterator(&iterConst);

    while (lkrc == LK_SUCCESS)
    {
        ++cRec;
        const CStringTestHashTable::Key     pszKey = iterConst.Key();
        const CStringTestHashTable::Record* pRec   = iterConst.Record();

        IRTLASSERT(pRec == &tl  ||  pRec == &tk  ||  pRec == &tr);
        if (fVerbose)
            printf("Const Record(%p) contains \"%s\"\n", pRec, pszKey);
        lkrc = sthtConst.IncrementIterator(&iterConst);
    }

    IRTLASSERT(lkrc == LK_NO_MORE_ELEMENTS);

    lkrc = sthtConst.CloseIterator(&iterConst);
    IRTLASSERT(lkrc == LK_SUCCESS);
    IRTLASSERT(cRec == sthtConst.Size());

#if 1
    TRACE("Check Clear\n");
    stht.Clear();
    IRTLASSERT(0 == stht.Size());
#else
    TRACE("Check DeleteKey\n");
    IRTLVERIFY(LK_SUCCESS == stht.DeleteKey(tl.m_sz));
    IRTLVERIFY(LK_SUCCESS == stht.DeleteKey(tk.m_sz));
    IRTLVERIFY(LK_SUCCESS == stht.DeleteKey(tr.m_sz));
#endif

    TRACE("Test done\n");
    // ~CTest will check for m_cRefs==0
}

#endif // SAMPLE_LKRHASH_TESTCLASS
