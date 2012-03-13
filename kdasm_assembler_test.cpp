// Copyright (c) 2012 Adrian Johnston.  All rights reserved.
// See Copyright Notice in kdasm.h

#include "kdasm_assembler.h"

#include <stdio.h>
#include <vector>

// ----------------------------------------------------------------------------

class KdasmTest
{
public:
	struct KdasmTestRandomSettings
	{
		int								m_maxNodes;
		int								m_maxLeaves;
		int								m_distanceLength;
		int								m_percentSubnodes;
		int								m_percentEmpty;
		unsigned short					m_seed;
		KdasmEncodingHeader::PageBits	m_pageBits;
	};

	KdasmTest( void );

	void SRand( unsigned short s );
	unsigned short Rand16( void );
	int RandBool( unsigned int percentChance );
	intptr_t Rand( size_t max );

	void SpewAssemblerNodes( KdasmAssemblerNode* nodes, intptr_t depth );
	KdasmAssemblerNode* GenerateRandomNodes( const KdasmTestRandomSettings& randomSettings );

	void TickActivity( bool callback );
	static void ActivityCallback( void* data );

	// Test cases
	void TestLeavesAtRoot( KdasmAssembler& kdasmAssembler, KdasmDisassembler& kdasmDisassembler );
	void TestRandom( KdasmAssembler& kdasmAssembler, KdasmDisassembler& kdasmDisassembler );

private:
	unsigned short	m_randSeed;
	intptr_t		m_activityCounter;
	intptr_t		m_activityIncrement;
};

// ----------------------------------------------------------------------------

KdasmTest::KdasmTest( void )
{
	m_randSeed = 1;
	m_activityCounter = 0;
	m_activityIncrement = 10000;
}

// Gerhard's generator
void KdasmTest::SRand( unsigned short s )
{
    m_randSeed = s;
}

unsigned short KdasmTest::Rand16( void )
{
    m_randSeed = (m_randSeed * 32719 + 3) % 32749;
    return m_randSeed;
}

int KdasmTest::RandBool( unsigned int percentChance )
{
	return ( (unsigned int)Rand16() % 100 ) < percentChance;
}

intptr_t KdasmTest::Rand( size_t max )
{
    size_t x = (size_t)Rand16();
	for( int i=2; i < sizeof( size_t ); i += 2 )
	{
		x <<= 16;
		x |= (size_t)Rand16();
	}
	return (intptr_t)( x % max );
}

void KdasmTest::SpewAssemblerNodes( KdasmAssemblerNode* nodes, intptr_t depth )
{
	if( nodes == NULL )
	{
		return;
	}
	for( intptr_t i=0; i < depth; ++i )
	{
		putchar( ' ' );
	}
	if( nodes->HasSubnodes() )
	{
		printf( "branch %s%s%d %d\n", nodes->GetSubnode( 0 ) ? "less ": "", nodes->GetSubnode( 1 ) ? "greater ": "", nodes->GetDistance(), nodes->GetNormal() );
		SpewAssemblerNodes( nodes->GetSubnode( 0 ), depth + 1 );
		SpewAssemblerNodes( nodes->GetSubnode( 1 ), depth + 1 );
	}
	else
	{
		intptr_t leafCount = nodes->GetLeafCount();
		unsigned short* leaves = nodes->GetLeaves();
		printf( "%d leaves: ", leafCount );
		for( intptr_t i=0; i < leafCount; ++i )
		{
			printf( "%d ", leaves[i] );
		}
		putchar( '\n' );
	}
}

KdasmAssemblerNode* KdasmTest::GenerateRandomNodes( const KdasmTestRandomSettings& randomSettings )
{
	intptr_t maxNodes = randomSettings.m_maxNodes;
	m_randSeed = (unsigned short)randomSettings.m_seed;

	KdasmAssemblerNode* root = new KdasmAssemblerNode;

	std::vector<KdasmAssemblerNode*> active;
	active.push_back( root );

	while( !active.empty() )
	{
		// pick the next node at random
		intptr_t currentIndex = Rand( active.size() );
		KdasmAssemblerNode* current = active[currentIndex];
		active[currentIndex] = active.back();
		active.pop_back();

		KdasmAssemblerNode* less    = ( RandBool( randomSettings.m_percentSubnodes ) ) ? new KdasmAssemblerNode : NULL;
		KdasmAssemblerNode* greater = ( RandBool( randomSettings.m_percentSubnodes ) ) ? new KdasmAssemblerNode : NULL;

		if( less || greater )
		{
			unsigned short normal = Rand16() % 3;
			if( randomSettings.m_distanceLength == 1 )
			{
				unsigned short distance = (unsigned short)( Rand( KdasmEncoding::DISTANCE_IMMEDIATE_MAX ) & KdasmEncoding::DISTANCE_IMMEDIATE_MASK );
				current->AddSubnodes( distance, normal, less, greater );
			}
			else
			{
				intptr_t distance = Rand( (intptr_t)KdasmEncoding::DISTANCE_PREFIX_MAX << ( randomSettings.m_distanceLength - 1 ) );
				current->AddSubnodes( distance, randomSettings.m_distanceLength, normal, less, greater );
			}
			if( less )
			{
				--maxNodes;
				active.push_back( less );
			}
			if( greater )
			{
				--maxNodes;
				active.push_back( greater );
			}
			if( maxNodes < 2 )
			{
				break;
			}
		}
		else
		{
			if( !RandBool( randomSettings.m_percentEmpty ) )
			{
				intptr_t numLeaves = Rand( randomSettings.m_maxLeaves + 1 );
				unsigned short* leaves = new unsigned short[numLeaves];
				for( intptr_t i=0; i < numLeaves; ++i )
				{
					leaves[i] = Rand16();
				}
				current->AddLeaves( numLeaves, leaves );
			}
		}

		TickActivity( false );
	}

	// Stuff leaves in the unfinished nodes, if any, to keep the tree canonical.
	while( !active.empty() )
	{
		KdasmAssemblerNode* current = active.back();
		active.pop_back();

		intptr_t numLeaves = Rand( randomSettings.m_maxLeaves - 1 ) + 1;
		unsigned short* leaves = new unsigned short[numLeaves];
		for( int i=0; i < numLeaves; ++i )
		{
			leaves[i] = Rand16();
		}
		current->AddLeaves( numLeaves, leaves );

		TickActivity( false );
	}

	root->TrimEmpty();

	return root;
}

void KdasmTest::ActivityCallback( void* data )
{
	KdasmTest* test = (KdasmTest*)data;
	test->TickActivity( true );
}

void KdasmTest::TickActivity( bool callback )
{
	if( callback || ++m_activityCounter > m_activityIncrement )
	{
		m_activityCounter = 0;
		printf( "." );
	}
}

void KdasmTest::TestLeavesAtRoot( KdasmAssembler& kdasmAssembler, KdasmDisassembler& kdasmDisassembler )
{
	m_randSeed = 0x2c84;

	// Checks one and two page size boundaries, including header and leaf block prefix word.
	static const int sizes[] = { 0, 1, 7, 29, 30, 61, 62, 200, 20000 };
	for( int i=0; i < (sizeof sizes / sizeof *sizes); ++i )
	{
		printf( "Test leaves at root %d.", sizes[i] );

		KdasmAssemblerNode* leavesAtRoot = new KdasmAssemblerNode;
		unsigned short* leaves = new unsigned short[sizes[i]];
		for( int j=0; j < sizes[i]; ++j )
		{
			leaves[j] = Rand16();
		}
		leavesAtRoot->AddLeaves( sizes[i], leaves ); 

		std::vector<KdasmEncoding> leavesAtRootResult;
		kdasmAssembler.Assemble( leavesAtRoot, KdasmEncodingHeader::PAGE_BITS_64B, leavesAtRootResult );

		KdasmAssemblerNode* leavesAtRootDisassembly = kdasmDisassembler.Disassemble( &leavesAtRootResult[0], leavesAtRoot );
		KdasmAssert( "Disassembly failed", leavesAtRootDisassembly );
		KdasmAssert( "Disassembly is not equal", leavesAtRoot->Equals( *leavesAtRootDisassembly ) ); // Double check.

		delete leavesAtRoot;
		delete leavesAtRootDisassembly;

		printf( ".\n" );
	}
}

void KdasmTest::TestRandom( KdasmAssembler& kdasmAssembler, KdasmDisassembler& kdasmDisassembler )
{
	static const KdasmTestRandomSettings settings[] = {
		// m_maxNodes,  m_maxLeaves,  m_distanceLength, m_percentSubnodes,  m_percentEmpty,  m_seed,  m_pageBits
		{        2000,            7,                 1,                77,              30,  0x8a15,  KdasmEncodingHeader::PAGE_BITS_128B },
		{        1000,          100,                 2,                70,              50,  0x61c6,  KdasmEncodingHeader::PAGE_BITS_64B  },
		{         100,           10,                 4,                73,              20,  0x73e5,  KdasmEncodingHeader::PAGE_BITS_32B  },
		{      100000,            8,                 1,                73,              20,  0x2152,  KdasmEncodingHeader::PAGE_BITS_64B  },
		{        1000,          100,                 2,                70,              50,  0x7988,  KdasmEncodingHeader::PAGE_BITS_64B  },
		{        1000,          100,                 2,                70,              50,  0xe750,  KdasmEncodingHeader::PAGE_BITS_64B  },
		{        1000,          100,                 2,                70,              50,  0x5a30,  KdasmEncodingHeader::PAGE_BITS_64B  },
	};

	for( int i=0; i < (sizeof settings / sizeof *settings); ++i )
	{
		printf( "Test random %x.", settings[i].m_seed );

		m_randSeed = settings[i].m_seed;

		KdasmAssemblerNode* random = GenerateRandomNodes( settings[i] );

		std::vector<KdasmEncoding> randomResult;
		kdasmAssembler.Assemble( random, settings[i].m_pageBits, randomResult );

		KdasmAssemblerNode* randomDisassembly = kdasmDisassembler.Disassemble( &randomResult[0], random );
		KdasmAssert( "Disassembly failed", random );
		KdasmAssert( "Disassembly is not equal", random->Equals( *randomDisassembly ) ); // Double check.

		delete random;
		delete randomDisassembly;

		KdasmDisassembler::EncodingStats stats;
		kdasmDisassembler.CalculateStats( &randomResult[0], (intptr_t)randomResult.size(), stats );

		printf( "\nStats:\n" );
		printf( "\t %8d totalEncodingData\n", stats.m_totalEncodingData );
		printf( "\t %8d paddingData\n", stats.m_paddingData );
		printf( "\t %8d headerData\n",  stats.m_headerData );
		printf( "\t %8d cuttingPlaneNodeCount\n", stats.m_cuttingPlaneNodeCount );
		printf( "\t %8d cuttingPlaneExtraData\n", stats.m_cuttingPlaneExtraData );
		printf( "\t %8d leafHeaderCount\n", stats.m_leafHeaderCount );
		printf( "\t %8d leafblockData\n", stats.m_leafblockData );
		printf( "\t %8d leafNodeCount\n", stats.m_leafNodeCount );
		printf( "\t %8d leafNodeFarCount\n", stats.m_leafNodeFarCount );
		printf( "\t %8d leafNodeFarExtraData\n", stats.m_leafNodeFarExtraData );
		printf( "\t %8d jumpNodeCount\n", stats.m_jumpNodeCount );
		printf( "\t %8d jumpNodeFarCount\n", stats.m_jumpNodeFarCount );
		printf( "\t %8d jumpNodeFarExtraData\n", stats.m_jumpNodeFarExtraData );

		intptr_t nodeDataWithPadding = sizeof(unsigned short) * ( stats.m_totalEncodingData + stats.m_paddingData - stats.m_leafblockData );
		intptr_t nodeDataNoPadding   = sizeof(unsigned short) * ( stats.m_totalEncodingData - stats.m_leafblockData );
		intptr_t nodeCount = stats.m_cuttingPlaneNodeCount + stats.m_leafNodeCount + stats.m_leafNodeFarCount;
		if( nodeCount == 0 )
		{
			nodeCount = 1; // Single block of leaf node.  Effectively encoded by the header.
		}

		printf( "%f bytes per-node, without leaf data\n", (float)(nodeDataWithPadding)/(float)(nodeCount) );
		printf( "%f bytes per-node, without leaf data or padding\n", (float)(nodeDataNoPadding)/(float)(nodeCount) );
	}
}

int main( void )
{
	printf( "KdasmTest Starting.\n" );

	KdasmTest kdasmTest;
	KdasmAssembler kdasmAssembler;
	KdasmDisassembler kdasmDisassembler;

	kdasmAssembler.SetActivityCallback( KdasmTest::ActivityCallback, (void*)&kdasmTest, 100 );

	kdasmTest.TestRandom( kdasmAssembler, kdasmDisassembler );
	kdasmTest.TestLeavesAtRoot( kdasmAssembler, kdasmDisassembler );
	printf( "Done.\n" );

	return 0;
}

