#ifndef KDASM_VISUALIZER_H
#define KDASM_VISUALIZER_H
// Copyright (c) 2012 Adrian Johnston.  All rights reserved.
// See Copyright Notice in kdasm.h
// Project Homepage: http://code.google.com/p/kdasm/

#include <vector>
#include <map>

#include "kdasm.h"

// ----------------------------------------------------------------------------
// Output the page transition graph in a format graphviz can read.
//
// Example graphviz command line:
//   "C:\Program Files (x86)\Graphviz 2.28\bin\dot" -Tpng kdasmgraph.dot -o graph.png

class KdasmVisualizer
{
public:
    KdasmVisualizer( void );
    void Visualize( KdasmEncoding* encodingRoot, FILE* graph );

private:
    struct SubpageRecord
    {
        SubpageRecord( intptr_t index, intptr_t linkCost ) : m_index( index ), m_linkCost( linkCost ) { }
        intptr_t m_index;
        int      m_linkCost;
    };
    
    struct PageRecord
    {
        PageRecord( void ) { m_nodeCount = 0; }
        intptr_t                   m_nodeCount;
        std::vector<SubpageRecord> m_subpages;
    };
    
    void VisualizeEncoding( KdasmEncoding* encoding, intptr_t treeIndex );
    void VisualizeLeavesFar( KdasmEncoding* encoding );
    void VisualizeLeaves( KdasmEncoding* encoding, intptr_t leafCount );

    void Node( KdasmEncoding* node );
    void FarNode( KdasmEncoding* node, KdasmEncoding* subnode, int linkCost );

    intptr_t                       m_pageAddressMask;
    KdasmEncoding*                 m_encodingRoot;
    std::map<intptr_t, PageRecord> m_pageRecords;
};

#endif // KDASM_ASSEMBLER_H
