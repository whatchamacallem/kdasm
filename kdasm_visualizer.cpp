// Copyright (c) 2012 Adrian Johnston.  All rights reserved.
// See Copyright Notice in kdasm.h
// Project Homepage: http://code.google.com/p/kdasm/

#include <algorithm>
#include <map>
#include "kdasm_visualizer.h"

KdasmVisualizer::KdasmVisualizer( void )
{
    m_pageAddressMask = 0;
    m_encodingRoot = NULL;
}

void KdasmVisualizer::Visualize( KdasmEncoding* encodingRoot, FILE* graph )
{
    KdasmEncodingHeader* header = (KdasmEncodingHeader*)encodingRoot;
    if( !header->VersionCheck() || !graph )
    {
        return;
    }

    m_pageAddressMask = ~(((intptr_t)1 << (header->GetPageBits() - 1)) - 1);

    // Used to calcualte cache-misses per-leaf node.
    m_encodingRoot = encodingRoot;

    if( header->IsLeavesAtRoot() )
    {
        VisualizeLeavesFar( encodingRoot + KdasmEncodingHeader::HEADER_LENGTH );
    }
    else
    {
        VisualizeEncoding( encodingRoot + KdasmEncodingHeader::HEADER_LENGTH, 0 );
    }

    fprintf( graph, "digraph G {\n" );

    std::map<intptr_t, PageRecord>::iterator i;
    for( i=m_pageRecords.begin(); i != m_pageRecords.end(); ++i )
    {
        fprintf( graph, "p%d [label=\"%d\"];\n", i->first, i->second.m_nodeCount );
    }

    std::vector<SubpageRecord>::iterator j;
    for( i=m_pageRecords.begin(); i != m_pageRecords.end(); ++i )
    {
        for( j=i->second.m_subpages.begin(); j != i->second.m_subpages.end(); ++j )
        {
            if( j->m_linkCost != 0 )
            {
                fprintf( graph, "p%d -> p%d [ label = \"%d\" ];\n", i->first, j->m_index, j->m_linkCost );
            }
            else
            {
                fprintf( graph, "p%d -> p%d;\n", i->first, j->m_index );
            }
        }
    }

    fprintf( graph, "}\n" );

    m_pageRecords.clear();
}

void KdasmVisualizer::VisualizeEncoding( KdasmEncoding* encoding, intptr_t treeIndex )
{
    KdasmU16 normal = encoding->GetNomal();
    if( normal == KdasmEncoding::NORMAL_OPCODE )
    {
        switch( encoding->GetOpcode() )
        {
            case KdasmEncoding::OPCODE_LEAVES:
            {
                Node( encoding );
                return;
            }
            case KdasmEncoding::OPCODE_LEAVES_FAR:
            {
                intptr_t offset = encoding->GetFarOffset();
                KdasmEncoding* encodingOffset = encoding + offset;

                FarNode( encoding, encodingOffset, encoding->GetIsImmediateOffset() ? 0 : encoding->GetFarWordsCount() );
                VisualizeLeavesFar( encodingOffset );
                return;
            }
            case KdasmEncoding::OPCODE_JUMP:
            {
                intptr_t offset = encoding->GetOffsetSigned();
                intptr_t treeIndexStart = (intptr_t)encoding->GetTreeIndexStart();

                VisualizeEncoding( encoding + offset, treeIndexStart );
                return;
            }
            case KdasmEncoding::OPCODE_JUMP_FAR:
            {
                intptr_t offset = encoding->GetFarOffset();
                KdasmEncoding* encodingOffset = encoding + offset;

                FarNode( encoding, encodingOffset, encoding->GetIsImmediateOffset() ? 0 : encoding->GetFarWordsCount() );
                VisualizeEncoding( encodingOffset, 0 );
                return;
            }
        }
    }
    else
    {
        Node( encoding );

        if( !encoding->GetStop0() )
        {
            KdasmEncoding* destinationEncoding = encoding + ( treeIndex + 1 );
            VisualizeEncoding( destinationEncoding, treeIndex * 2 + 1 );
        }
        if( !encoding->GetStop1() )
        {
            KdasmEncoding* destinationEncoding = encoding + ( treeIndex + 2 );
            VisualizeEncoding( destinationEncoding, treeIndex * 2 + 2 );
        }
    }
}

void KdasmVisualizer::VisualizeLeavesFar( KdasmEncoding* encoding )
{
    Node( encoding );
}

void KdasmVisualizer::Node( KdasmEncoding* node )
{
    intptr_t nodePage = (intptr_t)(node - m_encodingRoot) & m_pageAddressMask;
    m_pageRecords[nodePage].m_nodeCount += 1;
}

void KdasmVisualizer::FarNode( KdasmEncoding* node, KdasmEncoding* subnode, int linkCost )
{
    intptr_t nodePage = (intptr_t)(node - m_encodingRoot) & m_pageAddressMask;
    intptr_t subnodePage = (intptr_t)(subnode - m_encodingRoot) & m_pageAddressMask;
    if( nodePage != subnodePage )
    {
        m_pageRecords[nodePage].m_subpages.push_back( SubpageRecord( subnodePage, linkCost ) );
    }
}

