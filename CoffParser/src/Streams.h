#ifndef __STREAMS_H_
#define __STREAMS_H_
#pragma once

#include <string>

////////////////////////////////////////////////////////////////////////////////////////////////////
// this classes use big endian numbers format
////////////////////////////////////////////////////////////////////////////////////////////////////
// ��������� ������ � ������������ �������������� ��� ������� ��������
// binary mode only
////////////////////////////////////////////////////////////////////////////////////////////////////
struct SFileIOError
{
	string szError;
	SFileIOError( const char *pszString ): szError(pszString) {}
	SFileIOError( const string &s ): szError(s) {}
};
////////////////////////////////////////////////////////////////////////////////////////////////////
class CMemoryStream;
class CDataStream
{
protected:
	enum EFlags
	{
		F_Wasted   = 1,
		F_CanRead  = 2,
		F_CanWrite = 4,
	};

	unsigned char *pBuffer, *pCurrent, *pReservedEnd;
	unsigned char *pFileEnd;
	unsigned int nBufferStart;
	int nFlags;

	int CanRead() { return nFlags & F_CanRead; }
	int CanWrite() { return nFlags & F_CanWrite; }
	int IsWasted() { return nFlags & F_Wasted; }
	void SetWasted() { nFlags |= F_Wasted; }
	void ClearWasted() { nFlags &= ~F_Wasted; }
protected:
	void FixupSize() { if ( pCurrent > pFileEnd ) pFileEnd = pCurrent; } // this limits us to max file size 2M
	// ��������� �������������� ������ �� ����� ���������������� �������
	// a) ���������� ������� ������ ��� �������� ������� �������
	virtual void AllocForDirectReadAccess( size_t nSize ) = 0;
	// ������ ��������� ������� ���������� ������ � ������, nSize - ��������� ������ ������
	virtual void AllocForDirectWriteAccess( size_t nSize ) = 0;
	// b) ��������� �� ��������� ������ ������� �������
	virtual void NotifyFinishDirectAccess() {}
	// c) ������/������ �� �������������� � ������� �����
	virtual void DirectRead( void *pDest, size_t nSize ) = 0;
	virtual void DirectWrite( const void *pSrc, size_t nSize ) = 0;
	//
	// ������� ��� ����������� ������ ������� (� �������������� ��������) ������� � ������
	// can be called multiple times
	// ��������������� ��� ����������/������ nSize ���� (����� ���������� ����������
	// ������ ���� �������� �� ������������� ��������� ����� ������ ���� �������)
	// ������� ������ ������ ������������ �������
	inline unsigned char* ReserveR( size_t nSize );
	// ������� ReserveW ������ ��������� ���� ������� ����� � ������
	inline unsigned char* ReserveW( size_t nSize );
	// fixes up stream size if needed
	// ��������� ����� ������� ������� � ������, ������� ������ ����������� ���������
	// ������� ������� � pFinish
	void Free( unsigned char *pFinish ) { pCurrent = pFinish; NotifyFinishDirectAccess(); }

	// ������������� ������ � ������
	void RRead( void *pDest, size_t nSize ) { memcpy( pDest, pCurrent, nSize ); pCurrent += nSize; ASSERT( pCurrent <= pReservedEnd ); }
	void RWrite( const void *pSrc, size_t nSize ) { memcpy( pCurrent, pSrc, nSize ); pCurrent += nSize; ASSERT( pCurrent <= pReservedEnd ); }
	// exceptional case
	void ReadOverflow( void *pDest, size_t nSize );
	//
public:
	CDataStream() { nBufferStart = 0; }
	virtual ~CDataStream() {}
	// ����������������
	virtual void Seek( size_t nPos ) = 0;
	//void Trunc(); // instead of SetSize, truncates file on current position
	// ������� ������� ��� ������/������ ��/� �����
	inline void Read( void *pDest, size_t nSize );
	inline void Write( const void *pSrc, size_t nSize );
	//
	size_t GetSize() { FixupSize(); return pFileEnd - pBuffer + nBufferStart; }
	size_t GetPosition() { return pCurrent - pBuffer + nBufferStart; }

	virtual const char* GetName() { return ""; }

	//
	// ����������� �������� �����/������
	void ReadString( string &res, int nMaxSize = -1 );
	void WriteString( const string &res );
	template<class T>
		CDataStream& operator>>( T &res ) { Read( &res, sizeof(res) ); return *this; }
	template<class T>
		CDataStream& operator<<( const T &res ) { Write( &res, sizeof(res) ); return *this; }
	template<>
		CDataStream& operator>>( string &res ) { ReadString( res ); return *this; }
	template<>
		CDataStream& operator<<( const string &res ) { WriteString( res ); return *this; }
	// operations with whole streams
	inline void ReadTo( CMemoryStream *pDst, size_t nSize );
	inline void WriteFrom( CDataStream &src );

	friend class CBitLocker;
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// ������ ������������� ������, ������� ��� ������ � memory mapped files
class CFixedMemStream: public CDataStream
{
protected:
	virtual void AllocForDirectReadAccess( size_t nSize );
	// write is forbidden
	virtual void AllocForDirectWriteAccess( size_t nSize );
	// fill with zero exceeding request
	virtual void DirectRead( void *pDest, size_t nSize );
	virtual void DirectWrite( const void *pSrc, size_t nSize );
public:
	CFixedMemStream( const void *pCData, size_t nSize )
	{
		void *pData = const_cast<void*>( pCData ); 
		pBuffer = (unsigned char*)pData; pCurrent = pBuffer; pReservedEnd = pBuffer + nSize;
		pFileEnd = pReservedEnd;
		nFlags = F_CanRead;
	}
	virtual void Seek( size_t nPos ) { pCurrent = pBuffer + nPos; ASSERT( pCurrent <= pReservedEnd ); }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// �����, ������� ��������������� � ������
class CMemoryStream: public CDataStream
{
private:
	unsigned char *AllocBuffer( size_t nSize );
	void FreeBuffer();
	void RealFixupBufferSize( size_t nNewSize );
protected:
	// �������� ������ ���, ����� ��� ������� nNewSize ���� ���� ��������
	void FixupBufferSize( size_t nNewSize );
	virtual void AllocForDirectReadAccess( size_t nSize );
	virtual void AllocForDirectWriteAccess( size_t nSize );
	virtual void DirectRead( void *pDest, size_t nSize );
	virtual void DirectWrite( const void *pSrc, size_t nSize );
	void CopyMemoryStream( const CMemoryStream &src );
public:
	CMemoryStream();
	~CMemoryStream();
	CMemoryStream( const CMemoryStream &src );
	CMemoryStream& operator=( const CMemoryStream &src );
	void SetRMode() { nFlags = (nFlags&~(F_CanRead|F_CanWrite)) | F_CanRead; }
	void SetWMode() { nFlags = (nFlags&~(F_CanRead|F_CanWrite)) | F_CanWrite; }
	void SetRWMode() { nFlags = nFlags | (F_CanRead|F_CanWrite); }
	virtual void Seek( size_t nPos ) { FixupSize(); pCurrent = pBuffer + nPos; if ( pCurrent > pReservedEnd ) FixupBufferSize( pCurrent - pBuffer ); }
	// special memory stream funcs, this functions work only for memory stream
	void Clear() { pFileEnd = pBuffer; pCurrent = pBuffer; nFlags = F_CanRead|F_CanWrite; }
	// fast buffer access, use only if perfomance is of paramount importance
	const unsigned char* GetBuffer() const { return pBuffer; }
	unsigned char* GetBufferForWrite() const { return pBuffer; }
	void SetSize( size_t nSize ) { pFileEnd = pBuffer + nSize; pCurrent = pBuffer; if ( pFileEnd > pReservedEnd ) FixupBufferSize( nSize ); }
	void SetSizeDiscard( size_t nSize );
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// ������������� �������������� �����
class CBufferedStream: public CDataStream
{
private:
	void FlushBuffer();
	void SetNewBufferSize( size_t nSize );
	void ShiftBuffer();
	void LoadBufferForced( size_t nPos ); // set pCurrent to nPos and load buffer from nPos
	//
	virtual void AllocForDirectReadAccess( size_t nSize );
	virtual void AllocForDirectWriteAccess( size_t nSize );
	virtual void NotifyFinishDirectAccess();
	// c) ������/������ �� �������������� � ������� �����
	virtual void DirectRead( void *pDest, size_t nSize );
	virtual void DirectWrite( const void *pSrc, size_t nSize );
	CBufferedStream( const CBufferedStream &a ) { ASSERT(0); }
	CBufferedStream& operator=( const CBufferedStream &a ) { ASSERT(0); return *this;}
protected:
	void StartAccess( size_t nFileSize, size_t nSize );
	void FinishAccess();
	//
	virtual size_t DoRead( size_t nPos, void *pDest, size_t nSize ) = 0;
	virtual size_t DoWrite( size_t nPos, const void *pSrc, size_t nSize ) = 0;
public:
	CBufferedStream() { pBuffer = 0; nFlags = 0; pCurrent = 0; pFileEnd = 0; }
	~CBufferedStream() { FinishAccess(); }
	virtual void Seek( size_t nPos );
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// �������� �����
class CFileStream: public CBufferedStream
{
	std::string fileName;
	FILE *pFile;

	virtual size_t DoRead( size_t nPos, void *pDest, size_t nSize );
	virtual size_t DoWrite( size_t nPos, const void *pSrc, size_t nSize );
	CFileStream( const CFileStream &a ) { ASSERT(0); }
	CFileStream& operator=( const CFileStream &a ) { ASSERT(0); return *this;}
	//
	void Open( const char *pszFName, const char *pszMode, int _nFlags = F_CanRead|F_CanWrite );
public:
	CFileStream() { pFile = 0; }
	~CFileStream() { CloseFile(); }
	void CloseFile();
	void OpenRead( const char *pszFName ) { Open( pszFName, "rb", F_CanRead ); }
	void OpenWrite( const char *pszFName ) { Open( pszFName, "wb", F_CanWrite ); }
	void Open( const char *pszFName ) { Open( pszFName, "r+b" ); }
	bool TryOpenRead( const char *pszFName ) { try { OpenRead(pszFName); return true; } catch(...) {} return false; }
	bool TryOpenWrite( const char *pszFName ) { try { OpenWrite(pszFName); return true; } catch(...) {} return false; }
	bool TryOpen( const char *pszFName ) { try { Open(pszFName); return true; } catch(...) {} return false; }
	bool IsOpen() const { return pFile != 0; }
	virtual const char* GetName() { return fileName.c_str(); }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
// ����� ��� ���������������� ������/���������� ������, ������� ����������� ������
// ��� ���������� �������� ������, ����� �������������� �� ������������� ��������
// ������
class CBitStream
{
public:
	enum Mode
	{
		read,
		write
	};

protected:
	unsigned char *pCurrent;
	unsigned char *pBitPtr;         // for bit writing
	unsigned int nBits;
	unsigned char nBitsCount; // bits and bit counter
	static unsigned int nBitsMask[32];
	
#ifdef _DEBUG
	Mode mode;
	unsigned char *pReservedEnd;
	void CheckCurrentR() { ASSERT( pCurrent <= pReservedEnd ); ASSERT( mode == read ); }
	void CheckCurrentW() { ASSERT( pCurrent <= pReservedEnd ); ASSERT( mode == write ); }
#else
	void CheckCurrentR() {}
	void CheckCurrentW() {}
#endif

	inline void Init( unsigned char *pData, Mode _mode, size_t nSize );
public:
	CBitStream( void *pData, Mode _mode, size_t nSize ) { Init( (unsigned char*)pData, _mode, nSize ); }
	// result of read/write beyond data range is not determined
	void Read( void *pDest, size_t nSize ) { memcpy( pDest, pCurrent, nSize ); pCurrent += nSize; CheckCurrentR(); }
	void Write( const void *pSrc, size_t nSize ) { memcpy( pCurrent, pSrc, nSize ); pCurrent += nSize; CheckCurrentW(); }
	void ReadCString( string &res ) { size_t nLeng = strlen( (char*)pCurrent ); res.assign( (char*)pCurrent, nLeng ); pCurrent += nLeng + 1; CheckCurrentR(); }
	void WriteCString( const char *pSrc ) { size_t nLeng = strlen( pSrc ); memcpy( pCurrent, pSrc, nLeng + 1 ); pCurrent += nLeng + 1; CheckCurrentW(); }
	void FlushBits() { if ( nBitsCount ) { nBitsCount = 0; if ( pBitPtr ) pBitPtr[0] = (char)nBits; } }
	// not more then 24 bits per call
	inline void WriteBits( unsigned int _nBits, size_t _nBitsCount );
	inline void WriteBit( unsigned int _nBits );
	inline size_t ReadBits( size_t _nBitsCount );
	inline size_t ReadBit();
	// even more direct access, try to not use it, read only
	const unsigned char* GetCurrentPtr() const { return pCurrent; }
	// get pointer to place to write to later (not later then this object will be destructed)
	unsigned char* WriteDelayed( size_t nSize ) { unsigned char *pRes = pCurrent; pCurrent += nSize; CheckCurrentW(); return pRes; }
	//
	template <class T>
		inline void Write( const T &a ) { Write( &a, sizeof(a) ); }
	template <class T>
		inline void Read( T &a ) { Read( &a, sizeof(a) ); }
	template<> 
		inline void Write<string>( const string &a ) { WriteCString( a.c_str() ); }
	template<> 
		inline void Read<string>( string &a ) { ReadCString( a ); }
	//
	friend class CBitEmbedded;
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// ����� ��� ���������� ��������� � ����������� �����/������ � ����� ������ ����������
// ����� ����, ��� � CDataStream �������� �������� CBitLocker ������ �������� � 
// DataStream �������� � ������������� ����������
class CBitLocker: public CBitStream
{
	CDataStream *pData;
	unsigned char *pBuffer;
public:
	CBitLocker(): CBitStream( 0, read, 0 ) { pData = 0; }
	~CBitLocker() { FlushBits(); if ( pData ) pData->Free( pCurrent ); }
	// once per life of this object
	void LockRead( CDataStream &data, size_t nSize );
	void LockWrite( CDataStream &data, size_t nSize );
	// alloc additional buffer space, for better perfomance minimize number of this 
	// function calls
	//void ReserveRead( size_t nSize );
	//void ReserveWrite( size_t nSize );
	void Free() { ASSERT( pData ); FlushBits(); pData->Free( pCurrent ); pData = 0; }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
class CBitEmbedded: public CBitStream
{
	CBitStream &bits;
public:
	CBitEmbedded( CBitStream &_bits ): 
#ifdef _DEBUG
		CBitStream( _bits.pCurrent, _bits.mode, _bits.pReservedEnd - _bits.pCurrent )
#else
		CBitStream( _bits.pCurrent, read, 0 )
#endif
		,bits(_bits) {}
	~CBitEmbedded() { bits.pCurrent = pCurrent; }
};
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
// realization of inline functions for above classes
////////////////////////////////////////////////////////////////////////////////////////////////////
// CDataStream realization
////////////////////////////////////////////////////////////////////////////////////////////////////
inline unsigned char* CDataStream::ReserveR( size_t nSize )
{
	ASSERT( CanRead() );
	if ( pCurrent + nSize > pReservedEnd )
		AllocForDirectReadAccess( nSize );
	return pCurrent;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// ������� ReserveW ������ ��������� ���� ������� ����� � ������
inline unsigned char* CDataStream::ReserveW( size_t nSize )
{
	ASSERT( CanWrite() );
	SetWasted();
	if ( pCurrent + nSize > pReservedEnd )
		AllocForDirectWriteAccess( nSize );
	return pCurrent;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CDataStream::Read( void *pDest, size_t nSize )
{
	ASSERT( CanRead() );
	if ( pCurrent + nSize <= pFileEnd )
	{
		if ( pCurrent + nSize <= pReservedEnd )
			RRead( pDest, nSize );
		else 
			DirectRead( pDest, nSize );
	}
	else
		ReadOverflow( pDest, nSize);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CDataStream::Write( const void *pSrc, size_t nSize )
{
	ASSERT( CanWrite() );
	if ( pCurrent + nSize <= pReservedEnd )
	{
		SetWasted();
		RWrite( pSrc, nSize );
	}
	else DirectWrite( pSrc, nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CDataStream::ReadTo( CMemoryStream *pDst, size_t nSize )
{ 
	pDst->SetSizeDiscard( nSize );
	Read( pDst->GetBufferForWrite(), nSize );
	pDst->Seek(0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CDataStream::WriteFrom( CDataStream &src )
{
	src.Seek(0);
	size_t nSize = src.GetSize();
	unsigned char *pBuf = src.ReserveR( nSize );
	Write( pBuf, nSize );
	src.Free( pBuf + nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// CBitStream realization
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CBitStream::Init( unsigned char *pData, Mode _mode, size_t nSize )
{
	pCurrent = pData; nBitsCount = 0; pBitPtr = 0;
#ifdef _DEBUG
	mode = _mode;
	pReservedEnd = pCurrent + nSize;
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CBitStream::WriteBits( unsigned int _nBits, size_t _nBitsCount )
{
	if ( nBitsCount != 0 )
	{
		nBits += ( _nBits << nBitsCount );
		nBitsCount += _nBitsCount;
	}
	else
	{
		pBitPtr = pCurrent++;
		nBits = _nBits;
		nBitsCount = _nBitsCount;
	}
	while ( nBitsCount > 8 )
	{
		pBitPtr[0] = (unsigned char)nBits; //( nBits & 0xff );
		nBits >>= 8; nBitsCount -= 8;
		pBitPtr = pCurrent++;
	}
	CheckCurrentW();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void CBitStream::WriteBit( unsigned int _nBits )
{
	if ( nBitsCount == 0 )
	{
		pBitPtr = pCurrent++;
		nBits = _nBits;
		nBitsCount = 1;
	}
	else
	{
		nBits += ( _nBits << nBitsCount );
		nBitsCount++;
	}
	if ( nBitsCount > 8 )
	{
		pBitPtr[0] = (unsigned char)nBits; //( nBits & 0xff );
		nBits >>= 8; nBitsCount -= 8;
		pBitPtr = pCurrent++;
	}
	CheckCurrentW();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline size_t CBitStream::ReadBits( size_t _nBitsCount )
{
	while ( nBitsCount < _nBitsCount )
	{
		nBits += ((unsigned int)*pCurrent++) << nBitsCount;
		nBitsCount += 8;
	}
	int nRes = nBits & nBitsMask[ _nBitsCount - 1 ];
	nBits >>= _nBitsCount;
	nBitsCount -= _nBitsCount;
	CheckCurrentR();
	return nRes;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
inline size_t CBitStream::ReadBit()
{
	if ( nBitsCount < 1 )
	{
		nBits = ((unsigned int)*pCurrent++);
		nBitsCount = 8;
	}
	int nRes = nBits & 1;
	nBits >>= 1;
	nBitsCount--;
	CheckCurrentR();
	return nRes;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
class CTextStream
{
	CDataStream &f;
public:
	typedef CTextStream& (*OpFunc)( CTextStream& );
	CTextStream( CDataStream &_f ) : f(_f) {}
	CTextStream& operator<<( const char *p ) { f.Write( p, strlen(p) ); return *this; }
	CTextStream& operator<<( int n ) { char buf[128]; _itoa( n, buf, 10 ); f.Write( buf, strlen(buf) ); return *this; }
	CTextStream& operator<<( double n ) { char buf[128]; _gcvt( n, 7, buf ); f.Write( buf, strlen(buf) ); return *this; }
	CTextStream& operator<<( const string &s ) { f.Write( s.c_str(), s.length() ); return *this; }
	CTextStream& operator<<( OpFunc func ) { return func(*this); }
};
inline CTextStream& endl( CTextStream& sStream ) { sStream << "\n"; return sStream; }
////////////////////////////////////////////////////////////////////////////////////////////////////
#endif