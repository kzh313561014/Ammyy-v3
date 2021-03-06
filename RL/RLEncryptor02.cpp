#include "stdafx.h"
#include "RLEncryptor02.h"

// The algorithm is very security, it has 2 layers
// 1) first layer  - modified SEAL 3.0 with 160 bits key, ByteReverse() was modifyed
// 2) second layer - designed by maximp, Rotation input stream with state variable
// 
// it's very fast about 9 sec for Encrypt+Decrypt 1024^3 bytes on Notebook Intel 2.10 Ghz, 
// about 3 times faster than AES-128


RLEncryptor02::RLEncryptor02()
{
}

RLEncryptor02::~RLEncryptor02()
{
}


void RLEncryptor02::Copy(const RLEncryptor02& src)
{
	m_counter	= src.m_counter;
	m_position	= src.m_position;
	m_lastIndex	= src.m_lastIndex;
	memcpy(H, src.H, sizeof(H));
	memcpy(Z, src.Z, sizeof(Z));
	memcpy(D, src.D, sizeof(D));
	memcpy(S, src.S, sizeof(S));
	memcpy(T, src.T, sizeof(T));
	memcpy(R, src.R, sizeof(R));
	memcpy(m_buffer, src.m_buffer, sizeof(m_buffer));
	m_prevByte = src.m_prevByte;
	memcpy(m_rotation, src.m_rotation, sizeof(m_rotation));
}


// it can be used to fast recovery if Encoder was used less than for 4096 bytes
//
void RLEncryptor02::FastCopy(const RLEncryptor02& src)
{
	m_position = src.m_position;
	m_prevByte = src.m_prevByte;
}


inline static void FastXOR(UINT8 *input, UINT8 *mask, int count)
{
	int n = count / sizeof(int);

	for (; n>0; n--) {	
		*((int*)input) ^= *((int*)mask);
		input += sizeof(int);
		mask  += sizeof(int);
	}
	
	n = count % sizeof(int);

	for (; n>0; n--) {
		*input ^= *mask;
		input++;
		mask++;
	}	
}

//inline u32 rotlFixed(u32 x, u32 y) { return (x << y) | (x >> (32 - y)); }
//inline u32 rotrFixed(u32 x, u32 y) { return (x >> y) | (x << (32 - y)); }

//inline static u32 rotrFixed_8 (u32 x) { return _lrotr(x,8); }
//inline static u32 rotrFixed_16(u32 x) { return _lrotr(x,16); }
//inline static u32 rotrFixed_24(u32 x) { return _lrotr(x,24); }
//inline static u32 rotrFixed_9 (u32 x) { return _lrotr(x,9); }
//inline static u32 rotlFixed1 (u32 x) { return _lrotl(x,1); }
//inline static u32 rotlFixed5 (u32 x) { return _lrotl(x,5); }
//inline static u32 rotlFixed30(u32 x) { return _lrotl(x,30); }
//inline static u32 rotlFixed16(u32 x) { return _lrotl(x,16); }

#ifdef _WIN32
	#define rotlFixed1(x)   _rotl(x,1)
	#define rotlFixed5(x)   _rotl(x,5)
	#define rotlFixed30(x)  _rotl(x,30)
	#define rotlFixed16(x)  _rotl(x,16)
	#define rotrFixed_8(x)  _rotr(x,8)
	#define rotrFixed_16(x) _rotr(x,16)
	#define rotrFixed_24(x) _rotr(x,24)
	#define rotrFixed_9(x)  _rotr(x,9)
#else
	inline static UINT32 rotlFixed1  (UINT32 x) { return (x << 1)  | (x >> 31); }
	inline static UINT32 rotlFixed5  (UINT32 x) { return (x << 5)  | (x >> 27); }
	inline static UINT32 rotlFixed30 (UINT32 x) { return (x << 30) | (x >> 2);  }
	inline static UINT32 rotlFixed16 (UINT32 x) { return (x << 16) | (x >> 16); }
	inline static UINT32 rotrFixed_8 (UINT32 x) { return (x >> 8)  | (x << 24); }
	inline static UINT32 rotrFixed_16(UINT32 x) { return (x >> 16) | (x << 16); }
	inline static UINT32 rotrFixed_24(UINT32 x) { return (x >> 24) | (x << 8);  }
	inline static UINT32 rotrFixed_9 (UINT32 x) { return (x >> 9)  | (x << 23); }
#endif





#define K1  0x5A827999L                                 /* Rounds  0-19 */
#define K2  0x6ED9EBA1L                                 /* Rounds 20-39 */
#define K3  0x8F1BBCDCL                                 /* Rounds 40-59 */
#define K4  0xCA62C1D6L                                 /* Rounds 60-79 */

#define f1(x,y,z)   ( z ^ ( x & ( y ^ z ) ) )           /* Rounds  0-19 */
#define f2(x,y,z)   ( x ^ y ^ z )                       /* Rounds 20-39 */
#define f3(x,y,z)   ( ( x & y ) | ( z & ( x | y ) ) )   /* Rounds 40-59 */
#define f4(x,y,z)   ( x ^ y ^ z )                       /* Rounds 60-79 */

// from sha-1
void RLEncryptor02::SHATransform( UINT32 *digest, const UINT32 *data )
{
	register UINT32 A, B, C, D, E;

	#define expand(W,i) ( W[ i & 15 ] = rotlFixed1((W[i&15] ^ W[i-14&15] ^ W[i-8&15] ^ W[i-3&15])) )

	#define subRound(a, b, c, d, e, f, k, data) ( e += rotlFixed5(a) + f(b,c,d) + k + data, b = rotlFixed30(b))


	UINT32 eData[16];
	memcpy(eData, data, sizeof(eData));


	A = digest[0];
	B = digest[1];
	C = digest[2];
	D = digest[3];
	E = digest[4];

	// Heavy mangling, in 4 sub-rounds of 20 interations each.
	subRound( A, B, C, D, E, f1, K1, eData[  0 ] );
	subRound( E, A, B, C, D, f1, K1, eData[  1 ] );
	subRound( D, E, A, B, C, f1, K1, eData[  2 ] );
	subRound( C, D, E, A, B, f1, K1, eData[  3 ] );
	subRound( B, C, D, E, A, f1, K1, eData[  4 ] );
	subRound( A, B, C, D, E, f1, K1, eData[  5 ] );
	subRound( E, A, B, C, D, f1, K1, eData[  6 ] );
	subRound( D, E, A, B, C, f1, K1, eData[  7 ] );
	subRound( C, D, E, A, B, f1, K1, eData[  8 ] );
	subRound( B, C, D, E, A, f1, K1, eData[  9 ] );
	subRound( A, B, C, D, E, f1, K1, eData[ 10 ] );
	subRound( E, A, B, C, D, f1, K1, eData[ 11 ] );
	subRound( D, E, A, B, C, f1, K1, eData[ 12 ] );
	subRound( C, D, E, A, B, f1, K1, eData[ 13 ] );
	subRound( B, C, D, E, A, f1, K1, eData[ 14 ] );
	subRound( A, B, C, D, E, f1, K1, eData[ 15 ] );
	subRound( E, A, B, C, D, f1, K1, expand( eData, 16 ) );
	subRound( D, E, A, B, C, f1, K1, expand( eData, 17 ) );
	subRound( C, D, E, A, B, f1, K1, expand( eData, 18 ) );
	subRound( B, C, D, E, A, f1, K1, expand( eData, 19 ) );

	subRound( A, B, C, D, E, f2, K2, expand( eData, 20 ) );
	subRound( E, A, B, C, D, f2, K2, expand( eData, 21 ) );
	subRound( D, E, A, B, C, f2, K2, expand( eData, 22 ) );
	subRound( C, D, E, A, B, f2, K2, expand( eData, 23 ) );
	subRound( B, C, D, E, A, f2, K2, expand( eData, 24 ) );
	subRound( A, B, C, D, E, f2, K2, expand( eData, 25 ) );
	subRound( E, A, B, C, D, f2, K2, expand( eData, 26 ) );
	subRound( D, E, A, B, C, f2, K2, expand( eData, 27 ) );
	subRound( C, D, E, A, B, f2, K2, expand( eData, 28 ) );
	subRound( B, C, D, E, A, f2, K2, expand( eData, 29 ) );
	subRound( A, B, C, D, E, f2, K2, expand( eData, 30 ) );
	subRound( E, A, B, C, D, f2, K2, expand( eData, 31 ) );
	subRound( D, E, A, B, C, f2, K2, expand( eData, 32 ) );
	subRound( C, D, E, A, B, f2, K2, expand( eData, 33 ) );
	subRound( B, C, D, E, A, f2, K2, expand( eData, 34 ) );
	subRound( A, B, C, D, E, f2, K2, expand( eData, 35 ) );
	subRound( E, A, B, C, D, f2, K2, expand( eData, 36 ) );
	subRound( D, E, A, B, C, f2, K2, expand( eData, 37 ) );
	subRound( C, D, E, A, B, f2, K2, expand( eData, 38 ) );
	subRound( B, C, D, E, A, f2, K2, expand( eData, 39 ) );

	subRound( A, B, C, D, E, f3, K3, expand( eData, 40 ) );
	subRound( E, A, B, C, D, f3, K3, expand( eData, 41 ) );
	subRound( D, E, A, B, C, f3, K3, expand( eData, 42 ) );
	subRound( C, D, E, A, B, f3, K3, expand( eData, 43 ) );
	subRound( B, C, D, E, A, f3, K3, expand( eData, 44 ) );
	subRound( A, B, C, D, E, f3, K3, expand( eData, 45 ) );
	subRound( E, A, B, C, D, f3, K3, expand( eData, 46 ) );
	subRound( D, E, A, B, C, f3, K3, expand( eData, 47 ) );
	subRound( C, D, E, A, B, f3, K3, expand( eData, 48 ) );
	subRound( B, C, D, E, A, f3, K3, expand( eData, 49 ) );
	subRound( A, B, C, D, E, f3, K3, expand( eData, 50 ) );
	subRound( E, A, B, C, D, f3, K3, expand( eData, 51 ) );
	subRound( D, E, A, B, C, f3, K3, expand( eData, 52 ) );
	subRound( C, D, E, A, B, f3, K3, expand( eData, 53 ) );
	subRound( B, C, D, E, A, f3, K3, expand( eData, 54 ) );
	subRound( A, B, C, D, E, f3, K3, expand( eData, 55 ) );
	subRound( E, A, B, C, D, f3, K3, expand( eData, 56 ) );
	subRound( D, E, A, B, C, f3, K3, expand( eData, 57 ) );
	subRound( C, D, E, A, B, f3, K3, expand( eData, 58 ) );
	subRound( B, C, D, E, A, f3, K3, expand( eData, 59 ) );

	subRound( A, B, C, D, E, f4, K4, expand( eData, 60 ) );
	subRound( E, A, B, C, D, f4, K4, expand( eData, 61 ) );
	subRound( D, E, A, B, C, f4, K4, expand( eData, 62 ) );
	subRound( C, D, E, A, B, f4, K4, expand( eData, 63 ) );
	subRound( B, C, D, E, A, f4, K4, expand( eData, 64 ) );
	subRound( A, B, C, D, E, f4, K4, expand( eData, 65 ) );
	subRound( E, A, B, C, D, f4, K4, expand( eData, 66 ) );
	subRound( D, E, A, B, C, f4, K4, expand( eData, 67 ) );
	subRound( C, D, E, A, B, f4, K4, expand( eData, 68 ) );
	subRound( B, C, D, E, A, f4, K4, expand( eData, 69 ) );
	subRound( A, B, C, D, E, f4, K4, expand( eData, 70 ) );
	subRound( E, A, B, C, D, f4, K4, expand( eData, 71 ) );
	subRound( D, E, A, B, C, f4, K4, expand( eData, 72 ) );
	subRound( C, D, E, A, B, f4, K4, expand( eData, 73 ) );
	subRound( B, C, D, E, A, f4, K4, expand( eData, 74 ) );
	subRound( A, B, C, D, E, f4, K4, expand( eData, 75 ) );
	subRound( E, A, B, C, D, f4, K4, expand( eData, 76 ) );
	subRound( D, E, A, B, C, f4, K4, expand( eData, 77 ) );
	subRound( C, D, E, A, B, f4, K4, expand( eData, 78 ) );
	subRound( B, C, D, E, A, f4, K4, expand( eData, 79 ) );

	digest[0] += A;
	digest[1] += B;
	digest[2] += C;
	digest[3] += D;
	digest[4] += E;

	memset(eData, 0, sizeof(eData));
}

#define ByteReverse(x) (x)

//static inline UINT32 ByteReverse(UINT32 value)
//{
//	return value;
	//value = ((value & 0xFF00FF00) >> 8) | ((value & 0x00FF00FF) << 8);
	//return rotlFixed16(value);
//	__asm {		
//		mov eax, [value]
//		bswap eax
//	}
	// Return with result in EAX
//}


inline void RLEncryptor02::Gamma(const UINT32 *key)
{
	m_lastIndex = 0xffffffff;

	for (UINT32 i=0; i<5; i++) H[i] = ByteReverse(key[i]);

	memset(D, 0, 64);
}


inline UINT32 RLEncryptor02::GammaApply(UINT32 i)
{
	UINT32 shaIndex = i / 5;
	if (shaIndex != m_lastIndex)
    {
		memcpy(Z, H, 20);
		D[0] = shaIndex;
		SHATransform(Z, D);
		m_lastIndex = shaIndex;
	}
	return Z[i%5];
}

void RLEncryptor02::Generate() // OperateKeystream
{
	UINT32 in = m_counter;
	UINT32 a, b, c, d, n1, n2, n3, n4;
	UINT32 p, q;
	UINT32 *wout = (UINT32 *)m_buffer;

	for (UINT32 l=0; l<RLEncryptor02_L; l++)
    {
		a = in ^ R[4*l];
		b = rotrFixed_8 (in) ^ R[4*l+1];
		c = rotrFixed_16(in) ^ R[4*l+2];
		d = rotrFixed_24(in) ^ R[4*l+3];

		UINT8 *Tptr = (UINT8 *) & T[0];

		#define Ttab(x) *(UINT32 *)(Tptr + x)

		for (UINT32 j=0; j<2; j++)
		{
			p = a & 0x7fc;
			b += Ttab(p);
			a = rotrFixed_9(a);

			p = b & 0x7fc;
			c += Ttab(p);
			b = rotrFixed_9(b);

			p = c & 0x7fc;
			d += Ttab(p);
			c = rotrFixed_9(c);

			p = d & 0x7fc;
			a += Ttab(p);
			d = rotrFixed_9(d);
		}

		n1 = d;
		n2 = b;
		n3 = a;
		n4 = c;

		p = a & 0x7fc;
		b += Ttab(p);
		a = rotrFixed_9(a);

		p = b & 0x7fc;
		c += Ttab(p);
		b = rotrFixed_9(b);

		p = c & 0x7fc;
		d += Ttab(p);
		c = rotrFixed_9(c);

		p = d & 0x7fc;
		a += Ttab(p);
		d = rotrFixed_9(d);

		// generate 8192 bits
		for (UINT32 i=0; i<64; i++)
		{
			p = a & 0x7fc;
			a = rotrFixed_9(a);
			b += Ttab(p);
			b ^= a;

			q = b & 0x7fc;
			b = rotrFixed_9(b);
			c ^= Ttab(q);
			c += b;

			p = (p + c) & 0x7fc;
			c = rotrFixed_9(c);
			d += Ttab(p);
			d ^= c;

			q = (q + d) & 0x7fc;
			d = rotrFixed_9(d);
			a ^= Ttab(q);
			a += d;

			p = (p + a) & 0x7fc;
			b ^= Ttab(p);
			a = rotrFixed_9(a);

			q = (q + b) & 0x7fc;
			c += Ttab(q);
			b = rotrFixed_9(b);

			p = (p + c) & 0x7fc;
			d ^= Ttab(p);
			c = rotrFixed_9(c);

			q = (q + d) & 0x7fc;
			d = rotrFixed_9(d);
			a += Ttab(q);

			wout[0] = ByteReverse(b + S[4*i+0]);
			wout[1] = ByteReverse(c ^ S[4*i+1]);
			wout[2] = ByteReverse(d + S[4*i+2]);
			wout[3] = ByteReverse(a ^ S[4*i+3]);

			wout += 4;

			if (i & 1)
			{
				a += n3;
				b += n4;
				c ^= n3;
				d ^= n4;
			}
			else
			{
				a += n1;
				b += n2;
				c ^= n1;
				d ^= n2;
			}
        }
    }
}


inline void RLEncryptor02::IncrementCounter()
{
	m_counter++;
	m_position = 0;
	Generate();
}

// added by maximp
//
inline void RLEncryptor02::RotateEncr(UINT8 *input, int count)
{
	int b = m_prevByte;

	for (; count>0; count--) {
		b = m_rotation[b ^ (*input)];
		*input = b;
		input++;
	}
	
	m_prevByte = b;
}

// added by maximp
//
inline void RLEncryptor02::RotateDecr(UINT8 *input, int count)
{
	int b = m_prevByte; // work faster that UINT8

	for (; count>0; count--) {
		int b2 = (*input);		
		*input = b^m_rotation[b2];
		b = b2;
		input++;
	}
	
	m_prevByte = b;
}

void RLEncryptor02::Encrypt(UINT8 *input, int count)
{
	XORCrypt  (input, count);
	RotateEncr(input, count);
}

void RLEncryptor02::Decrypt(UINT8 *input, int count)
{
	RotateDecr(input, count);
	XORCrypt  (input, count);
}

void RLEncryptor02::XORCrypt(UINT8 *input, int length)
{
	while (true)
    {
		int count = 1024*RLEncryptor02_L - m_position;
		if (length<count) break;
		FastXOR(input, m_buffer + m_position, count);
		length -= count;
		input  += count;
		IncrementCounter();
    }

	FastXOR(input, m_buffer + m_position, length);
	m_position += length;
}

void RLEncryptor02::SetKey(const UINT8 *key, bool encryption)
{
	UINT i;

	m_counter = 0;
	m_position = 0;

	Gamma((UINT32*)key);

	for (i=0; i<512; i++)	T[i] = GammaApply(i);

	for (i=0; i<256; i++)	S[i] = GammaApply(0x1000 + i);

	for (i=0; i<4*RLEncryptor02_L; i++)	R[i] = GammaApply(0x2000 + i);

	Generate();

	// added maximp, fill rotate table
	m_prevByte = 0;
	UINT8 buffer[256];
	UINT8 rotation[256];

	for (i=0; i<256; i++) rotation[i] = (UINT8)(0xFF-i);
	
	int n1 = 0;

	for (    i=0;   i<2;  i++) {
	for (int j=i+1; j<20; j++) {
	for (int k=j+1; k<20; k++) {
		int v = (key[i] ^ key[j] + key[k]) % 256;
		buffer[n1++] = (UINT8)v;
		if (n1>=256) goto exit1;
	}
	}
	}

exit1:

	for (int v1=0;  v1<256; v1++) {
		int v2 = buffer[v1];
		if (v1==v2) continue;
		// swap
		UINT8 k1 = rotation[v1];
		rotation[v1] = rotation[v2];
		rotation[v2] = k1;
	}

	if (encryption) {
		memcpy(m_rotation, rotation, sizeof(m_rotation));
	}
	else {
		for (i=0;  i<256; i++)
			m_rotation[rotation[i]] = (UINT8)i;
	}

	//int n3 = 0;
	//for (i=0;  i<256; i++) if (m_rotation[i]==i) n3++;
}
