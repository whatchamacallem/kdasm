// Copyright (c) 2012 Adrian Johnston.  All rights reserved.
// See Copyright Notice in kdasm.h

#include <algorithm>
#include "kdasm_assembler.h"

// Enables an excessive level of validation that is useful during development
// to catch problems close to where they occur.
//#define KDASM_PARANOIA

// ----------------------------------------------------------------------------

// This is unlikely to be what you want.
bool KdasmAssertFail( const char* expression, const char* file, int line )
{
	printf( "kdasm assert fail: %s %d : %s\n", file, line, expression );
	abort();
	return true;
}

// ----------------------------------------------------------------------------
// KdasmAssemblerNode 

void KdasmAssemblerNode::Clear( void )
{
	if( m_subnodes[0] )
	{
		delete m_subnodes[0];
		m_subnodes[0] = NULL;
	}
	if( m_subnodes[1] )
	{
		delete m_subnodes[1];
		m_subnodes[1] = NULL;
	}
	if( m_leaves )
	{
		delete[] m_leaves;
		m_leaves = NULL;
	}
}

void KdasmAssemblerNode::AddSubnodes( unsigned short distance, unsigned short normal, KdasmAssemblerNode* less, KdasmAssemblerNode* greater )
{
	AddSubnodes( &distance, 1, normal, less, greater );
}

void KdasmAssemblerNode::AddSubnodes( intptr_t distance, int distanceLength, unsigned short normal, KdasmAssemblerNode* less, KdasmAssemblerNode* greater )
{
	KdasmAssert( "Distance length max exceeded", distanceLength < KdasmEncodingHeader::DISTANCE_LENGTH_MAX );

	for( int i=distanceLength; i--; /**/ )
	{
		m_distance[i] = (unsigned short)distance;
		distance >>= 16;
	}

	AddSubnodes( m_distance, distanceLength, normal, less, greater );
}

void KdasmAssemblerNode::AddSubnodes( unsigned short* distance, int distanceLength, unsigned short normal, KdasmAssemblerNode* less, KdasmAssemblerNode* greater )
{
	KdasmAssert( "Unsupported distance length", distanceLength > 0 && distanceLength <= KdasmEncodingHeader::DISTANCE_LENGTH_MAX );
	KdasmAssert( "Distance out of range", distance[0] <= (unsigned short)KdasmEncoding::DISTANCE_IMMEDIATE_MAX );
	KdasmAssert( "Distance has trailing bits that will be lost", distanceLength != 1 || ( distance[0] & ~(unsigned short)KdasmEncoding::DISTANCE_IMMEDIATE_MASK ) == 0 );
	KdasmAssert( "First word of distance is too large", distanceLength == 1 || ( distance[0] & ~(unsigned short)KdasmEncoding::DISTANCE_PREFIX_MAX ) == 0 );
	KdasmAssert( "Cutting normal must be 0, 1 or 2", normal < KdasmEncoding::NORMAL_OPCODE );

	Clear();
	if( m_distance != distance )
	{
		for( int i=0; i < distanceLength; ++i )
		{
			m_distance[i] = distance[i];
		}
	}
	m_distanceLength = distanceLength;
	m_normal = normal;
	m_subnodes[0] = less;
	m_subnodes[1] = greater;
}

void KdasmAssemblerNode::AddLeaves( intptr_t leafCount, unsigned short* leaves )
{
	// Direct support for larger blocks of leaf data would add runtime checks.
	// Count will be returned as KdasmEncoding::LEAF_COUNT_OVERFLOW if it is larger
	// than that and the user would have to have embedded a header in the leaf
	// data to determine the real length.
	KdasmAssert( "Leaf data block will overflow.  Will require a header.", leafCount < KdasmEncoding::LEAF_COUNT_OVERFLOW );

	Clear();
	m_leafCount = leafCount;
	m_leaves = leaves;
}

bool KdasmAssemblerNode::Equals( const KdasmAssemblerNode& n, bool checkSubnodes ) const
{
	if( HasSubnodes() || n.HasSubnodes() )
	{
		if( m_normal != n.m_normal )
		{
			return false;
		}
		if( m_distanceLength != n.m_distanceLength )
		{
			return false;
		}
		for( unsigned short i=0; i < m_distanceLength; ++i )
		{
			if( m_distance[i] != n.m_distance[i] )
			{
				return false;
			}
		}
		if( !checkSubnodes )
		{
			return true;
		}
		if(    ( m_subnodes[0] != NULL ) != ( n.m_subnodes[0] != NULL )
			|| ( m_subnodes[1] != NULL ) != ( n.m_subnodes[1] != NULL ) )
		{
			return false; // mismatched subnode pointers.
		}
		return ( m_subnodes[0] == NULL || m_subnodes[0]->Equals( *n.m_subnodes[0], checkSubnodes ) )
			&& ( m_subnodes[1] == NULL || m_subnodes[1]->Equals( *n.m_subnodes[1], checkSubnodes ) );
	}
	if( m_leafCount != n.m_leafCount )
	{
		return false;
	}
	return ::memcmp( m_leaves, n.m_leaves, m_leafCount * sizeof( unsigned short ) ) == 0;
}

bool KdasmAssemblerNode::TrimEmpty( void )
{
	if( HasSubnodes() )
	{
		for( intptr_t i=0; i < 2; ++i )
		{
			if( m_subnodes[i] && m_subnodes[i]->TrimEmpty() )
			{
				delete m_subnodes[i];
				m_subnodes[i] = NULL;
			}
		}
		return !HasSubnodes();
	}

	return m_leafCount == 0;
}

KdasmAssemblerVirtualPage* KdasmAssemblerNode::GetVirtualPage( void )
{
	// Confirm that this is indeed a back pointer.
#ifdef KDASM_PARANOIA
	KdasmAssertDebug( !m_virtualPage || ( std::find( m_virtualPage->GetNodes().begin(), m_virtualPage->GetNodes().end(), this ) != m_virtualPage->GetNodes().end() ) );
#endif
	return m_virtualPage;
}

void KdasmAssemblerNode::SetVirtualPage( KdasmAssemblerVirtualPage* pg )
{
	m_virtualPage = pg;
}

intptr_t KdasmAssemblerNode::GetPhysicalPageStart( void )
{
	return GetVirtualPage()->GetPhysicalPageStart();
}

KdasmAssemblerPageTempData* KdasmAssemblerNode::GetPageTemp( void )
{
	KdasmAssertDebug( !m_pageTempData || m_pageTempData->m_node == this );
	return m_pageTempData;
}

intptr_t KdasmAssemblerNode::AssemblePrepare( KdasmAssemblerNode* supernode, intptr_t nextCompareToId )
{
	m_compareToId = nextCompareToId;
	m_virtualPage = NULL;
	m_pageTempData = NULL;

	m_nodeTempData = new KdasmAssemblerNodeTempData;
	::memset( m_nodeTempData, 0, sizeof *m_nodeTempData );
	m_nodeTempData->m_supernode = supernode;
	m_nodeTempData->m_forceFarAddressing = false;
	KdasmAssemblerPagePacker::ClearEncodingIndices( &m_nodeTempData->m_internalIndices );
	KdasmAssemblerPagePacker::ClearEncodingIndices( &m_nodeTempData->m_externalIndices );

	for( intptr_t i=0; i < 2; ++i )
	{
		if( m_subnodes[i] )
		{
			// Distance length should remain constant as it is not explicitly encoded.
			KdasmAssert( "Distance length cannot vary within the tree", \
				!m_subnodes[i]->HasSubnodes() || m_subnodes[i]->GetDistanceLength() == GetDistanceLength() );
			nextCompareToId = m_subnodes[i]->AssemblePrepare( this, nextCompareToId + 1 );
		}
	}
	return nextCompareToId;
}

void KdasmAssemblerNode::AssembleFinish( void )
{
	KdasmAssertDebug( m_pageTempData == NULL );

	m_virtualPage = NULL;
	if( m_nodeTempData )
	{
		delete m_nodeTempData;
		m_nodeTempData = NULL;
	}

	for( intptr_t i=0; i < 2; ++i )
	{
		if( m_subnodes[i] )
		{
			m_subnodes[i]->AssembleFinish();
		}
	}
}

// ----------------------------------------------------------------------------
// KdasmAssemblerVirtualPage

KdasmAssemblerVirtualPage::KdasmAssemblerVirtualPage( void )
{
	m_physicalPageStart = -1;
	m_physicalPageCount = 0;
	m_encodingSize = 0;
}

intptr_t KdasmAssemblerVirtualPage::PageStart( void ) const
{
	KdasmAssertDebug( m_physicalPageStart != 0 || m_nodes[0]->GetNodeTemp()->m_supernode == NULL );
	return ( m_physicalPageStart == 0 ) ? KdasmEncodingHeader::HEADER_LENGTH : 0;
}

void KdasmAssemblerVirtualPage::InsertNode( KdasmAssemblerNode* n )
{
	KdasmAssertDebug( n->GetVirtualPage() == NULL );
	n->SetVirtualPage( this );
	m_nodes.push_back( n );
}

void KdasmAssemblerVirtualPage::RemoveNode( KdasmAssemblerNode* n )
{
	KdasmAssertDebug( n->GetVirtualPage() == this );

	std::vector<KdasmAssemblerNode*>::reverse_iterator i = std::find( m_nodes.rbegin(), m_nodes.rend(), n );
	KdasmAssertDebug( i != m_nodes.rend() );
	m_nodes.erase( i.base() - 1 );

	n->SetVirtualPage( NULL );
}

void KdasmAssemblerVirtualPage::BuildPageHierarchy( void )
{
	m_superPages.clear();
	m_subPages.clear();

	for( size_t i=0; i < m_nodes.size(); ++i )
	{
		KdasmAssemblerNode* n = m_nodes[i]->GetNodeTemp()->m_supernode;
		if( n && n->GetVirtualPage() != this )
		{
			m_superPages.push_back( n->GetVirtualPage() );
		}

		for( intptr_t j=0; j < 2; ++j )
		{
			KdasmAssemblerNode* sn = m_nodes[i]->GetSubnode( j );
			if( sn && sn->GetVirtualPage() != this )
			{
				if( std::find( m_subPages.begin(), m_subPages.end(), sn->GetVirtualPage() ) == m_subPages.end() )
				{
					m_subPages.push_back( sn->GetVirtualPage() );
				}
			}
		}
	}
}

bool KdasmAssemblerVirtualPage::ValidatePageHierarchy( void )
{
	for( size_t i=0; i < m_superPages.size(); ++i )
	{
		if( std::find( m_superPages[i]->m_subPages.begin(), m_superPages[i]->m_subPages.end(), this ) == m_superPages[i]->m_subPages.end() )
		{
			return false;
		}
	}

	for( size_t i=0; i < m_subPages.size(); ++i )
	{
		if( std::find( m_subPages[i]->m_superPages.begin(), m_subPages[i]->m_superPages.end(), this ) == m_subPages[i]->m_superPages.end() )
		{
			return false;
		}
	}

	return m_nodes.size() == 0 || m_physicalPageStart == 0 || !m_superPages.empty();
}

bool KdasmAssemblerVirtualPage::CompareByPhysicalPages( const KdasmAssemblerVirtualPage* a, const KdasmAssemblerVirtualPage* b ) 
{
	// Use unsigned size_t to place -1 after other values.
	return (size_t)a->m_physicalPageStart < (size_t)b->m_physicalPageStart;
}

bool KdasmAssemblerVirtualPage::CompareByPhysicalPagesReverse( const KdasmAssemblerVirtualPage* a, const KdasmAssemblerVirtualPage* b ) 
{
	return a->m_physicalPageStart > b->m_physicalPageStart;
}

bool KdasmAssemblerVirtualPage::CompareByEncodingSize( const KdasmAssemblerVirtualPage* a, const KdasmAssemblerVirtualPage* b ) 
{
	return a->m_encodingSize < b->m_encodingSize;
}

// ----------------------------------------------------------------------------
// KdasmAssemblerPageAllocator

KdasmAssemblerPageAllocator::KdasmAssemblerPageAllocator( void )
{
	Clear();
}

void KdasmAssemblerPageAllocator::SetPhysicalPageWords( int pageBits )
{
	m_physicalPageWords = (intptr_t)1 << (pageBits-1);
}

intptr_t KdasmAssemblerPageAllocator::GetPhysicalPageWords( void )
{
	return m_physicalPageWords;
}

intptr_t KdasmAssemblerPageAllocator::GetPhysicalPagesRequired( KdasmAssemblerNode* n )
{
	if( n->HasSubnodes() )
	{
		return 1;
	}

	// The leaf block prefix word is accounted for.
	intptr_t header = ( n->GetNodeTemp()->m_supernode == NULL ) ? KdasmEncodingHeader::HEADER_LENGTH : 0;
	return ( n->GetLeafCount() + header + m_physicalPageWords ) / m_physicalPageWords;
}

KdasmAssemblerVirtualPage* KdasmAssemblerPageAllocator::Allocate( intptr_t physicalPageCount )
{
	KdasmAssertDebug( physicalPageCount > 0 );
	KdasmAssertDebug( m_physicalPageWords != 0 );

	KdasmAssemblerVirtualPage* result = NULL;
	if( !m_freeList.empty() )
	{
		result = m_freeList.back();
		m_freeList.pop_back();

		if( physicalPageCount != result->GetPhysicalPageCount() )
		{
			// Run the physical page compactor before there is likely enough wasted
			// space to overflow immediate mode addressing.
			m_compactPhysicalPagesCounter += result->GetPhysicalPageCount();
			if( m_compactPhysicalPagesCounter > (KdasmEncoding::IMMEDIATE_MAX/2) )
			{
				m_compactPhysicalPagesCounter = 0;
				CompactPhysicalPages();
			}

			result->SetPhysicalPageStart( m_nextPhysicalPage );
			result->SetPhysicalPageCount( physicalPageCount );
			m_nextPhysicalPage += physicalPageCount;
		}
	}
	else
	{
		result = new KdasmAssemblerVirtualPage;
		m_pageList.push_back( result );

		result->SetPhysicalPageStart( m_nextPhysicalPage );
		result->SetPhysicalPageCount( physicalPageCount );
		m_nextPhysicalPage += physicalPageCount;
	}

	KdasmAssertDebug( result->GetPhysicalPageStart() >= 0 && result->GetPhysicalPageCount() >= 0 );
	return result;
}

void KdasmAssemblerPageAllocator::Recycle( KdasmAssemblerVirtualPage* pg )
{
	KdasmAssertDebug( pg && pg->NodeCount() == 0 );
#ifdef KDASM_PARANOIA
	KdasmAssertDebug( std::find( m_freeList.begin(), m_freeList.end(), pg ) == m_freeList.end() );
#endif

	m_freeList.push_back( pg );
}

void KdasmAssemblerPageAllocator::CompactAndFreePhysicalPages( void )
{
	CompactPhysicalPages();

	while( !m_freeList.empty() )
	{
		KdasmAssemblerVirtualPage* pg = m_freeList.back();
		m_freeList.pop_back();

		std::vector<KdasmAssemblerVirtualPage*>::reverse_iterator i = std::find( m_pageList.rbegin(), m_pageList.rend(), pg );
		KdasmAssertDebug( i != m_pageList.rend() );
		m_pageList.erase( i.base() - 1 );
		delete pg;
	}
}

std::vector<KdasmAssemblerVirtualPage*>& KdasmAssemblerPageAllocator::GetAllocatedPages( void )
{
	return m_pageList;
}

// Should never increase the number of bits required to encode external page references.
void KdasmAssemblerPageAllocator::CompactPhysicalPages( void )
{
	for( std::vector<KdasmAssemblerVirtualPage*>::iterator it=m_pageList.begin(); it != m_pageList.end(); ++it )
	{
		KdasmAssemblerVirtualPage* pg = *it;
		if( pg->NodeCount() == 0 )
		{
			pg->SetPhysicalPageStart( -1 );
			pg->SetPhysicalPageCount( 0 );
		}
	}

	std::sort( m_pageList.begin(), m_pageList.end(), KdasmAssemblerVirtualPage::CompareByPhysicalPages );

	intptr_t currentPhysicalPageCount = 0;
	for( std::vector<KdasmAssemblerVirtualPage*>::iterator it=m_pageList.begin(); it != m_pageList.end(); ++it )
	{
		KdasmAssemblerVirtualPage* pg = *it;
		if( pg->NodeCount() != 0 )
		{
			pg->SetPhysicalPageStart( currentPhysicalPageCount );
			currentPhysicalPageCount += pg->GetPhysicalPageCount();
		}
	}

	m_nextPhysicalPage = currentPhysicalPageCount;
}

intptr_t KdasmAssemblerPageAllocator::AllocatedSize( void )
{
	intptr_t currentPhysicalPageCount = 0;

	for( std::vector<KdasmAssemblerVirtualPage*>::iterator it=m_pageList.begin(); it != m_pageList.end(); ++it )
	{
		KdasmAssemblerVirtualPage* pg = *it;
		currentPhysicalPageCount += pg->GetPhysicalPageCount();
	}
	
	return currentPhysicalPageCount * m_physicalPageWords;
}

void KdasmAssemblerPageAllocator::Clear( void )
{
	m_compactPhysicalPagesCounter = 0;
	m_nextPhysicalPage = 0;
	m_physicalPageWords = 0;
	m_freeList.clear();
	while( !m_pageList.empty() )
	{
		KdasmAssemblerVirtualPage* pg = m_pageList.back();
		m_pageList.pop_back();
		delete pg;
	}
}

// ----------------------------------------------------------------------------
// KdasmAssemblerNodeBreadthFirstQueue

void KdasmAssemblerNodeBreadthFirstQueue::Init( KdasmAssemblerNode* root, KdasmAssemblerPageAllocator& pgAlloc )
{
	KdasmAssertDebug( m_nodes.empty() && root != NULL );
	if( !root->GetVirtualPage() )
	{
		ptrdiff_t pagesRequired = pgAlloc.GetPhysicalPagesRequired( root );
		pgAlloc.Allocate( pagesRequired )->InsertNode( root );
		root->GetNodeTemp()->m_forceFarAddressing = root->GetLeafCount() > KdasmEncoding::LENGTH_MAX;
	}
	m_nodes.push_back( root );
}

KdasmAssemblerNode* KdasmAssemblerNodeBreadthFirstQueue::GetNext( KdasmAssemblerPageAllocator& pgAlloc )
{
	if( m_nodes.empty() )
	{
		return NULL;
	}

	KdasmAssemblerNode* n = m_nodes.front();
	KdasmAssertDebug( n && n->GetVirtualPage() );

	for( intptr_t i=0; i < 2; ++i )
	{
		if( n->GetSubnode( i ) )
		{
			KdasmAssemblerNode* sn = n->GetSubnode( i );
			if( !sn->GetVirtualPage() )
			{
				ptrdiff_t pagesRequired = pgAlloc.GetPhysicalPagesRequired( sn );
				pgAlloc.Allocate( pagesRequired )->InsertNode( sn );
				sn->GetNodeTemp()->m_forceFarAddressing = sn->GetLeafCount() > KdasmEncoding::LENGTH_MAX;
			}
		}
	}
	return n;
}

void KdasmAssemblerNodeBreadthFirstQueue::PopNext( bool addSubnodes )
{
	KdasmAssertDebug( !m_nodes.empty() );
	KdasmAssemblerNode* n = m_nodes.front();
	m_nodes.pop_front();
	KdasmAssertDebug( n && n->GetVirtualPage() );

	if( addSubnodes )
	{
		for( intptr_t i=0; i < 2; ++i )
		{
			if( n->GetSubnode( i ) )
			{
				KdasmAssemblerNode* sn = n->GetSubnode( i );
				m_nodes.push_back( sn );
			}
		}
	}
}

void KdasmAssemblerNodeBreadthFirstQueue::Append( KdasmAssemblerNode* n )
{
	m_nodes.push_back( n );
}

// ----------------------------------------------------------------------------
// KdasmAssemblerPagePacker

KdasmAssemblerPagePacker::KdasmAssemblerPagePacker( void )
{
	Clear();
}

void KdasmAssemblerPagePacker::SetPageSize( int pageBits )
{
	// Words are two bytes wide.
	m_pageWordBits = pageBits - 1;
}

bool KdasmAssemblerPagePacker::Pack( KdasmAssemblerVirtualPage* p, bool saveIfOk, intptr_t padding )
{
	KdasmAssertDebug( p->GetPhysicalPageStart() >= 0 && p->GetPhysicalPageCount() >= 0 );

	m_virtualPage = p;

	// Multi-page blocks of leaf data could be encoded more optimally.  Although
	// this is an unlikely bottleneck and would add complexity.
	m_currentPageWords = (intptr_t)1 << m_pageWordBits;
	m_currentPageWords *= p->GetPhysicalPageCount();

	m_allocationMap.assign( m_currentPageWords, NULL );

	BuildNodeTempData();

	bool packOk = PackExtraData( padding );
	if( packOk )
	{
		packOk = PackEncodingWords();
		if( packOk )
		{
#ifdef KDASM_PARANOIA
			KdasmAssertDebug( ValidateAllocationMap() );
#endif
			if( saveIfOk )
			{
				SaveEncodingIndices();

				if( p->GetPhysicalPageStart() == 0 )
				{
					p->SetEncodingSize( m_currentPageWords - std::count( m_allocationMap.begin(),
						m_allocationMap.end(), (KdasmAssemblerPageTempData*)NULL ) );
				}
				else
				{
					p->SetEncodingSize( m_currentPageWords - std::count( m_allocationMap.begin(),
						m_allocationMap.begin() + m_extraDataStart, (KdasmAssemblerPageTempData*)NULL ) );
				}
			}
		}
	}

	ClearNodeTempData();
	return packOk;
}

std::vector<KdasmEncoding>& KdasmAssemblerPagePacker::Encode( KdasmAssemblerVirtualPage* p )
{
	m_virtualPage = p;

	m_currentPageWords = (intptr_t)1 << m_pageWordBits;
	m_currentPageWords *= p->GetPhysicalPageCount();

#ifdef KDASM_PARANOIA
	m_allocationMap.assign( m_currentPageWords, NULL );
#endif

	BuildNodeTempData();

	UseSavedEncodingIndices();

#ifdef KDASM_PARANOIA
	KdasmAssertDebug( ValidateAllocationMap() );
#endif

	KdasmEncoding t;
	t.SetRaw( (unsigned short)KdasmEncoding::PAD_VALUE );
	m_encoding.assign( m_currentPageWords, t );

	WriteEncoding();

	ClearNodeTempData();

	return m_encoding;
}

void KdasmAssemblerPagePacker::Clear( void )
{
	m_pageWordBits = 0;
	m_currentPageWords = 0;
	m_virtualPage = NULL;
	m_allocationMap.clear();
	m_encoding.clear();
	m_pageTempData.clear();
	m_bestFitTreeRoot = -1;
	m_bestFitPageIndex = -1;
	m_bestFitTreeIndex = -1;
}

void KdasmAssemblerPagePacker::ClearEncodingIndices( KdasmAssemblerEncodingIndices* indices )
{
	indices->m_encodingWordIndex = -1;
	indices->m_extraDataSize = 0;
	indices->m_extraDataIndex = -1;
	indices->m_internalJumpIndex = -1;
	indices->m_treeIndex = -1;
}

void KdasmAssemblerPagePacker::BuildNodeTempData( void )
{
	std::vector<KdasmAssemblerNode*>& nodes = m_virtualPage->GetNodes();

	m_pageTempData.clear();
	m_pageTempData.reserve( nodes.size() * 3 );  // storage for external nodes as well.

	// Add entries for nodes within the page.
	KdasmAssemblerPageTempData t;
	::memset( &t, 0, sizeof t );
	t.m_isPageRoot = true;
	ClearEncodingIndices( &t.m_indices );

	for( size_t i=0; i < nodes.size(); ++i )
	{
		KdasmAssemblerNode* n = nodes[i];
		t.m_node = n;
		m_pageTempData.push_back( t );
		n->SetPageTemp( &m_pageTempData.back() );
	}

	// Now add the entries for external nodes.
	t.m_isExternal = true;
	t.m_isPageRoot = false;

	for( size_t i=0; i < nodes.size(); ++i )
	{
		KdasmAssemblerNode* n = nodes[i];
		for( int j=0; j < 2; ++j )
		{
			if( n->GetSubnode( j ) )
			{
				KdasmAssemblerNode* sn = n->GetSubnode( j );
				if( sn->GetVirtualPage() != m_virtualPage )
				{
					KdasmAssertDebug( !sn->GetPageTemp() );
					t.m_node = sn;
					m_pageTempData.push_back( t );
					sn->SetPageTemp( &m_pageTempData.back() );
				}
				else
				{
					KdasmAssemblerPageTempData* p = sn->GetPageTemp();
					KdasmAssertDebug( !p->m_isExternal );
					p->m_isPageRoot = sn->GetNodeTemp()->m_forceFarAddressing;
				}
			}
		}
	}
}

void KdasmAssemblerPagePacker::ClearNodeTempData( void )
{
	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		t->m_node->SetPageTemp( NULL );
	}
}

bool KdasmAssemblerPagePacker::PackExtraData( intptr_t padding )
{
	// Special case for leaves at root.
	if( m_virtualPage->GetPhysicalPageStart() == 0 && m_pageTempData.size() == 1 )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[0];
		t->m_indices.m_extraDataIndex = m_virtualPage->PageStart();
		t->m_indices.m_extraDataSize = CalculateNodeExtraDataSize( t );

		for( intptr_t j=0; j < t->m_indices.m_extraDataSize; ++j )
		{
			m_allocationMap[t->m_indices.m_extraDataIndex + j] = t;
		}
		m_extraDataStart = m_virtualPage->PageStart();
		return true;
	}

	intptr_t extraDataIndex = m_currentPageWords - padding;

	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		intptr_t extraDataSize = CalculateNodeExtraDataSize( t );
		if( extraDataSize == 0 )
		{
			continue;
		}

		extraDataIndex -= extraDataSize;
		if( extraDataIndex < 0 )
		{
			return false;
		}

		t->m_indices.m_extraDataIndex = extraDataIndex;
		t->m_indices.m_extraDataSize = extraDataSize;

		for( intptr_t j=0; j < extraDataSize; ++j )
		{
			m_allocationMap[extraDataIndex + j] = t;
		}
	}

	m_extraDataStart = extraDataIndex;
	return true;
}

bool KdasmAssemblerPagePacker::PackEncodingWords( void )
{
	// The initial tree roots are the non-leaf page roots.  References by OPCODE_LEAVES_FAR have no encoding.
	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		if( t->m_isPageRoot && t->m_node->HasSubnodes() )
		{
			t->m_indices.m_treeIndex = 0;

			m_treeRootsRemaining.push_back( t );
		}
	}

	while( !m_treeRootsRemaining.empty() )
	{
		PackingStats bestFit;
		bestFit.m_encodingWords = 0;
		bestFit.m_internalJumps = 1; // No point encoding a single jump.  Just wastes space.

		m_bestFitTreeRoot = -1;
		m_bestFitPageIndex = -1;
		m_bestFitTreeIndex = -1;

		for( intptr_t i=0; i < (intptr_t)m_treeRootsRemaining.size(); ++i )
		{
			for( intptr_t index=m_virtualPage->PageStart(); index < m_extraDataStart; ++index )
			{
				if( m_allocationMap[index] != NULL )
				{
					continue;
				}

				if( m_treeRootsRemaining[i]->m_indices.m_treeIndex != 0 )
				{
					intptr_t treeIndexEnd = m_extraDataStart - index;
					if( treeIndexEnd > (KdasmEncoding::TREE_INDEX_MAX+1) )
					{
						treeIndexEnd = (KdasmEncoding::TREE_INDEX_MAX+1);
					}
					for( intptr_t treeIndex=0; treeIndex < treeIndexEnd; ++treeIndex )
					{
						if( EvaluatePacking( i, index, treeIndex, bestFit ) )
						{
							break;
						}
					}
				}
				else
				{
					EvaluatePacking( i, index, 0, bestFit );
				}
			}
		}

		if( m_bestFitTreeRoot == -1 )
		{
			m_treeRootsRemaining.clear();
			return false;
		}

		// Pack the best fit tree root at the selected index and tree index.
		KdasmAssemblerPageTempData* t = m_treeRootsRemaining[m_bestFitTreeRoot];
		m_treeRootsRemaining.erase( m_treeRootsRemaining.begin() + m_bestFitTreeRoot );
		CommitSubtreePacking( t, m_bestFitPageIndex, m_bestFitTreeIndex );
	}

	return true;
}

// Returns true if no improvement is possible.
bool KdasmAssemblerPagePacker::EvaluatePacking( intptr_t treeRoot, intptr_t index, intptr_t treeIndex, PackingStats& bestFit )
{
	KdasmAssemblerPageTempData* t = m_treeRootsRemaining[treeRoot];

	PackingStats stats;
	stats.m_encodingWords = 0;
	stats.m_internalJumps = 0;

	EvaluateSubnodePacking( t, index, treeIndex, stats );

	if( bestFit.m_encodingWords < stats.m_encodingWords 
		|| ( bestFit.m_encodingWords == stats.m_encodingWords && bestFit.m_internalJumps < stats.m_internalJumps ) )
	{
		bestFit = stats;
		m_bestFitTreeRoot = treeRoot;
		m_bestFitPageIndex = index;
		m_bestFitTreeIndex = treeIndex;
	}

	return stats.m_internalJumps == 0;
}

void KdasmAssemblerPagePacker::EvaluateSubnodePacking( KdasmAssemblerPageTempData* t, intptr_t index, intptr_t treeIndex, KdasmAssemblerPagePacker::PackingStats& stats )
{
	KdasmAssertDebug( t->m_indices.m_encodingWordIndex == -1 );
	KdasmAssertDebug( m_allocationMap[index] == NULL );

	KdasmAssemblerNode* n = t->m_node;
	if( t->m_isExternal || !n->HasSubnodes() )
	{
		++stats.m_encodingWords;
		return;
	}

	PackingStats subStats;
	subStats.m_encodingWords = 1;
	subStats.m_internalJumps = 0;

	for( intptr_t j=0; j < 2; ++j )
	{
		KdasmAssemblerNode* sn = n->GetSubnode( j );
		if( sn )
		{
			// Subnodes are at 2n+1 and 2n+2.  However index may be offset from treeIndex.
			intptr_t subIndex = index + treeIndex + 1 + j;
			if( subIndex >= m_extraDataStart || m_allocationMap[subIndex] != NULL )
			{
				++stats.m_internalJumps;
				return;
			}
			else
			{
				EvaluateSubnodePacking( sn->GetPageTemp(), subIndex, treeIndex * 2 + 1 + j, subStats );
			}
		}
	}

	stats.m_encodingWords += subStats.m_encodingWords;
	stats.m_internalJumps += subStats.m_internalJumps;
}

void KdasmAssemblerPagePacker::CommitSubtreePacking( KdasmAssemblerPageTempData* t, intptr_t index, intptr_t treeIndex )
{
	KdasmAssertDebug( t->m_indices.m_encodingWordIndex == -1 );
	KdasmAssertDebug( m_allocationMap[index] == NULL );

	m_allocationMap[index] = t;

	KdasmAssemblerNode* n = t->m_node;
	if( t->m_isExternal || !n->HasSubnodes() )
	{
		// This is tricky.  Those "page root" leaf nodes which actually have internal
		// references are assigned an OPCODE_LEAVES_FAR encoding word here.
		t->m_indices.m_encodingWordIndex = index;
		return;
	}

	bool subnodesOk = true;
	for( intptr_t j=0; j < 2; ++j )
	{
		KdasmAssemblerNode* sn = n->GetSubnode( j );
		if( sn )
		{
			intptr_t subIndex = index + treeIndex + 1 + j;
			if( subIndex >= m_extraDataStart || m_allocationMap[subIndex] != NULL )
			{
				subnodesOk = false;
				break;
			}
		}
	}

	if( subnodesOk )
	{
		KdasmAssertDebug( t->m_indices.m_encodingWordIndex == -1 );
		KdasmAssertDebug( t->m_indices.m_treeIndex == -1 || t->m_indices.m_treeIndex == 0 );

		t->m_indices.m_encodingWordIndex = index;
		t->m_indices.m_treeIndex = treeIndex;

		for( intptr_t j=0; j < 2; ++j )
		{
			KdasmAssemblerNode* sn = n->GetSubnode( j );
			if( sn )
			{
				CommitSubtreePacking( sn->GetPageTemp(), index + treeIndex + 1 + j, treeIndex * 2 + 1 + j );
			}
		}
	}
	else
	{
		// Destination tree index is decided when the new subtree is placed.
		KdasmAssertDebug( t->m_indices.m_internalJumpIndex == -1 );
		t->m_indices.m_internalJumpIndex = index;
		m_treeRootsRemaining.push_back( t );
	}
}

void KdasmAssemblerPagePacker::WriteEncoding( void )
{
	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		if( t->m_indices.m_internalJumpIndex != -1 )
		{
			CalculateInternalJumpEncoding( t );
		}
		if( t->m_indices.m_extraDataIndex != -1 )
		{
			CalculateNodeExtraData( t );
		}
		if( t->m_indices.m_encodingWordIndex != -1 )
		{
			CalculateNodeEncoding( t );
		}
	}

#ifdef KDASM_PARANOIA
	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		if( t->m_indices.m_encodingWordIndex != -1 )
		{
			KdasmAssertDebug( ValidateNodeEncoding( t ) );
		}
	}
#endif
}

intptr_t KdasmAssemblerPagePacker::CalculateNodeExtraDataSize( KdasmAssemblerPageTempData* t )
{
	KdasmAssemblerNode* n = t->m_node;

	if( !t->m_isExternal )
	{
		if( n->HasSubnodes() )
		{
			// Either NORMAL_X/Y/Z.  Prefix is encoded in cut word.
			return n->GetDistanceLength() - 1;
		}
		else
		{
			if( t->m_isPageRoot )
			{
				// Referenced by OPCODE_LEAVES_FAR.  Requires header.
				return n->GetLeafCount() + 1;
			}
			else
			{
				// OPCODE_LEAVES.
				return n->GetLeafCount();
			}
		}
	}
	else
	{
		// OPCODE_JUMP_FAR or OPCODE_LEAVES_FAR.  This allows for subsequent
		// assignment of the actual locations.

		intptr_t physicalPageDelta = ::abs( m_virtualPage->GetPhysicalPageStart() - n->GetVirtualPage()->GetPhysicalPageStart() );
		physicalPageDelta += m_virtualPage->GetPhysicalPageCount() + n->GetVirtualPage()->GetPhysicalPageCount();
		physicalPageDelta <<= m_pageWordBits;

		return CalculateWordsRequired( physicalPageDelta );
	}
}

void KdasmAssemblerPagePacker::CalculateNodeExtraData( KdasmAssemblerPageTempData* t )
{
	KdasmAssemblerNode* n = t->m_node;

	if( !t->m_isExternal )
	{
		if( n->HasSubnodes() )
		{
			// Either NORMAL_X/Y/Z.  Prefix is encoded in cut word.
			for( int i=1; i < n->GetDistanceLength(); ++i )
			{

#ifdef KDASM_PARANOIA
				KdasmAssertDebug( m_allocationMap[t->m_indices.m_extraDataIndex + i - 1] == t );
#endif
				m_encoding[t->m_indices.m_extraDataIndex + i - 1].SetRaw( n->GetDistance()[i] );
			}
		}
		else
		{
			intptr_t headerOffset = 0;
			if( t->m_isPageRoot )
			{

#ifdef KDASM_PARANOIA
				KdasmAssertDebug( m_allocationMap[t->m_indices.m_extraDataIndex] == t );
#endif
				// Referenced by OPCODE_LEAVES_FAR.  Requires header.
				if( n->GetLeafCount() < KdasmEncoding::LEAF_COUNT_OVERFLOW )
				{
					m_encoding[t->m_indices.m_extraDataIndex].SetRaw( (unsigned short)n->GetLeafCount() );
				}
				else
				{
					// Signal data is too long.
					m_encoding[t->m_indices.m_extraDataIndex].SetRaw( KdasmEncoding::LEAF_COUNT_OVERFLOW );
				}
				headerOffset = 1;
			}

			// OPCODE_LEAVES.
			for( intptr_t i=0; i < n->GetLeafCount(); ++i )
			{
#ifdef KDASM_PARANOIA
				KdasmAssertDebug( m_allocationMap[t->m_indices.m_extraDataIndex + i + headerOffset] == t );
#endif
				m_encoding[t->m_indices.m_extraDataIndex + i + headerOffset].SetRaw( n->GetLeaves()[i] );
			}
		}
	}
	else
	{
		intptr_t nodeOffset = CalculateNodeFarOffset( t );

		// Write in reverse order.
		for( intptr_t i = t->m_indices.m_extraDataSize; i-- != 0; /**/ )
		{
#ifdef KDASM_PARANOIA
			KdasmAssertDebug( m_allocationMap[t->m_indices.m_extraDataIndex + i] == t );
#endif
			m_encoding[t->m_indices.m_extraDataIndex + i].SetRaw( (unsigned short)nodeOffset );
			nodeOffset >>= 16;
		}

		KdasmAssertDebug( nodeOffset == 0 || nodeOffset == -1 ); // Check for fit.
		// Most significant bit matches sign.
		KdasmAssertDebug( ( ( m_encoding[t->m_indices.m_extraDataIndex].GetRaw() & 0x8000 ) != 0 ) == ( nodeOffset == -1 ) );
	}
}

void KdasmAssemblerPagePacker::CalculateInternalJumpEncoding( KdasmAssemblerPageTempData* t )
{
	KdasmAssertDebug( !t->m_isExternal && !t->m_isPageRoot ); // Would imply a *_FAR opcode instead.
	KdasmAssertDebug( t->m_indices.m_encodingWordIndex != -1 && t->m_indices.m_treeIndex != -1 );
#ifdef KDASM_PARANOIA
	KdasmAssertDebug( m_allocationMap[t->m_indices.m_internalJumpIndex] == t );
#endif

	KdasmEncoding x; x.SetRaw( 0 );
	x.SetNomal( KdasmEncoding::NORMAL_OPCODE );
	x.SetOpcode( KdasmEncoding::OPCODE_JUMP );
	x.SetOperandOffset( (unsigned short)( t->m_indices.m_encodingWordIndex - t->m_indices.m_internalJumpIndex ) );
	x.SetOperandTreeIndexStart( (unsigned short)t->m_indices.m_treeIndex );

	m_encoding[t->m_indices.m_internalJumpIndex] = x;
}

void KdasmAssemblerPagePacker::CalculateNodeEncoding( KdasmAssemblerPageTempData* t )
{
	KdasmAssemblerNode* n = t->m_node;
	KdasmEncoding x; x.SetRaw( 0 );

	if( !t->m_isExternal && ( !t->m_isPageRoot || n->HasSubnodes() ) )
	{
		if( n->HasSubnodes() )
		{
			unsigned short normal = n->GetNormal();
			KdasmAssertDebug( normal != KdasmEncoding::NORMAL_OPCODE );
			x.SetNomal( normal );
			x.SetStop0( n->GetSubnode( 0 ) == NULL );
			x.SetStop1( n->GetSubnode( 1 ) == NULL );

			unsigned short distance0 = n->GetDistance()[0];
			if( n->GetDistanceLength() == 1 )
			{
				x.SetDistanceImmediate( distance0 );
			}
			else
			{
				KdasmAssertDebug( t->m_indices.m_extraDataIndex != -1 );
				x.SetDistancePrefix( distance0 );
				x.SetOperandOffset( (unsigned short)( t->m_indices.m_extraDataIndex - t->m_indices.m_encodingWordIndex ) );
			}
		}
		else
		{
			KdasmAssertDebug( !t->m_isPageRoot ); // References by OPCODE_LEAVES_FAR have no encoding.

			x.SetNomal(  KdasmEncoding::NORMAL_OPCODE );
			x.SetOpcode( KdasmEncoding::OPCODE_LEAVES );
			x.SetOperandOffset( (unsigned short)( t->m_indices.m_extraDataIndex - t->m_indices.m_encodingWordIndex ) );
			x.SetOperandLength( (unsigned short)t->m_indices.m_extraDataSize );
		}
	}
	else
	{
		x.SetNomal(  KdasmEncoding::NORMAL_OPCODE );
		x.SetOpcode( n->HasSubnodes() ? (unsigned short)KdasmEncoding::OPCODE_JUMP_FAR : (unsigned short)KdasmEncoding::OPCODE_LEAVES_FAR );

		if( !t->m_isExternal || t->m_indices.m_extraDataSize == 0 )
		{
			intptr_t offset = CalculateNodeFarOffset( t );
			x.SetIsOperandImmediate( true );
			x.SetOperandImmediate( (unsigned short)offset );
		}
		else
		{
			x.SetIsOperandImmediate( false );
			x.SetOperandWordsOffset( (unsigned short)( t->m_indices.m_extraDataIndex - t->m_indices.m_encodingWordIndex ) );
			x.SetOperandWordsCount( (unsigned short)t->m_indices.m_extraDataSize );
		}
	}

#ifdef KDASM_PARANOIA
	KdasmAssertDebug( m_allocationMap[t->m_indices.m_encodingWordIndex] == t );
#endif
	m_encoding[t->m_indices.m_encodingWordIndex] = x;
}

void KdasmAssemblerPagePacker::SaveEncodingIndices( void )
{
	// Cache off valid encoding offsets.
	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		if( t->m_isExternal )
		{
			t->m_node->GetNodeTemp()->m_externalIndices = t->m_indices;
		}
		else
		{
			t->m_node->GetNodeTemp()->m_internalIndices = t->m_indices;
		}
	}
}

void KdasmAssemblerPagePacker::UseSavedEncodingIndices( void )
{
	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		if( t->m_isExternal )
		{
			t->m_indices = t->m_node->GetNodeTemp()->m_externalIndices;
		}
		else
		{
			t->m_indices = t->m_node->GetNodeTemp()->m_internalIndices;
		}

#ifdef KDASM_PARANOIA
		if( t->m_indices.m_internalJumpIndex != -1 )
		{
			m_allocationMap[t->m_indices.m_internalJumpIndex] = t;
		}
		if( t->m_indices.m_extraDataIndex != -1 )
		{
			for( intptr_t j=0; j < t->m_indices.m_extraDataSize; ++j )
			{
				m_allocationMap[t->m_indices.m_extraDataIndex + j] = t;
			}
		}
		if( t->m_indices.m_encodingWordIndex != -1 )
		{
			m_allocationMap[t->m_indices.m_encodingWordIndex] = t;
		}
#endif
	}
}

intptr_t KdasmAssemblerPagePacker::CalculateNodeFarOffset( KdasmAssemblerPageTempData* t )
{
	KdasmAssemblerNode* n = t->m_node;

	intptr_t encodingWordIndex = n->GetNodeTemp()->m_internalIndices.m_encodingWordIndex;
	if( !t->m_isExternal || encodingWordIndex == -1 )
	{
		// Leaf nodes with far addressing have their extra data addressed directly.
		KdasmAssertDebug( !n->HasSubnodes() && t->m_indices.m_internalJumpIndex == -1 );
		encodingWordIndex = n->GetNodeTemp()->m_internalIndices.m_extraDataIndex;
	}

	KdasmAssertDebug( encodingWordIndex >= 0 );
	KdasmAssertDebug( m_virtualPage->GetPhysicalPageStart() >= 0 && m_virtualPage->GetPhysicalPageCount() >= 0 );
	KdasmAssertDebug( n->GetVirtualPage()->GetPhysicalPageStart() >= 0 && n->GetVirtualPage()->GetPhysicalPageCount() >= 0 );

	intptr_t pageWords = (intptr_t)1 << m_pageWordBits;
	intptr_t encodingLocation = m_virtualPage->GetPhysicalPageStart() * pageWords + t->m_indices.m_encodingWordIndex;
	intptr_t externalLocation = n->GetPhysicalPageStart() * pageWords + encodingWordIndex;
	return externalLocation - encodingLocation;
}

int KdasmAssemblerPagePacker::CalculateWordsRequired( intptr_t x )
{
	x = ::abs( x );
	if( x <= KdasmEncoding::IMMEDIATE_MAX )
	{
		return 0;
	}

	// Allow for negation and sign extension.
	int numPages = 1;
	while( x > (intptr_t)0x7fff )
	{
		++numPages;
		x >>= 16;
	}
	return numPages;
}

bool KdasmAssemblerPagePacker::ValidateAllocationMap( void )
{
	for( intptr_t i=0; i < (intptr_t)m_allocationMap.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = m_allocationMap[i];
		if( t != NULL )
		{
			if( t->m_indices.m_encodingWordIndex != i && t->m_indices.m_internalJumpIndex != i && t->m_indices.m_extraDataIndex != i )
			{
				KdasmAssertDebug( t->m_indices.m_extraDataIndex <= i && ( t->m_indices.m_extraDataIndex + t->m_indices.m_extraDataSize ) > i );
			}
			else
			{
				++t->m_validatedIndices;
			}
		}
	}

	for( size_t i=0; i < m_pageTempData.size(); ++i )
	{
		KdasmAssemblerPageTempData* t = &m_pageTempData[i];
		int referenceCount = (int)( t->m_indices.m_encodingWordIndex != -1 ) + (int)( t->m_indices.m_internalJumpIndex != -1 ) + (int)( t->m_indices.m_extraDataIndex != -1 );
		KdasmAssertDebug( referenceCount > 0 );
		KdasmAssertDebug( referenceCount == t->m_validatedIndices );
		KdasmAssertDebug( t->m_indices.m_internalJumpIndex == -1 || !t->m_isExternal );
	}

	return true;
}

bool KdasmAssemblerPagePacker::ValidateNodeEncoding( KdasmAssemblerPageTempData* t )
{
	KdasmAssemblerNode* n = t->m_node;
	KdasmEncoding& x = m_encoding[t->m_indices.m_encodingWordIndex];
	
	if( !t->m_isExternal && ( !t->m_isPageRoot || n->HasSubnodes() ) )
	{
		if( n->HasSubnodes() )
		{
			unsigned short normal = n->GetNormal();
			unsigned short distance0 = n->GetDistance()[0];
			if( n->GetDistanceLength() == 1 )
			{
				KdasmAssertDebug( x.GetDistanceImmediate() == distance0 );
			}
			else
			{
				KdasmAssertDebug( x.GetDistancePrefix() == distance0 );
				KdasmAssertDebug( x.UnpackOffset() == ( t->m_indices.m_extraDataIndex - t->m_indices.m_encodingWordIndex ) );
			}

			KdasmAssertDebug( x.GetNomal() == normal );
		}
		else
		{
			KdasmAssertDebug( x.GetNomal() == KdasmEncoding::NORMAL_OPCODE );
			KdasmAssertDebug( x.GetOpcode() == KdasmEncoding::OPCODE_LEAVES );
			KdasmAssertDebug( x.UnpackOffset() == ( t->m_indices.m_extraDataIndex - t->m_indices.m_encodingWordIndex ) );
			KdasmAssertDebug( x.GetOperandLength() == t->m_indices.m_extraDataSize );
		}

		if( t->m_indices.m_internalJumpIndex != -1 )
		{
			KdasmEncoding& j = m_encoding[t->m_indices.m_internalJumpIndex];

			KdasmAssertDebug( j.GetNomal() == KdasmEncoding::NORMAL_OPCODE );
			KdasmAssertDebug( j.GetOpcode() == KdasmEncoding::OPCODE_JUMP );
			KdasmAssertDebug( j.UnpackOffset() == ( t->m_indices.m_encodingWordIndex - t->m_indices.m_internalJumpIndex ) );
			KdasmAssertDebug( j.GetOperandTreeIndexStart() == t->m_indices.m_treeIndex );
		}
	}
	else
	{
		intptr_t offset = CalculateNodeFarOffset( t );
		KdasmAssertDebug( x.GetNomal() == KdasmEncoding::NORMAL_OPCODE );
		KdasmAssertDebug( x.GetOpcode() == ( n->HasSubnodes() ? KdasmEncoding::OPCODE_JUMP_FAR : KdasmEncoding::OPCODE_LEAVES_FAR ) );
		KdasmAssertDebug( x.UnpackFarOffset() == offset ); // May read extra data words.
	}

	return true;
}

// ----------------------------------------------------------------------------

KdasmAssembler::KdasmAssembler( void )
{
	m_activityCallback = NULL;
	m_activityData = NULL;
	m_activityFrequency = INT_MAX;
	m_activityCounter = 0;
}

void KdasmAssembler::SetActivityCallback( KdasmAssembler::ActivityCallback callback, void* data, int activityFrequency )
{
	m_activityCallback = callback;
	m_activityData = data;
	m_activityFrequency = activityFrequency;
}

void KdasmAssembler::Assemble( KdasmAssemblerNode* root, KdasmEncodingHeader::PageBits pageBits, std::vector<KdasmEncoding>& result )
{
	result.clear();

	KdasmAssemblerNode empty;
	if( root == NULL )
	{
		root = &empty;
	}

	pageBits = ( pageBits < KdasmEncodingHeader::PAGE_BITS_32B )  ? KdasmEncodingHeader::PAGE_BITS_32B
	       : ( ( pageBits > KdasmEncodingHeader::PAGE_BITS_128B ) ? KdasmEncodingHeader::PAGE_BITS_128B : pageBits );
	m_pagePacker.SetPageSize( (int)pageBits );
	m_pageAllocator.SetPhysicalPageWords( (int)pageBits );

	root->TrimEmpty();
	root->AssemblePrepare( NULL, 1 ); // A CompareToId of 0 is invalid.
	root->GetNodeTemp()->m_forceFarAddressing = true;

	m_globalQueue.Init( root, m_pageAllocator );
	KdasmAssertDebug( root->GetVirtualPage()->PageStart() != 0 ); // Is header page.

	while( !m_globalQueue.Empty() )
	{
		PackNextPage();
	}

	m_pageAllocator.CompactAndFreePhysicalPages();

	BinPack();

	m_pageAllocator.CompactAndFreePhysicalPages();

	Encode( root, pageBits, result );

	root->AssembleFinish();

	Clear();
}

void KdasmAssembler::TickActivity( void )
{
	if( ++m_activityCounter >= m_activityFrequency )
	{
		m_activityCounter = 0;
		if( m_activityCallback != NULL )
		{
			m_activityCallback( m_activityData );
		}
	}
}

void KdasmAssembler::PackNextPage( void )
{
	TickActivity();

	KdasmAssemblerNode* pageRootNode = m_globalQueue.GetNext( m_pageAllocator );
	m_globalQueue.PopNext( false ); // remove node and subtree from global queue.

	KdasmAssertDebug( pageRootNode );
	m_pageQueue.Init( pageRootNode, m_pageAllocator );
	m_pageQueue.PopNext( true ); // add subnodes to page queue.

	KdasmAssemblerVirtualPage* virtualPage = pageRootNode->GetVirtualPage();
	KdasmAssertDebug( virtualPage );

	while( !m_pageQueue.Empty() )
	{
		KdasmAssemblerNode* nodeToAdd = m_pageQueue.GetNext( m_pageAllocator );

		KdasmAssemblerVirtualPage* virtualPagePrevioius = nodeToAdd->GetVirtualPage();
		virtualPagePrevioius->RemoveNode( nodeToAdd );
		KdasmAssertDebug( virtualPagePrevioius->NodeCount() == 0 );
		virtualPage->InsertNode( nodeToAdd );

		// Add two pad words to allow longer external references when bin packing.  This
		// is the one bit of fudge factor and should be improved on.
		if( m_pagePacker.Pack( virtualPage, true, 2 ) )
		{
			m_pageAllocator.Recycle( virtualPagePrevioius );
			m_pageQueue.PopNext( true );

			if( m_pageQueue.Empty() )
			{
				return; // Correct result.
			}
		}
		else
		{
			virtualPage->RemoveNode( nodeToAdd );
			virtualPagePrevioius->InsertNode( nodeToAdd );

			m_globalQueue.Append( nodeToAdd );
			m_pageQueue.PopNext( false );
		}
	}

	if( virtualPage->NodeCount() == 1 )
	{
		// Inital state was never stored.
		bool result = m_pagePacker.Pack( virtualPage, true );
		KdasmAssertDebug( result );
	}
}

void KdasmAssembler::Encode( KdasmAssemblerNode* root, KdasmEncodingHeader::PageBits pageBits, std::vector<KdasmEncoding>& result )
{
	size_t expectedSize = m_pageAllocator.AllocatedSize();
	result.reserve( expectedSize );

	std::vector<KdasmAssemblerVirtualPage*>& pages = m_pageAllocator.GetAllocatedPages();
	for( size_t i=0; i < pages.size(); ++i )
	{
		std::vector<KdasmEncoding>& pageEncoding = m_pagePacker.Encode( pages[i] );
		result.insert( result.end(), pageEncoding.begin(), pageEncoding.end() );
		TickActivity();
	}

	KdasmAssertDebug( result.size() == expectedSize );
	KdasmAssertDebug( pages[0]->GetNodes().front() == root );

	KdasmEncodingHeader h;
	h.Reset();
	h.SetDistanceLength( (unsigned short)root->GetDistanceLength() );
	h.SetIsLeavesAtRoot( !root->HasSubnodes() );
	h.SetPageBits( pageBits );

	for( int i=0; i < KdasmEncodingHeader::HEADER_LENGTH; ++i )
	{
		KdasmAssertDebug( result[i].GetRaw() == KdasmEncoding::PAD_VALUE );
		result[i].SetRaw( h.GetRaw( i ) );
	}
}

void KdasmAssembler::BinPack( void )
{
	std::vector<KdasmAssemblerVirtualPage*>& pages = m_pageAllocator.GetAllocatedPages();
	if( pages.size() <= 2 )
	{
		return;
	}

	intptr_t pageWords = m_pageAllocator.GetPhysicalPageWords();
	BuildPagesBySize( pageWords );

	// Scan the "bins" from largest encoding to smallest.
	ptrdiff_t compactPhysicalPagesCounter = 0;
	for( ptrdiff_t i=pageWords; i > 0; --i )
	{
		for( size_t j=0; j < pages.size(); ++j )
		{
			KdasmAssertDebug( pages[j]->ValidatePageHierarchy() );
		}

		while( !m_pagesBySize[i].empty() )
		{
			KdasmAssemblerVirtualPage* bin = m_pagesBySize[i].back();
			m_pagesBySize[i].pop_back();

			ptrdiff_t remainingWords = bin->GetPhysicalPageCount() * pageWords - bin->GetEncodingSize();
			KdasmAssertDebug( remainingWords >= 0 && remainingWords < pageWords );
			if( remainingWords > i )
			{
				remainingWords = i; // Larger pages were already packed and removed.
			}

			// Look through physically nearby pages that might fit starting with pages
			// that are the same size as the remaining space and then on down.
			for( ptrdiff_t j=remainingWords; j > 0; --j )
			{
				std::vector<KdasmAssemblerVirtualPage*>& currentPagesBySize = m_pagesBySize[j];
				if( currentPagesBySize.empty() )
				{
					continue;
				}

				intptr_t pivot = FindClosestPhysicalPage( bin, currentPagesBySize );
				intptr_t distance = 0;
				bool step = true;

				while( !currentPagesBySize.empty() )
				{
					TickActivity();

					KdasmAssemblerVirtualPage* pg = currentPagesBySize[pivot + distance];

					if( TryBinPack( bin, pg ) )
					{
						currentPagesBySize.erase( currentPagesBySize.begin() + pivot + distance );

						compactPhysicalPagesCounter += pg->GetPhysicalPageCount();
						m_pageAllocator.Recycle( pg );
						if( compactPhysicalPagesCounter > (KdasmEncoding::IMMEDIATE_MAX/2) )
						{
							compactPhysicalPagesCounter = 0;
							m_pageAllocator.CompactAndFreePhysicalPages();
						}

						remainingWords = bin->GetPhysicalPageCount() * pageWords - bin->GetEncodingSize();
						KdasmAssertDebug( remainingWords >= 0 && remainingWords < pageWords );
						if( j > remainingWords )
						{
							j = remainingWords + 1; // skip to remainingWords.
							break;
						}

						if( distance <= 0 && pivot > 0 )
						{
							--pivot;
						}
						step = false;
					}

					distance = -distance;
					if( step )
					{
						distance += ( distance >= 0 ) ? 1 : -1;
					}
					step = !step;
					if( ( pivot + distance ) < 0 || ( pivot + distance ) >= (intptr_t)currentPagesBySize.size() )
					{
						distance = -distance;
						if( step )
						{
							distance += ( distance >= 0 ) ? 1 : -1;
						}
						step = !step;
						if( ( pivot + distance ) < 0 || ( pivot + distance ) >= (intptr_t)currentPagesBySize.size() )
						{
							break;
						}
					}
					if( distance > MAX_PAGE_MERGE_SCAN_DISTANCE )
					{
						break;
					}
				}
			}
		}
	}
}

void KdasmAssembler::BuildPagesBySize( intptr_t pageWords )
{
	// Last index is actually for pages larger than a single physical page.
	m_pagesBySize.clear();
	m_pagesBySize.resize( pageWords + 1 );

	// Skip bin packing the root page.  It should be well packed anyway.
	std::vector<KdasmAssemblerVirtualPage*>& pages = m_pageAllocator.GetAllocatedPages();
	pages[0]->BuildPageHierarchy();

	// Queue bins earlier in the address space before those that come after.  This gives
	// pages closer to the root of the tree a chance to gather nearby nodes first.
	// Pages of size "pageWords" and "pageWords-1" are not added as they are full.
	for( size_t i=pages.size()-1; i >= 1; --i )
	{
		KdasmAssemblerVirtualPage* pg = pages[i];
		pg->BuildPageHierarchy();

		if( pg->GetEncodingSize() >= (pageWords + 1) )
		{
			m_pagesBySize[pageWords].push_back( pg );
		}
		else if( pg->GetEncodingSize() < (pageWords - 1) )
		{
			m_pagesBySize[pg->GetEncodingSize()].push_back( pg );
		}
	}
	KdasmAssertDebug( m_pagesBySize[0].empty() );

	// As a special case, oversize pages are packed first and are bins only, but at
	// least hit them in the right order.
	std::sort( m_pagesBySize[pageWords].begin(), m_pagesBySize[pageWords].end(), KdasmAssemblerVirtualPage::CompareByEncodingSize );
}

intptr_t KdasmAssembler::FindClosestPhysicalPage( KdasmAssemblerVirtualPage* bin, std::vector<KdasmAssemblerVirtualPage*>& pages )
{
	KdasmAssertDebug( !pages.empty() );
	std::vector<KdasmAssemblerVirtualPage*>::iterator i = std::lower_bound( pages.begin(), pages.end(),
		bin, KdasmAssemblerVirtualPage::CompareByPhysicalPagesReverse );

	if( i == pages.end() )
	{
		i = pages.end() - 1;
	}
	return (intptr_t)( i - pages.begin() );
}

bool KdasmAssembler::TryBinPack( KdasmAssemblerVirtualPage* bin, KdasmAssemblerVirtualPage* pg )
{
	std::vector<KdasmAssemblerNode*>& binNodes = bin->GetNodes();
	std::vector<KdasmAssemblerNode*>& pgNodes = pg->GetNodes();
	size_t pgNodeCount = pgNodes.size();

	// Append page nodes to bin.
	binNodes.insert( binNodes.end(), pgNodes.begin(), pgNodes.end() );
	for( size_t i=binNodes.size() - pgNodeCount; i < binNodes.size(); ++i )
	{
		binNodes[i]->SetVirtualPage( bin );
	}
	bin->BuildPageHierarchy();

	// Test if page and referring pages will still encode within size limits.
	bool packOk = m_pagePacker.Pack( bin, false );
	if( packOk )
	{
		for( size_t i=0; i < bin->GetSuperPages().size(); ++i )
		{
			if( !m_pagePacker.Pack( bin->GetSuperPages()[i], false ) )
			{
				packOk = false;
				break;
			}
		}
	}
	if( packOk )
	{
		// Commit to page merge.
		pgNodes.clear();
		pg->BuildPageHierarchy();

		bool packOk = m_pagePacker.Pack( bin, true );
		for( size_t i=0; i < bin->GetSuperPages().size(); ++i )
		{
			bin->GetSuperPages()[i]->BuildPageHierarchy();
			packOk |= m_pagePacker.Pack( bin->GetSuperPages()[i], true );
		}
		KdasmAssertDebug( packOk );

		for( size_t i=0; i < bin->GetSubPages().size(); ++i )
		{
			bin->GetSubPages()[i]->BuildPageHierarchy();
		}
	}
	else
	{
		// Revert changes.
		for( size_t i=0; i < pgNodeCount; ++i )
		{
			pgNodes[i]->SetVirtualPage( pg );
		}

		binNodes.erase( binNodes.end() - pgNodeCount, binNodes.end() );
		bin->BuildPageHierarchy();
	}
	return packOk;
}

void KdasmAssembler::Clear( void )
{
	m_pageAllocator.Clear();
	m_globalQueue.Clear();
	m_pageQueue.Clear();
	m_pagePacker.Clear();
	m_pagesBySize.clear();
}

// ----------------------------------------------------------------------------

KdasmAssemblerNode* KdasmDisassembler::Disassemble( KdasmEncoding* encodingRoot, KdasmAssemblerNode* compareTo )
{
	::memset( this, 0, sizeof *this );

	KdasmEncodingHeader* header = (KdasmEncodingHeader*)encodingRoot;
	if( !header->VersionCheck() )
	{
		return NULL;
	}

	m_pageWords    = (intptr_t)1 << (header->GetPageBits() - 1);
	m_encodingRoot = encodingRoot;

	KdasmAssemblerNode* result = NULL;
	if( header->IsLeavesAtRoot() )
	{
		result = DisassembleLeavesFar( encodingRoot + KdasmEncodingHeader::HEADER_LENGTH, compareTo );
	}
	else
	{
		m_distanceLength = (int)header->GetDistanceLength();
		result = DisassembleEncoding( encodingRoot + KdasmEncodingHeader::HEADER_LENGTH, 0, compareTo );
	}

	if( m_compareToFailId != 0 )
	{
		delete result;
		return NULL;
	}
	return result;
}

KdasmAssemblerNode* KdasmDisassembler::DisassembleEncoding( KdasmEncoding* encoding, intptr_t treeIndex, KdasmAssemblerNode* compareTo )
{
	unsigned short normal = encoding->GetNomal();
	if( normal == KdasmEncoding::NORMAL_OPCODE )
	{
		switch( encoding->GetOpcode() )
		{
			case KdasmEncoding::OPCODE_LEAVES:
			{
				intptr_t offset = encoding->UnpackOffset();
				intptr_t leafCount = (intptr_t)encoding->GetOperandLength();
				return DisassembleLeaves( encoding + offset, leafCount, compareTo );
			}
			case KdasmEncoding::OPCODE_LEAVES_FAR:
			{
				intptr_t offset = encoding->UnpackFarOffset();
				return DisassembleLeavesFar( encoding + offset, compareTo );
			}
			case KdasmEncoding::OPCODE_JUMP:
			{
				intptr_t offset = encoding->UnpackOffset();
				intptr_t treeIndexStart = (intptr_t)encoding->GetOperandTreeIndexStart();
				return DisassembleEncoding( encoding + offset, treeIndexStart, compareTo );
			}
			case KdasmEncoding::OPCODE_JUMP_FAR:
			{
				intptr_t offset = encoding->UnpackFarOffset();
				return DisassembleEncoding( encoding + offset, 0, compareTo );
			}
			default:
			{
				KdasmAssertDebug( 0 ); // Impossible
				return NULL;
			}
		}
	}
	else
	{
		// This should be done with GetDistanceImmediate() or UnpackDistance<DISTANCE_LENGTH>().
		// However this is generic tools code.
		unsigned short distance[KdasmEncodingHeader::DISTANCE_LENGTH_MAX];
		if( m_distanceLength == 1 )
		{
			distance[0] = encoding->GetDistanceImmediate();
		}
		else
		{
			distance[0] = encoding->GetDistancePrefix();
			intptr_t offset = encoding->UnpackOffset();

			for( int i=1; i < m_distanceLength; ++i )
			{
				distance[i] = ( encoding + offset + i - 1 )->GetRaw();
			}
		}

		// This would fire if PAD_VALUE data was hit.
		KdasmAssertDebug( !encoding->GetStop0() || !encoding->GetStop1() );

		if( compareTo )
		{
			if( compareTo->GetNormal() != normal
				|| compareTo->GetDistanceLength() != m_distanceLength )
			{
				m_compareToFailId = compareTo->GetCompareToId();
				return NULL;
			}
			for( int i=0; i < m_distanceLength; ++i )
			{
				if( distance[i] != compareTo->GetDistance()[i] )
				{
					m_compareToFailId = compareTo->GetCompareToId();
					return NULL;
				}
			}

			if( encoding->GetStop0() != ( compareTo->GetSubnode( 0 ) == NULL ) )
			{
				m_compareToFailId = compareTo->GetCompareToId();
				return NULL;
			}
			if( encoding->GetStop1() != ( compareTo->GetSubnode( 1 ) == NULL ) )
			{
				m_compareToFailId = compareTo->GetCompareToId();
				return NULL;
			}
		}

		KdasmAssemblerNode* subnode0 = NULL;
		if( !encoding->GetStop0() )
		{
			// Destination index is "2n+1" however encoding is already offset by n.  
			KdasmEncoding* destinationEncoding = encoding + ( treeIndex + 1 );
			KdasmAssemblerNode* compareTo0 = compareTo ? compareTo->GetSubnode( 0 ) : NULL;
			subnode0 = DisassembleEncoding( destinationEncoding, treeIndex * 2 + 1, compareTo0 );
		}

		KdasmAssemblerNode* subnode1 = NULL;
		if( !encoding->GetStop1() )
		{
			// Destination index is "2n+2" however encoding is already offset by n.  
			KdasmEncoding* destinationEncoding = encoding + ( treeIndex + 2 );
			KdasmAssemblerNode* compareTo1 = compareTo ? compareTo->GetSubnode( 1 ) : NULL;
			subnode1 = DisassembleEncoding( destinationEncoding, treeIndex * 2 + 2, compareTo1 );
		}

		KdasmAssemblerNode* n = new KdasmAssemblerNode;
		n->AddSubnodes( distance, m_distanceLength, normal, subnode0, subnode1 );
		return n;
	}
}

KdasmAssemblerNode* KdasmDisassembler::DisassembleLeavesFar( KdasmEncoding* encoding, KdasmAssemblerNode* compareTo )
{
	intptr_t leafCount = (intptr_t)encoding->GetRaw();
	KdasmAssert( "Leaf data block overflow.  Will require a header.", leafCount < KdasmEncoding::LEAF_COUNT_OVERFLOW );
	++encoding;
	return DisassembleLeaves( encoding, leafCount, compareTo );
}

KdasmAssemblerNode* KdasmDisassembler::DisassembleLeaves( KdasmEncoding* encoding, intptr_t leafCount, KdasmAssemblerNode* compareTo )
{
	unsigned short* leaves = new unsigned short[leafCount];
	for( intptr_t i=0; i < leafCount; ++i )
	{
		leaves[i] = encoding[i].GetRaw();
	}

	if( compareTo )
	{
		if( compareTo->GetLeafCount() != leafCount )
		{
			m_compareToFailId = compareTo->GetCompareToId();
			return NULL;
		}
		for( intptr_t i=0; i < leafCount; ++i )
		{
			if( leaves[i] != compareTo->GetLeaves()[i] )
			{
				m_compareToFailId = compareTo->GetCompareToId();
				return NULL;
			}
		}
	}

	KdasmAssemblerNode* n = new KdasmAssemblerNode;
	n->AddLeaves( leafCount, leaves );
	return n;
}

void KdasmDisassembler::CalculateStats( KdasmEncoding* encodingRoot, intptr_t encodingSize, EncodingStats& stats )
{
	::memset( this, 0, sizeof *this );
	::memset( &stats, 0, sizeof stats );

	KdasmEncodingHeader* header = (KdasmEncodingHeader*)encodingRoot;
	if( !header->VersionCheck() )
	{
		stats.m_paddingData = encodingSize;
		return;
	}

	stats.m_headerData = KdasmEncodingHeader::HEADER_LENGTH;

	m_pageWords    = (intptr_t)1 << (header->GetPageBits() - 1);
	m_encodingRoot = encodingRoot;

	if( header->IsLeavesAtRoot() )
	{
		CalculateStatsLeavesFar( encodingRoot + KdasmEncodingHeader::HEADER_LENGTH, stats );
	}
	else
	{
		m_distanceLength = (int)header->GetDistanceLength();
		CalculateStatsEncoding( encodingRoot + KdasmEncodingHeader::HEADER_LENGTH, 0, stats );
	}

	stats.m_totalEncodingData = stats.m_cuttingPlaneNodeCount + stats.m_cuttingPlaneExtraData
						+ stats.m_leafHeaderCount    + stats.m_leafblockData
						+ stats.m_leafNodeCount
						+ stats.m_leafNodeFarCount   + stats.m_leafNodeFarExtraData
						+ stats.m_jumpNodeCount
						+ stats.m_jumpNodeFarCount   + stats.m_jumpNodeFarExtraData
						+ stats.m_headerData;

	stats.m_paddingData = encodingSize - stats.m_totalEncodingData;
}

void KdasmDisassembler::CalculateStatsEncoding( KdasmEncoding* encoding, intptr_t treeIndex, EncodingStats& stats )
{
	unsigned short normal = encoding->GetNomal();
	if( normal == KdasmEncoding::NORMAL_OPCODE )
	{
		switch( encoding->GetOpcode() )
		{
			case KdasmEncoding::OPCODE_LEAVES:
			{
				intptr_t leafCount = (intptr_t)encoding->GetOperandLength();

				++stats.m_leafNodeCount;
				stats.m_leafblockData += leafCount; // Technically extra data, but that confuses the point.
				return;
			}
			case KdasmEncoding::OPCODE_LEAVES_FAR:
			{
				intptr_t offset = encoding->UnpackFarOffset();

				++stats.m_leafNodeFarCount;
				stats.m_leafNodeFarExtraData += encoding->GetIsOperandImmediate() ? 0 : encoding->GetOperandWordsCount();

				CalculateStatsLeavesFar( encoding + offset, stats );
				return;
			}
			case KdasmEncoding::OPCODE_JUMP:
			{
				intptr_t offset = encoding->UnpackOffset();
				intptr_t treeIndexStart = (intptr_t)encoding->GetOperandTreeIndexStart();

				++stats.m_jumpNodeCount;

				CalculateStatsEncoding( encoding + offset, treeIndexStart, stats );
				return;
			}
			case KdasmEncoding::OPCODE_JUMP_FAR:
			{
				intptr_t offset = encoding->UnpackFarOffset();

				++stats.m_jumpNodeFarCount;
				stats.m_jumpNodeFarExtraData += encoding->GetIsOperandImmediate() ? 0 : encoding->GetOperandWordsCount();

				CalculateStatsEncoding( encoding + offset, 0, stats );
				return;
			}
		}
	}
	else
	{
		++stats.m_cuttingPlaneNodeCount;
		stats.m_cuttingPlaneExtraData += m_distanceLength - 1;

		if( !encoding->GetStop0() )
		{
			KdasmEncoding* destinationEncoding = encoding + ( treeIndex + 1 );
			CalculateStatsEncoding( destinationEncoding, treeIndex * 2 + 1, stats );
		}
		if( !encoding->GetStop1() )
		{
			KdasmEncoding* destinationEncoding = encoding + ( treeIndex + 2 );
			CalculateStatsEncoding( destinationEncoding, treeIndex * 2 + 2, stats );
		}
	}
}

void KdasmDisassembler::CalculateStatsLeavesFar( KdasmEncoding* encoding, EncodingStats& stats )
{
	intptr_t leafCount = (intptr_t)encoding->GetRaw();
	KdasmAssert( "Leaf data block overflow.  Will require a header.", leafCount < KdasmEncoding::LEAF_COUNT_OVERFLOW );

	++stats.m_leafHeaderCount;
	stats.m_leafblockData += leafCount;
}

// Tools code does not require correct alignment.
KdasmEncoding* KdasmDisassembler::PageBaseAddress( KdasmEncoding* encoding )
{
	intptr_t offset = encoding - m_encodingRoot;
	intptr_t pageOffset = offset & ( m_pageWords - 1 );
	return encoding - pageOffset;
}
