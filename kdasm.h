#ifndef KDASM_H
#define KDASM_H

// Copyright (c) 2012 Adrian Johnston.  All rights reserved.
// 
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Kdasm: k-d tree compressor with efficient cache-optimized runtime.

typedef unsigned short KdasmU16;

// ----------------------------------------------------------------------------
// KdasmEncoding is an encoding of a cutting plane, a jump statement or leaves.

class KdasmEncoding
{
public:
    enum {
        NORMAL_X                            = 0x0000, 
        NORMAL_Y                            = 0x0001,
        NORMAL_Z		                    = 0x0002,
        NORMAL_OPCODE                       = 0x0003,
        NORMAL_MASK                         = 0x0003,
        OPCODE_LEAVES                       = 0x0000,
        OPCODE_LEAVES_FAR                   = 0x0004,
        OPCODE_JUMP                         = 0x0008,
        OPCODE_JUMP_FAR                     = 0x000c,
        // With a distance length of 1 the value must fit in DISTANCE_IMMEDIATE_MASK and be less than DISTANCE_IMMEDIATE_MAX.
        DISTANCE_IMMEDIATE_MASK             = 0xfff0,
        DISTANCE_IMMEDIATE_MAX              = 0xffe0,
        // Due to quantization, a cutting plane has this width.
        DISTANCE_IMMEDIATE_PLANE_WIDTH      = 0x0010,
        // With a distance length greater than 1 this is the max value of the first KdasmU16.
        DISTANCE_PREFIX_MAX                 = 0x001f,
        LEAF_WORD_LENGTH_MAX                = 0x001f,
        TREE_INDEX_MAX                      = 0x001f,
        IMMEDIATE_OFFSET_MAX                = 0x03ff,   // Max absolute value.  Negative values allowed.
        LEAF_COUNT_OVERFLOW                 = 0xffff,    // An embedded header in the leaf data is required beyond this.
        PAD_VALUE                           = 0xcccc    // Impossible x axis cut with both stop bits set.
    };

    KdasmU16 GetRaw( void ) const                   { return m_word; }
    KdasmU16 GetNomal( void ) const                 { return m_word & (KdasmU16)NORMAL_MASK; }
    bool GetStop0( void ) const                     { return (m_word & (KdasmU16)STOP_BIT_0) != (KdasmU16)0; }
    bool GetStop1( void ) const                     { return (m_word & (KdasmU16)STOP_BIT_1) != (KdasmU16)0; }
    KdasmU16 GetDistanceImmediate( void ) const     { return m_word & (KdasmU16)DISTANCE_IMMEDIATE_MASK; }
    KdasmU16 GetDistancePrefix( void ) const        { return m_word >> DISTANCE_PREFIX_SHIFT; }
    KdasmU16 GetOpcode( void ) const                { return m_word & (KdasmU16)OPCODE_MASK; }
    bool GetIsImmediateOffset( void ) const         { return (m_word & (KdasmU16)IMMEDIATE_BIT) != (KdasmU16)0; }
    KdasmU16 GetImmediateOffset( void ) const       { return m_word >> IMMEDIATE_SHIFT; }
    KdasmU16 GetFarWordsCount( void ) const         { return (m_word & (KdasmU16)WORDS_COUNT_MASK) >> WORDS_COUNT_SHIFT; }
    KdasmU16 GetFarWordsOffset( void ) const        { return m_word >> WORDS_OFFSET_SHIFT; }
    KdasmU16 GetOffset( void ) const                { return (m_word & (KdasmU16)OFFSET_MASK) >> OFFSET_SHIFT; }
    KdasmU16 GetLength( void ) const                { return m_word >> LENGTH_SHIFT; }
    KdasmU16 GetTreeIndexStart( void ) const        { return m_word >> TREE_INDEX_START_SHIFT; }

    void SetRaw( KdasmU16 x )                       { m_word = x; }
    KdasmU16* Ptr( void )                           { return &m_word; }
    void SetNomal( KdasmU16 n )                     { m_word = (m_word & ~(KdasmU16)NORMAL_MASK) | ((KdasmU16)NORMAL_MASK & n); }
    void SetStop0( bool b )                         { SetBool( b, STOP_BIT_0 ); }
    void SetStop1( bool b )                         { SetBool( b, STOP_BIT_1 ); }
    void SetDistanceImmediate( KdasmU16 d )         { m_word = (m_word & ~(KdasmU16)DISTANCE_IMMEDIATE_MASK) | ((KdasmU16)DISTANCE_IMMEDIATE_MASK & d); }
    void SetDistancePrefix( KdasmU16 n )            { SetNShift( n, DISTANCE_PREFIX_SHIFT ); }
    void SetOpcode( KdasmU16 op )                   { m_word = (m_word & ~(KdasmU16)OPCODE_MASK) | ((KdasmU16)OPCODE_MASK & op); }
    void SetIsImmediateOffset( bool b )             { SetBool( b, IMMEDIATE_BIT ); }
    void SetImmediateOffset( KdasmU16 n )           { SetNShift( n, IMMEDIATE_SHIFT ); }
    void SetFarWordsCount( KdasmU16 n )             { SetNShiftAndMask( n, WORDS_COUNT_SHIFT, WORDS_COUNT_MASK ); }
    void SetFarWordsOffset( KdasmU16 n )            { SetNShift( n, WORDS_OFFSET_SHIFT ); }
    void SetOffset( KdasmU16 o )                    { SetNShiftAndMask( o, OFFSET_SHIFT, OFFSET_MASK ); }
    void SetLength( KdasmU16 n )                    { SetNShift( n, LENGTH_SHIFT ); }
    void SetTreeIndexStart( KdasmU16 n )            { SetNShift( n, TREE_INDEX_START_SHIFT ); }

    // Convert floating point value between 0 and 1 to fixed point.
    static KdasmU16 PackDistanceImmediate( float d01 )
    {
        d01 = (d01 < 0.0f) ? 0.0f : ((d01 > 1.0f) ? 1.0f : d01); // clamp [0..1]
        return DISTANCE_IMMEDIATE_MASK & (KdasmU16)( d01 * (float)DISTANCE_IMMEDIATE_MAX );
    }

    // For Distance Length == 1.  Returns the sides of the quantized cutting plane.
    void UnpackDistanceImmediate( float* d01less, float* d01greater ) const
    {
        float do01 = (float)GetDistanceImmediate() * (1.0f / (float)(DISTANCE_IMMEDIATE_MAX));
        *d01less = do01;
        *d01greater = do01 + ((float)DISTANCE_IMMEDIATE_PLANE_WIDTH/(float)DISTANCE_IMMEDIATE_MAX + 2.0f*FLT_EPSILON);
    }

    // For distanceLength > 1.  The number of words used to encode distance is constant.
    template<int distanceLength> intptr_t UnpackDistance( void ) const
    {
        intptr_t distance = UnpackUnsignedWords( distanceLength - 1, UnpackOffset() );
        return distance | ( (intptr_t)( m_word & (KdasmU16)DISTANCE_PREFIX_MASK ) << ( 16 * ( distanceLength - 1 ) - DISTANCE_PREFIX_SHIFT ) );
    }

    intptr_t UnpackOffset( void ) const
    {
        return ( (intptr_t)GetOffset() ^ (intptr_t)OFFSET_SIGN_BIT ) - (intptr_t)OFFSET_SIGN_BIT;
    }

    intptr_t UnpackFarOffset( void ) const
    {
        // Uses "x^high_bit-high_bit" trick to sign extend the high bit.
        if( GetIsImmediateOffset() )
        {
            return ( (intptr_t)GetImmediateOffset() ^ (intptr_t)IMMEDIATE_SIGN_BIT ) - (intptr_t)IMMEDIATE_SIGN_BIT;
        }
        return UnpackSignedWords( GetFarWordsCount(), ( GetFarWordsOffset() ^ WORDS_OFFSET_SIGN_BIT ) - WORDS_OFFSET_SIGN_BIT );
    }

private:
    enum {
        OPCODE_MASK                 = 0x000c,    // NORMAL_OPCODE
        STOP_BIT_0                  = 0x0004,    // NORMAL_X/Y/Z
        STOP_BIT_1                  = 0x0008,
        IMMEDIATE_BIT               = 0x0010,    // OPCODE_LEAVES_FAR, OPCODE_JUMP_FAR
        IMMEDIATE_SHIFT             = 5,
        IMMEDIATE_SIGN_BIT          = 0x0400,
        WORDS_COUNT_SHIFT           = 5,
        WORDS_COUNT_MASK            = 0x00e0,
        WORDS_OFFSET_SHIFT          = 8,
        WORDS_OFFSET_SIGN_BIT       = 0x0100,
        OFFSET_SHIFT                = 4,        // OPCODE_LEAVES, OPCODE_JUMP
        OFFSET_MASK                 = 0x07f0,
        OFFSET_SIGN_BIT             = 0x0040,
        DISTANCE_PREFIX_MASK        = 0xf800,    // NORMAL_X/Y/Z
        DISTANCE_PREFIX_SHIFT       = 11,
        LENGTH_SHIFT                = 11,        // OPCODE_LEAVES
        TREE_INDEX_START_SHIFT      = 11,        // OPCODE_JUMP
    };

    void SetNShift( KdasmU16 n, int shift )
    {
        m_word = (m_word & (KdasmU16)((1 << shift) - 1)) | (KdasmU16)(n << shift);
    }

    void SetNShiftAndMask( KdasmU16 n, int shift, KdasmU16 mask )
    {
        m_word = (m_word & ~mask) | ((n << shift) & mask);
    }

    void SetBool( bool b, KdasmU16 mask )
    {
        m_word = (m_word & ~mask) | (b ? mask : 0);
    }

    intptr_t UnpackUnsignedWords( KdasmU16 wordCount, KdasmU16 wordsOffset ) const
    {
        const KdasmU16* words = &m_word + wordsOffset;
        intptr_t result = (intptr_t)*words;
        while( --wordCount != 0 )
        {
            result <<= 16;
            result |= (intptr_t)*++words;
        }
        return result;
    }

    intptr_t UnpackSignedWords( KdasmU16 wordCount, KdasmU16 wordsOffset ) const
    {
        intptr_t signBit = (intptr_t)1 << (wordCount * 16 - 1);
        return ( UnpackUnsignedWords( wordCount, wordsOffset ) ^ signBit ) - signBit;
    }

    KdasmU16 m_word;
};

// ----------------------------------------------------------------------------
// Inserted at the beginning of the first page.

class KdasmEncodingHeader
{
public:
    enum {
        DISTANCE_LENGTH_MAX = 0x0007,
        HEADER_LENGTH       = 2
    };

    enum PageBits {
        PAGE_BITS_32B  = 5,
        PAGE_BITS_64B  = 6,
        PAGE_BITS_128B = 7
    };

    bool VersionCheck( void ) const             { return m_words[0] == VERSION_1; }
    // Number of KdasmU16 including prefix, or 1 for immediate storage.
    KdasmU16 GetDistanceLength( void ) const    { return m_words[1] & (KdasmU16)DISTANCE_LENGTH_MASK; }
    bool IsLeavesAtRoot( void ) const           { return ( m_words[1] & (KdasmU16)FLAG_LEAVES_AT_ROOT ) != 0; }
    PageBits GetPageBits( void ) const          { return (PageBits)((m_words[1] & (KdasmU16)PAGE_BITS_MASK) >> PAGE_BITS_SHIFT); }

    // internal
    void Reset( void )							{ m_words[0] = VERSION_1; m_words[1] = 0; }
    void SetDistanceLength( KdasmU16 dl )       { m_words[1] |= (KdasmU16)DISTANCE_LENGTH_MASK & dl; }
    void SetIsLeavesAtRoot( bool b )            { m_words[1] |= b ? (KdasmU16)FLAG_LEAVES_AT_ROOT : 0; }
    void SetPageBits( PageBits pb )             { m_words[1] |= (KdasmU16)PAGE_BITS_MASK & ((KdasmU16)pb << PAGE_BITS_SHIFT); }
    KdasmU16 GetRaw( int index ) const          { return m_words[index]; }

private:
    union PortabilityCheck
    {
        char size_of_unsigned_short_incorrect[sizeof(KdasmU16) == 2];
        char size_of_int_incorrect[sizeof(int) >= 4];
        char size_of_encoding_incorrect[sizeof(KdasmEncoding) == 2];
        char sign_extended_shift_incorrect[((-1)>>1) == (-1)];
    };

    enum {
        VERSION_1            = 0x316b,    // 'k','1'
        DISTANCE_LENGTH_MASK = 0x0007,
        FLAG_LEAVES_AT_ROOT  = 0x0008,    // Starts with a OPCODE_LEAVES_FAR reference.
        PAGE_BITS_MASK       = 0x00f0,
        PAGE_BITS_SHIFT      = 4,
    };

    KdasmU16 m_words[HEADER_LENGTH];
};

#endif // KDASM_H

