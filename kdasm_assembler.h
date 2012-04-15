#ifndef KDASM_ASSEMBLER_H
#define KDASM_ASSEMBLER_H
// Copyright (c) 2012 Adrian Johnston.  All rights reserved.
// See Copyright Notice in kdasm.h
// Project Homepage: http://code.google.com/p/kdasm/

#include <vector>
#include <deque>

#include "kdasm.h"

// ----------------------------------------------------------------------------

// Enables validation of internal processing. Useful during development to catch
// problems close to where they occur.
//#define KDASM_INTERNAL_VALIDATION

bool KdasmAssertFail( const char* expression, const char* file, int line );
#if defined(_DEBUG) || defined(KDASM_INTERNAL_VALIDATION)
#define KdasmAssertInternal( x ) (void)( !!( x ) || KdasmAssertFail( "Internal Error", __FILE__, __LINE__ ),0 )
#else
#define KdasmAssertInternal( x ) (void)0
#endif
#define KdasmAssert( s, x ) (void)( !!( x ) || KdasmAssertFail( ( s ), __FILE__, __LINE__ ),0 )

class KdasmAssemblerVirtualPage;
struct KdasmAssemblerPageTempData;
struct KdasmAssemblerNodeTempData;

// ----------------------------------------------------------------------------
// KdasmAssemblerNode
//
// This is the intermediate representation fed to the assembler.  Enforces not
// having leaves attached to branch nodes.  (As that has it's own overhead and
// the same result could be achived with the insertion of a special branch...)

class KdasmAssemblerNode
{
public:
    KdasmAssemblerNode( void )                                  { ::memset( this, 0, sizeof *this ); }
    ~KdasmAssemblerNode( void )                                 { Clear(); }

    bool HasSubnodes( void ) const                              { return m_subnodes[0] || m_subnodes[1]; }
    KdasmU16 GetNormal( void ) const                            { return m_normal; }
    const KdasmU16* GetDistance( void ) const                   { return m_distance; }
    int GetDistanceLength( void ) const                         { return m_distanceLength; }
    const KdasmAssemblerNode* GetSubnode( intptr_t i ) const    { KdasmAssert( "Index out of range", i >= 0 && i < 2 ); return m_subnodes[i]; }
          KdasmAssemblerNode* GetSubnode( intptr_t i )          { KdasmAssert( "Index out of range", i >= 0 && i < 2 ); return m_subnodes[i]; }
    intptr_t GetLeafCount( void ) const                         { return m_leafCount; }
    KdasmU16* GetLeaves( void )                                 { return m_leaves; }

    void AddSubnodes( KdasmU16 distance, KdasmU16 normal, KdasmAssemblerNode* less, KdasmAssemblerNode* greater );
    // Distance length should remain constant across entire tree as it is only encoded in the header.
    void AddSubnodes( intptr_t distance, int distanceLength, KdasmU16 normal, KdasmAssemblerNode* less, KdasmAssemblerNode* greater );
    void AddSubnodes( KdasmU16* distance, int distanceLength, KdasmU16 normal, KdasmAssemblerNode* less, KdasmAssemblerNode* greater );
    void AddLeaves( intptr_t leafCount, KdasmU16* leaves );
    void Clear( void );
    bool Equals( const KdasmAssemblerNode& n, bool checkSubnodes=true ) const;
    bool TrimEmpty( void ); // Canonicalizes.  Returns true if root node is empty.

    // Internal
    KdasmAssemblerVirtualPage* GetVirtualPage( void )           { return m_virtualPage; }
    void SetVirtualPage( KdasmAssemblerVirtualPage* pg )        { m_virtualPage = pg; }    
    intptr_t GetPhysicalPageStart( void );
    void SetPageTemp( KdasmAssemblerPageTempData* t )           { m_pageTempData = t; }
    KdasmAssemblerPageTempData* GetPageTemp( void );
    const KdasmAssemblerNodeTempData* GetNodeTemp( void ) const { return m_nodeTempData; }
          KdasmAssemblerNodeTempData* GetNodeTemp( void )       { return m_nodeTempData; }
    intptr_t AssemblePrepare( KdasmAssemblerNode* supernode, intptr_t nextCompareToId );
    void AssembleFinish( void );
    // Debug ID.
    intptr_t GetCompareToId( void )                             { return m_compareToId; }

private:
    KdasmAssemblerNode( KdasmAssemblerNode& ); // undefined

    KdasmU16                    m_normal;
    KdasmU16                    m_distance[KdasmEncodingHeader::DISTANCE_LENGTH_MAX];
    int                         m_distanceLength;
    KdasmAssemblerNode*         m_subnodes[2];
    intptr_t                    m_leafCount;
    KdasmU16*                   m_leaves;

    // Compile time data.
    KdasmAssemblerVirtualPage*  m_virtualPage;
    KdasmAssemblerPageTempData* m_pageTempData;
    KdasmAssemblerNodeTempData* m_nodeTempData;
    intptr_t                    m_compareToId;
};

// ----------------------------------------------------------------------------
// A virtual page is a KdasmAssemblerNode container that represents a page that
// those nodes are assigned to.  A virtual page may be reassigned to different
// physical pages.  Very large leaf nodes may span multiple physical pages, with
// other nodes packed in at the head.

class KdasmAssemblerVirtualPage
{
public:
    KdasmAssemblerVirtualPage( void );
    intptr_t PageStart( void ) const;
    void InsertNode( KdasmAssemblerNode* n );
    void RemoveNode( KdasmAssemblerNode* n );
    intptr_t GetNodeCount( void ) const                            { return (intptr_t)m_nodes.size(); }
    std::vector<KdasmAssemblerNode*>& GetNodes( void )             { return m_nodes; }
    intptr_t GetPhysicalPageStart( void ) const                    { return m_physicalPageStart; }
    void SetPhysicalPageStart( intptr_t n )                        { m_physicalPageStart = n; }
    intptr_t GetPhysicalPageCount( void ) const                    { return m_physicalPageCount; }
    void SetPhysicalPageCount( intptr_t n )                        { m_physicalPageCount = n; }
    void SetEncodingSize( intptr_t size )                          { m_encodingSize = size; }
    intptr_t GetEncodingSize( void ) const                         { return m_encodingSize; }

    void FindSuperpages( std::vector<KdasmAssemblerVirtualPage*>& pages );
    void AppendSuperpages( std::vector<KdasmAssemblerVirtualPage*>& pages, KdasmAssemblerNode** additionalNodes, size_t additionalNodesCount );
    void FindSubpages( std::vector<KdasmAssemblerVirtualPage*>& pages );

    static bool CompareByPhysicalPages( const KdasmAssemblerVirtualPage* a, const KdasmAssemblerVirtualPage* b );
    static bool CompareByPhysicalPagesReverse( const KdasmAssemblerVirtualPage* a, const KdasmAssemblerVirtualPage* b );
    static bool CompareByEncodingSize( const KdasmAssemblerVirtualPage* a, const KdasmAssemblerVirtualPage* b );

private:
    intptr_t                                m_physicalPageStart;
    intptr_t                                m_physicalPageCount;
    std::vector<KdasmAssemblerNode*>        m_nodes;
    intptr_t                                m_encodingSize;
};

// ----------------------------------------------------------------------------
// Allocates pages for KdasmAssemblerNodes.

class KdasmAssemblerPageAllocator
{
public:
    KdasmAssemblerPageAllocator( void );
    void SetPhysicalPageWords( int pageBits );
    intptr_t GetPhysicalPageWords( void );
    intptr_t GetPhysicalPagesRequired( KdasmAssemblerNode* n );
    KdasmAssemblerVirtualPage* Allocate( intptr_t physicalPageCount=1 );
    void Recycle( KdasmAssemblerVirtualPage* pg );
    void CompactAndFreePhysicalPages( void );
    std::vector<KdasmAssemblerVirtualPage*>& GetAllocatedPages( void );
    // Should never increase the number of words required to encode external page references:
    void CompactPhysicalPages( void );
    intptr_t AllocatedSize( void );
    void Clear( void );

private:
    intptr_t                                m_compactPhysicalPagesCounter;
    intptr_t                                m_physicalPageWords;
    intptr_t                                m_nextPhysicalPage;
    std::vector<KdasmAssemblerVirtualPage*> m_freeList;
    std::vector<KdasmAssemblerVirtualPage*> m_pageList;
};

// ----------------------------------------------------------------------------
// Encapsulates the traversal operations needed by the assembler.  Assigns
// new default pages as required.

class KdasmAssemblerNodeBreadthFirstQueue
{
public:
    void Init( KdasmAssemblerNode* root, KdasmAssemblerPageAllocator& pgAlloc );
    KdasmAssemblerNode* GetNext( KdasmAssemblerPageAllocator& pgAlloc );
    void PopNext( bool addSubnodes );
    void Prepend( KdasmAssemblerNode* n );
    bool Empty( void )                      { return m_nodes.empty(); }
    void Clear( void )                      { m_nodes.clear(); }

private:
    std::deque<KdasmAssemblerNode*> m_nodes; // front is next
};

// ----------------------------------------------------------------------------

// Used while encoding or testing a new encoding.
struct KdasmAssemblerEncodingIndices
{
    intptr_t                      m_encodingWordIndex;    // Location of external jump or internal encoding.
    intptr_t                      m_extraDataSize;        // May be very large in the case of leaves.
    intptr_t                      m_extraDataIndex;
    intptr_t                      m_internalJumpIndex;    // Some nodes have jump statements.
    intptr_t                      m_treeIndex;            // Tree address calculation offset.
};

// Persists only while a single page is being packed.
struct KdasmAssemblerPageTempData
{
    KdasmAssemblerNode*           m_node;
    bool                          m_isPageRoot;            // Owner is external to this page.
    bool                          m_isExternal;            // Encodes as a jump to an external node.
    KdasmAssemblerEncodingIndices m_indices;
    int                           m_validatedIndices;
};

// Persists across a single assembly job.
struct KdasmAssemblerNodeTempData
{
    KdasmAssemblerNode*           m_supernode;
    bool                          m_forceFarAddressing;
    KdasmAssemblerEncodingIndices m_internalIndices;        // Page the node is encoded in
    KdasmAssemblerEncodingIndices m_externalIndices;        // Page that references the encoding
};

// ----------------------------------------------------------------------------
// Encodes a KdasmAssemblerVirtualPage into a page of KdasmEncoding if possible.

class KdasmAssemblerPagePacker
{
public:
    KdasmAssemblerPagePacker( void );
    void SetPageSize( int pageBits );
    bool Pack( KdasmAssemblerVirtualPage* p, bool saveIfOk, KdasmAssemblerNode** additionalNodes=NULL, size_t additionalNodesCount=0 );
    std::vector<KdasmEncoding>& Encode( KdasmAssemblerVirtualPage* p );
    void Clear( void );
    static void ClearEncodingIndices( KdasmAssemblerEncodingIndices* indices );

private:
    struct PackingStats
    {
        intptr_t m_encodingWords;
        intptr_t m_internalJumps;
    };

    void BuildNodeTempData( KdasmAssemblerNode** additionalNodes, size_t additionalNodesCount );
    void ClearNodeTempData( void );
    bool PackExtraData( void );
    bool PackEncodingWords( void );
    bool EvaluatePacking( intptr_t treeRoot, intptr_t index, intptr_t treeIndex, PackingStats& bestFit );
    void EvaluateSubnodePacking( KdasmAssemblerPageTempData* t, intptr_t index, intptr_t treeIndex, PackingStats& stats );
    void CommitSubtreePacking( KdasmAssemblerPageTempData* t, intptr_t index, intptr_t treeIndex );
    void WriteEncoding( void );
    intptr_t CalculateNodeExtraDataSize( KdasmAssemblerPageTempData* t );
    void CalculateNodeExtraData( KdasmAssemblerPageTempData* t );
    void CalculateInternalJumpEncoding( KdasmAssemblerPageTempData* t );
    void CalculateNodeEncoding( KdasmAssemblerPageTempData* t );
    void SaveEncodingIndices( void );
    void UseSavedEncodingIndices( void );
    intptr_t CalculateNodeFarOffset( KdasmAssemblerPageTempData* t );
    int CalculateWordsRequired( intptr_t x );
    bool ValidateAllocationMap( void );
    bool ValidateNodeEncoding( KdasmAssemblerPageTempData* t );

    int                                      m_pageWordBits;
    intptr_t                                 m_currentPageWords;
    intptr_t                                 m_extraDataStart;
    KdasmAssemblerVirtualPage*               m_virtualPage;
    std::vector<KdasmAssemblerPageTempData*> m_allocationMap;
    std::vector<KdasmEncoding>               m_encoding;
    std::vector<KdasmAssemblerPageTempData>  m_pageTempData;
    std::vector<KdasmAssemblerPageTempData*> m_treeRootsRemaining;
    intptr_t                                 m_bestFitTreeRoot;
    intptr_t                                 m_bestFitPageIndex;
    intptr_t                                 m_bestFitTreeIndex;
};

// ----------------------------------------------------------------------------
// Converts a KdasmAssemblerNode tree into a cache aligned KdasmEncoding array.

class KdasmAssembler
{
public:
    typedef void (*ActivityCallback)( void* data );

    KdasmAssembler( void );
    void SetActivityCallback( ActivityCallback callback, void* data=NULL, int activityFrequency=10000 );
    void Assemble( KdasmAssemblerNode* root, KdasmEncodingHeader::PageBits pageBits, std::vector<KdasmEncoding>& encoding );

private:
    enum {
        MAX_PAGE_MERGE_SCAN_DISTANCE = 3
    };

    typedef std::vector<std::vector<KdasmAssemblerVirtualPage*> > PagesBySize;

    void TickActivity( void );
    void PackNextPage( void );
    void SubpageMerge( void );
    void BinPack( void );
    void BuildPagesBySize( intptr_t pageWords );
    intptr_t FindClosestPhysicalPage( KdasmAssemblerVirtualPage* bin, std::vector<KdasmAssemblerVirtualPage*>& pages );
    bool TryBinPack( KdasmAssemblerVirtualPage* bin, KdasmAssemblerVirtualPage* pg );
    void Encode( KdasmAssemblerNode* root, KdasmEncodingHeader::PageBits pageBits, std::vector<KdasmEncoding>& result );
    void Clear( void );

    ActivityCallback                        m_activityCallback;
    void*                                   m_activityData;
    int                                     m_activityFrequency;
    int                                     m_activityCounter;

    KdasmAssemblerPageAllocator             m_pageAllocator;
    KdasmAssemblerNodeBreadthFirstQueue     m_globalQueue;
    KdasmAssemblerNodeBreadthFirstQueue     m_pageQueue;
    KdasmAssemblerPagePacker                m_pagePacker;
    std::vector<KdasmAssemblerVirtualPage*> m_superpages;
    std::vector<KdasmAssemblerVirtualPage*> m_failingPageSuperpages;
    PagesBySize                             m_pagesBySize;
};

// ----------------------------------------------------------------------------
// Allows for validation and forward conversion of data structure.

class KdasmDisassembler
{
public:
    struct EncodingStats
    {
        intptr_t m_totalEncodingData;
        intptr_t m_paddingData;
        intptr_t m_headerData;
        intptr_t m_cuttingPlaneNodeCount;
        intptr_t m_cuttingPlaneExtraData;
        intptr_t m_leafHeaderCount;
        intptr_t m_leafblockData;
        intptr_t m_leafNodeCount;
        intptr_t m_leafNodeFarCount;
        intptr_t m_leafNodeFarExtraData;
        intptr_t m_jumpNodeCount;
        intptr_t m_jumpNodeFarCount;
        intptr_t m_jumpNodeFarExtraData;
        intptr_t m_totalCacheMissesForEachLeafNode;
    };

    // Returns null on failure.  Optionally checks against compareTo in order to
    // identify the nodeId in case of failure. 
    KdasmAssemblerNode* Disassemble( KdasmEncoding* encodingRoot, KdasmAssemblerNode* compareTo=NULL );

    void CalculateStats( KdasmEncoding* encodingRoot, intptr_t encodingSize, EncodingStats& stats );

private:
    KdasmAssemblerNode* DisassembleEncoding( KdasmEncoding* encoding, intptr_t treeIndex, KdasmAssemblerNode* compareTo );
    KdasmAssemblerNode* DisassembleLeavesFar( KdasmEncoding* encoding, KdasmAssemblerNode* compareTo );
    KdasmAssemblerNode* DisassembleLeaves( KdasmEncoding* encoding, intptr_t leafCount, KdasmAssemblerNode* compareTo );

    void CalculateStatsEncoding( KdasmEncoding* encoding, intptr_t treeIndex, EncodingStats& stats );
    void CalculateStatsLeavesFar( KdasmEncoding* encoding, EncodingStats& stats );
    void CalculateStatsLeaves( KdasmEncoding* encoding, intptr_t leafCount, EncodingStats& stats );

    bool IsCacheMiss( KdasmEncoding* node, KdasmEncoding* subnode );

    int            m_distanceLength;
    intptr_t       m_compareToFailId;
    intptr_t       m_pageAddressMask;
    KdasmEncoding* m_encodingRoot;
    intptr_t       m_cacheMissDepth;
};

#endif // KDASM_ASSEMBLER_H
