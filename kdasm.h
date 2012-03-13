#ifndef KDASM_H
#define KDASM_H

// Kdasm: k-d tree compressor with efficient cache-optimized runtime.

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

// ----------------------------------------------------------------------------
// KdasmEncoding is an encoding of a cutting plane, a jump statement or leaves.
// It resembles a primitive 16-bit assembly language that relies heavily on
// variable length encoding.

class KdasmEncoding
{
public:
	enum {
		PAGE_BITS16						= 6,		// Up to 128 byte pages.
		NORMAL_X						= 0x0000, 
		NORMAL_Y						= 0x0001,
		NORMAL_Z						= 0x0002,
		NORMAL_OPCODE					= 0x0003,	// Indicates an OPCODE_*
		NORMAL_MASK						= 0x0003,
		// With a distance length of 1 the value must fit in DISTANCE_IMMEDIATE_MASK and be less than DISTANCE_IMMEDIATE_MAX.
		DISTANCE_IMMEDIATE_MASK			= 0xfff0,
		DISTANCE_IMMEDIATE_MAX			= 0xffe0,
		// Due to quantization, a cutting plane has this width.
		DISTANCE_IMMEDIATE_PLANE_WIDTH	= 0x0010,
		// With a distance length greater than 1 this is the max value of the first unsigned short.
		DISTANCE_PREFIX_MAX				= 0x001f,
		LENGTH_MAX						= 0x001f,
		TREE_INDEX_MAX					= 0x001f,
		// Operation codes: Appear with normal NORMAL_OPCODE.
		OPCODE_LEAVES					= 0x0000,	// operands: offset + length
		OPCODE_LEAVES_FAR				= 0x0004,	// operands: immediate or num words + words offset
		OPCODE_JUMP						= 0x0008,	// operands: offset + tree index
		OPCODE_JUMP_FAR					= 0x000c,	// operands: immediate or num words + words offset
		IMMEDIATE_MAX					= 0x03ff,   // Max absolute value.  Negative values allowed.
		LEAF_COUNT_OVERFLOW				= 0xffff,	// An embedded header in the leaf data is required beyond this.
		PAD_VALUE						= 0xcccc	// Illegal x axis cut with both stop bits set.
	};

	unsigned short GetRaw( void ) const						{ return m_word; }
	unsigned short GetNomal( void ) const					{ return m_word & (unsigned short)NORMAL_MASK; }
	bool GetStop0( void ) const								{ return (m_word & (unsigned short)STOP_BIT_0) != (unsigned short)0; }
	bool GetStop1( void ) const								{ return (m_word & (unsigned short)STOP_BIT_1) != (unsigned short)0; }
	unsigned short GetDistanceImmediate( void ) const		{ return m_word & (unsigned short)DISTANCE_IMMEDIATE_MASK; }
	unsigned short GetDistancePrefix( void ) const			{ return m_word >> DISTANCE_PREFIX_SHIFT; }
	unsigned short GetOpcode( void ) const					{ return m_word & (unsigned short)OPCODE_MASK; }
	// Used by OPCODE_LEAVES_FAR and OPCODE_JUMP_FAR
	bool GetIsOperandImmediate( void ) const				{ return (m_word & (unsigned short)IMMEDIATE_BIT) != (unsigned short)0; }
	unsigned short GetOperandImmediate( void ) const		{ return m_word >> IMMEDIATE_SHIFT; }
	unsigned short GetOperandWordsCount( void ) const		{ return (m_word & (unsigned short)WORDS_COUNT_MASK) >> WORDS_COUNT_SHIFT; }
	unsigned short GetOperandWordsOffset( void ) const		{ return m_word >> WORDS_OFFSET_SHIFT; }
	// Used by OPCODE_LEAVES and OPCODE_JUMP, and extended distance storage.
	unsigned short GetOperandOffset( void ) const			{ return (m_word & (unsigned short)OFFSET_MASK) >> OFFSET_SHIFT; }
	// Used by OPCODE_LEAVES
	unsigned short GetOperandLength( void ) const			{ return m_word >> LENGTH_SHIFT; }
	// Used by OPCODE_JUMP
	unsigned short GetOperandTreeIndexStart( void ) const	{ return m_word >> TREE_INDEX_START_SHIFT; }

	void SetRaw( unsigned short x )							{ m_word = x; }
	unsigned short* Ptr( void )								{ return &m_word; }
	void SetNomal( unsigned short n )						{ m_word = (m_word & ~(unsigned short)NORMAL_MASK) | ((unsigned short)NORMAL_MASK & n); }
	void SetStop0( bool b )									{ SetBool( b, STOP_BIT_0 ); }
	void SetStop1( bool b )									{ SetBool( b, STOP_BIT_1 ); }
	void SetDistanceImmediate( unsigned short d )			{ m_word = (m_word & ~(unsigned short)DISTANCE_IMMEDIATE_MASK) | ((unsigned short)DISTANCE_IMMEDIATE_MASK & d); }
	void SetDistancePrefix( unsigned short n )				{ SetNShift( n, DISTANCE_PREFIX_SHIFT ); }
	void SetOpcode( unsigned short op )						{ m_word = (m_word & ~(unsigned short)OPCODE_MASK) | ((unsigned short)OPCODE_MASK & op); }
	void SetIsOperandImmediate( bool b )					{ SetBool( b, IMMEDIATE_BIT ); }
	void SetOperandImmediate( unsigned short n )			{ SetNShift( n, IMMEDIATE_SHIFT ); }
	void SetOperandWordsCount( unsigned short n )			{ SetNShiftAndMask( n, WORDS_COUNT_SHIFT, WORDS_COUNT_MASK ); }
	void SetOperandWordsOffset( unsigned short n )			{ SetNShift( n, WORDS_OFFSET_SHIFT ); }
	void SetOperandOffset( unsigned short o )				{ SetNShiftAndMask( o, OFFSET_SHIFT, OFFSET_MASK ); }
	void SetOperandLength( unsigned short n )				{ SetNShift( n, LENGTH_SHIFT ); }
	void SetOperandTreeIndexStart( unsigned short n )		{ SetNShift( n, TREE_INDEX_START_SHIFT ); }

	// Convert floating point value between 0 and 1 to fixed point.
	static unsigned short PackDistanceImmediate( float d01 )
	{
		d01 = (d01 < 0.0f) ? 0.0f : ((d01 > 1.0f) ? 1.0f : d01); // clamp [0..1]
		return DISTANCE_IMMEDIATE_MASK & (unsigned short)( d01 * (float)DISTANCE_IMMEDIATE_MAX );
	}

	// For distanceLength == 1.  Returns the sides of the quantized cutting plane.
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
		return distance | ( (intptr_t)( m_word & (unsigned short)DISTANCE_PREFIX_MASK ) << ( 16 * ( distanceLength - 1 ) - DISTANCE_PREFIX_SHIFT ) );
	}

	intptr_t UnpackOffset( void ) const
	{
		return ( (intptr_t)GetOperandOffset() ^ (intptr_t)OFFSET_SIGN_BIT ) - (intptr_t)OFFSET_SIGN_BIT;
	}

	intptr_t UnpackFarOffset( void ) const
	{
		// Uses "x^high_bit-high_bit" trick to sign extend the high bit.
		if( GetIsOperandImmediate() )
		{
			return ( (intptr_t)GetOperandImmediate() ^ (intptr_t)IMMEDIATE_SIGN_BIT ) - (intptr_t)IMMEDIATE_SIGN_BIT;
		}
		return UnpackSignedWords( GetOperandWordsCount(), ( GetOperandWordsOffset() ^ WORDS_OFFSET_SIGN_BIT ) - WORDS_OFFSET_SIGN_BIT );
	}

private:
	enum {
		OPCODE_MASK				= 0x000c,	// NORMAL_OPCODE
		STOP_BIT_0				= 0x0004,	// NORMAL_X/Y/Z
		STOP_BIT_1				= 0x0008,
		IMMEDIATE_BIT			= 0x0010,	// OPCODE_LEAVES_FAR, OPCODE_JUMP_FAR
		IMMEDIATE_SHIFT			= 5,
		IMMEDIATE_SIGN_BIT		= 0x0400,
		WORDS_COUNT_SHIFT		= 5,
		WORDS_COUNT_MASK		= 0x00e0,
		WORDS_OFFSET_SHIFT		= 8,
		WORDS_OFFSET_SIGN_BIT	= 0x0100,
		OFFSET_SHIFT			= 4,		// OPCODE_LEAVES, OPCODE_JUMP
		OFFSET_MASK				= 0x07f0,
		OFFSET_SIGN_BIT			= 0x0040,
		DISTANCE_PREFIX_MASK	= 0xf800,	// NORMAL_X/Y/Z
		DISTANCE_PREFIX_SHIFT	= 11,
		LENGTH_SHIFT			= 11,		// OPCODE_LEAVES
		TREE_INDEX_START_SHIFT	= 11,		// OPCODE_JUMP
	};

	void SetNShift( unsigned short n, int shift )
	{
		m_word = (m_word & (unsigned short)((1 << shift) - 1)) | (unsigned short)(n << shift);
	}

	void SetNShiftAndMask( unsigned short n, int shift, unsigned short mask )
	{
		m_word = (m_word & ~mask) | ((n << shift) & mask);
	}

	void SetBool( bool b, unsigned short mask )
	{
		m_word = (m_word & ~mask) | (b ? mask : 0);
	}

	intptr_t UnpackUnsignedWords( unsigned short wordCount, unsigned short wordsOffset ) const
	{
		const unsigned short* words = &m_word + wordsOffset;
		intptr_t result = (intptr_t)*words;
		while( --wordCount != 0 )
		{
			result <<= 16;
			result |= (intptr_t)*++words;
		}
		return result;
	}

	intptr_t UnpackSignedWords( unsigned short wordCount, unsigned short wordsOffset ) const
	{
		intptr_t signBit = (intptr_t)1 << (wordCount * 16 - 1);
		return ( UnpackUnsignedWords( wordCount, wordsOffset ) ^ signBit ) - signBit;
	}

	unsigned short m_word;
};

// ----------------------------------------------------------------------------
// Inserted at the beginning of the first page.

class KdasmEncodingHeader
{
public:
	enum {
		DISTANCE_LENGTH_MAX				= 0x0007,
		HEADER_LENGTH					= 2
	};

	enum PageBits {
		PAGE_BITS_32B  = 5,
		PAGE_BITS_64B  = 6,
		PAGE_BITS_128B = 7
	};

	bool VersionCheck( void ) const							{ return m_words[0] == VERSION_1; }
	// Number of unsigned short including prefix, or 1 for immediate storage.
	unsigned short GetDistanceLength( void ) const			{ return m_words[1] & (unsigned short)DISTANCE_LENGTH_MASK; }
	bool IsLeavesAtRoot( void ) const						{ return ( m_words[1] & (unsigned short)FLAG_LEAVES_AT_ROOT ) != 0; }
	PageBits GetPageBits( void ) const						{ return (PageBits)((m_words[1] & (unsigned short)PAGE_BITS_MASK) >> PAGE_BITS_SHIFT); }

	// internal
	void Reset( void )										{ m_words[0] = VERSION_1; m_words[1] = 0; }
	void SetDistanceLength( unsigned short dl )				{ m_words[1] |= (unsigned short)DISTANCE_LENGTH_MASK & dl; }
	void SetIsLeavesAtRoot( bool b )						{ m_words[1] |= b ? (unsigned short)FLAG_LEAVES_AT_ROOT : 0; }
	void SetPageBits( PageBits pb )							{ m_words[1] |= (unsigned short)PAGE_BITS_MASK & ((unsigned short)pb << PAGE_BITS_SHIFT); }
	unsigned short GetRaw( int index ) const				{ return m_words[index]; }

private:
	union PortabilityCheck
	{
		char size_of_unsigned_short_incorrect[sizeof(unsigned short) == 2];
		char size_of_int_incorrect[sizeof(int) == 4];
		char size_of_encoding_incorrect[sizeof(KdasmEncoding) == 2];
		char sign_extended_shift_incorrect[((-1)>>1) == (-1)];
	};

	enum {
		VERSION_1						= 0x316b,	// 'k1' as ascii.
		DISTANCE_LENGTH_MASK			= 0x0007,
		FLAG_LEAVES_AT_ROOT				= 0x0008,	// Starts with a OPCODE_LEAVES_FAR reference.
		PAGE_BITS_MASK					= 0x00f0,
		PAGE_BITS_SHIFT					= 4,
	};

	unsigned short m_words[HEADER_LENGTH];
};

#endif // KDASM_H

