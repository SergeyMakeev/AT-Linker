#include "StdAfx.h"
////////////////////////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include "Streams.h"
////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned int CBitStream::nBitsMask[32] = {
	0x01,       0x03,       0x07,       0x0F,        0x1F,       0x3F,       0x7F,       0xFF,
	0x01FF,     0x03FF,     0x07FF,     0x0FFF,      0x1FFF,     0x3FFF,     0x7FFF,     0xFFFF,
	0x01FFFF,   0x03FFFF,   0x07FFFF,   0x0FFFFF,    0x1FFFFF,   0x3FFFFF,   0x7FFFFF,   0xFFFFFF,
	0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,  0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF,
};
////////////////////////////////////////////////////////////////////////////////////////////////////
// CDataStream
////////////////////////////////////////////////////////////////////////////////////////////////////
void CDataStream::ReadOverflow( void *pDest, size_t nSize )
{
	//SetFailed();
	memset( pDest, 0, nSize );
	if ( pCurrent < pFileEnd )
	{
		size_t nRes = pFileEnd - pCurrent;
		Read( pDest, nRes ); // return Read( pDest, nRes );
	}
	pCurrent = pFileEnd;
	throw SFileIOError( "read overflow" );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CDataStream::ReadString( string &res, int nMaxSize )
{
	size_t nSize = 0;
	Read( &nSize, 1 );
	if ( nSize & 1 )
		Read( ((char*)&nSize) + 1, 3 );
	nSize >>= 1;
	if ( nMaxSize > 0 && nSize > nMaxSize )
	{
		throw SFileIOError( "string read error" );
		//SetFailed();
		//return false;
	}
	unsigned char *pData = ReserveR( nSize );
	res.assign( (const char*)pData, nSize );
	Free( pData + nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CDataStream::WriteString( const string &res )
{
	size_t nSize = res.size(), nVal;
	if ( nSize >= 128 )
	{
		nVal = nSize * 2 + 1;
		Write( &nVal, 4 );
	}
	else
	{
		nVal = nSize * 2;
		Write( &nVal, 1 );
	}
	Write( res.data(), nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// CBitLocker realization
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBitLocker::LockRead( CDataStream &data, size_t nSize )
{
	ASSERT(!pData);
	pData = &data;
	pBuffer = data.ReserveR( nSize );
	Init( pBuffer, read, nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBitLocker::LockWrite( CDataStream &data, size_t nSize )
{
	ASSERT(!pData);
	pData = &data;
	pBuffer = data.ReserveW( nSize );
	Init( pBuffer, write, nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
/*void CBitLocker::ReserveRead( size_t nSize )
{
	ASSERT(pData);
	size_t nNewSize = pCurrent - pBuffer + nSize;
	unsigned char *pNewBuf;
	pNewBuf = pData->ReserveR( nNewSize );
#ifdef _DEBUG
	pReservedEnd = pNewBuf + nNewSize;
#endif
	size_t nFixup = pNewBuf - pBuffer;
	pCurrent += nFixup;
	pBitPtr += nFixup;
	pBuffer = pNewBuf;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBitLocker::ReserveWrite( size_t nSize )
{
	ASSERT(pData);
	size_t nNewSize = pCurrent - pBuffer + nSize;
	unsigned char *pNewBuf;
	pNewBuf = pData->ReserveW( nNewSize );
#ifdef _DEBUG
	pReservedEnd = pNewBuf + nNewSize;
#endif
	size_t nFixup = pNewBuf - pBuffer;
	pCurrent += nFixup;
	pBitPtr += nFixup;
	pBuffer = pNewBuf;
}*/
////////////////////////////////////////////////////////////////////////////////////////////////////
// CFixedMemStream
////////////////////////////////////////////////////////////////////////////////////////////////////
void CFixedMemStream::AllocForDirectReadAccess( size_t nSize )
{
	// this amount can not be guaranteed to be read
	ASSERT(0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CFixedMemStream::AllocForDirectWriteAccess( size_t nSize )
{
	ASSERT(0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CFixedMemStream::DirectRead( void *pDest, size_t nSize )
{
	// this should never happen
	ASSERT(0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CFixedMemStream::DirectWrite( const void *pSrc, size_t nSize )
{
	// write is forbidden
	ASSERT(0); 
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// CMemoryStream
////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned char *CMemoryStream::AllocBuffer( size_t nSize )
{
	unsigned char *pRes;
	if ( nSize > 2 * 1024 * 1024 )
	{
		pRes = (unsigned char*)VirtualAlloc( 0, nSize, MEM_COMMIT, PAGE_READWRITE );
		if ( pRes == 0 )
		{
			FILE *pFile = fopen( "error.txt", "wt" );
			fprintf( pFile, "ERROR!: can't alloc %d bytes", nSize, pReservedEnd - pBuffer );
			fflush( pFile );
			fclose( pFile );
		}
	}
	else
		pRes = new unsigned char[ nSize ];
	return pRes;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::FreeBuffer()
{
	size_t nSize = pReservedEnd - pBuffer;
	if ( nSize > 2 * 1024 * 1024 )
	{
		BOOL bOk = VirtualFree( pBuffer, 0, MEM_RELEASE );
		ASSERT( bOk );
	}
	else
		delete[] pBuffer;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::RealFixupBufferSize( size_t nNewSize )
{
	unsigned char *pNewBuf = AllocBuffer( nNewSize );
	memcpy( pNewBuf, pBuffer, pReservedEnd - pBuffer );
	FreeBuffer();
	pCurrent = pNewBuf + ( pCurrent - pBuffer );
	pFileEnd = pNewBuf + ( pFileEnd - pBuffer );
	pReservedEnd = pNewBuf + nNewSize;
	pBuffer = pNewBuf;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::FixupBufferSize( size_t nNewSize )
{
	RealFixupBufferSize( nNewSize * 2 + 64 );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::AllocForDirectReadAccess( size_t nSize )
{
	RealFixupBufferSize( GetPosition() + nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// должна сохранять текущее содержимое буфера в памяти
void CMemoryStream::AllocForDirectWriteAccess( size_t nSize )
{
	RealFixupBufferSize( GetPosition() + nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// c) чтение/запись не укладывающиеся в текущий буфер
void CMemoryStream::DirectRead( void *pDest, size_t nSize )
{
	// should never happen
	ASSERT(0);
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::DirectWrite( const void *pSrc, size_t nSize )
{
	FixupBufferSize( GetPosition() + nSize );
	RWrite( pSrc, nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
CMemoryStream::CMemoryStream() 
{ 
	nFlags = F_CanRead | F_CanWrite; 
	size_t nDefaultSize = 32;
	pBuffer = AllocBuffer( nDefaultSize );
	pReservedEnd = pBuffer + nDefaultSize;
	pFileEnd = pBuffer;
	pCurrent = pBuffer;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
CMemoryStream::~CMemoryStream()
{
	FreeBuffer();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::SetSizeDiscard( size_t nSize )
{
	size_t nBufSize = nSize + 64;
	FreeBuffer();
	pBuffer = AllocBuffer( nBufSize );
	pReservedEnd = pBuffer + nBufSize;
	pFileEnd = pBuffer + nSize;
	pCurrent = pBuffer;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CMemoryStream::CopyMemoryStream( const CMemoryStream &src )
{
	size_t nBufSize = src.pReservedEnd - src.pBuffer;
	pBuffer = AllocBuffer( nBufSize );
	pReservedEnd = pBuffer + nBufSize;
	memcpy( pBuffer, src.pBuffer, nBufSize );
	pFileEnd = pBuffer + ( src.pFileEnd - src.pBuffer );
	pCurrent = pBuffer + ( src.pCurrent - src.pBuffer );
	nFlags = src.nFlags;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
CMemoryStream::CMemoryStream( const CMemoryStream &src )
{
	CopyMemoryStream( src );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
CMemoryStream& CMemoryStream::operator=( const CMemoryStream &src )
{
	FreeBuffer();
	CopyMemoryStream( src );
	return *this;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// CBufferedStream
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::FlushBuffer()
{
	if ( IsWasted() )
	{
		ASSERT( pBuffer );
		ClearWasted();
		if ( !pBuffer )
			return;
		size_t nSize = pCurrent - pBuffer;
		if ( DoWrite( nBufferStart, pBuffer, nSize ) != nSize )
			throw SFileIOError( "write error" );// SetFailed(); 
		FixupSize();
	} 
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::SetNewBufferSize( size_t nSize )
{
	size_t nNewSize = nSize + 1024;
	if ( nNewSize < pReservedEnd - pBuffer )
		return;
	unsigned char *pNewBuf = new unsigned char [ nNewSize ];
	if ( pBuffer )
	{
		memcpy( pNewBuf, pBuffer, pReservedEnd - pBuffer );
		delete[] pBuffer;
	}
	size_t nFixup = pNewBuf - pBuffer;
	pCurrent += nFixup;
	pFileEnd += nFixup;
	pBuffer = pNewBuf;
	pReservedEnd = pNewBuf + nNewSize;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::StartAccess( size_t nFileSize, size_t nSize )
{
	ClearWasted();
	pBuffer = new unsigned char [ nSize ];
	pCurrent = pBuffer;
	pFileEnd = pBuffer + nFileSize;
	pReservedEnd = pBuffer + nSize;
	nBufferStart = 0;
	LoadBufferForced( 0 );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::FinishAccess()
{
	FlushBuffer();
	if ( pBuffer ) delete[] pBuffer; pBuffer = 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
template<class T>
T __Min( const T &a, const T &b ) { return a < b ? a : b; }
//
void CBufferedStream::LoadBufferForced( size_t nPos )
{
	FlushBuffer();
	size_t nRead = __Min( pReservedEnd - pBuffer, pFileEnd - pCurrent );
	if ( DoRead( nPos, pBuffer, nRead ) != nRead )
		throw SFileIOError( "read error" );//SetFailed(); // throw
	pCurrent += nBufferStart - nPos;
	pFileEnd += nBufferStart - nPos;
	nBufferStart = nPos;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// shift buffer start to pCurrent
void CBufferedStream::ShiftBuffer()
{
	size_t nPos = GetPosition();
	if ( nBufferStart == nPos )
		return;
	LoadBufferForced( nPos ); // also shifts pCurrent to pBuffer
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// a) недостаток размера буфера для операция прямого доступа
void CBufferedStream::AllocForDirectReadAccess( size_t nSize )
{
	if ( nSize > pReservedEnd - pBuffer )
	{
		SetNewBufferSize( nSize );
		LoadBufferForced( nBufferStart );
	}
	else
		ShiftBuffer();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::AllocForDirectWriteAccess( size_t nSize )
{
	SetNewBufferSize( nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::NotifyFinishDirectAccess()
{
	ShiftBuffer();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::Seek( size_t nPos )
{
	FixupSize();
	unsigned char *pNewCurrent = pBuffer + nPos - nBufferStart; 
	if ( pNewCurrent >= pBuffer && pNewCurrent < pReservedEnd )
	{
		pCurrent = pNewCurrent;
		return;
	}
	FlushBuffer(); 
	pCurrent = pNewCurrent;
	ShiftBuffer();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::DirectRead( void *pDest, size_t nSize )
{
	if ( nSize > pReservedEnd - pBuffer )
	{
		FlushBuffer();
		size_t nRes = DoRead( GetPosition(), pDest, nSize );
		if ( nRes != nSize )
			throw SFileIOError( "read size mismatch" );//SetFailed(); // throw
		pCurrent += nSize;
		ShiftBuffer();
		return;
	}
	ShiftBuffer();
	Read( pDest, nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CBufferedStream::DirectWrite( const void *pSrc, size_t nSize )
{
	if ( nSize > pReservedEnd - pBuffer )
	{
		FlushBuffer();
		size_t nRes = DoWrite( GetPosition(), pSrc, nSize );
		if ( nRes != nSize )
			throw SFileIOError( "write size mismatch" );//SetFailed(); // throw
		pCurrent += nSize;
		FixupSize();
		ShiftBuffer();
		return;
	}
	ShiftBuffer();
	Write( pSrc, nSize );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
// CFileStream
////////////////////////////////////////////////////////////////////////////////////////////////////
size_t CFileStream::DoRead( size_t nPos, void *pDest, size_t nSize )
{
	ASSERT( pFile );
	if ( !pFile )
		return 0;
	if ( fseek( pFile, nPos, SEEK_SET ) )
		return 0;
	return fread( pDest, 1, nSize, pFile );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
size_t CFileStream::DoWrite( size_t nPos, const void *pSrc, size_t nSize )
{
	ASSERT( pFile );
	if ( !pFile )
		return 0;
	if ( fseek( pFile, nPos, SEEK_SET ) )
		return 0;
	return fwrite( pSrc, 1, nSize, pFile );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CFileStream::CloseFile()
{
	FinishAccess();
	if ( pFile ) fclose( pFile ); pFile = 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void CFileStream::Open( const char *pszFName, const char *pszMode, int _nFlags )
{
	CloseFile();
	//SetOk();
	nFlags = _nFlags; 
	pFile = fopen( pszFName, pszMode ); 
	if ( pFile )
	{
		fileName = pszFName;

		fseek( pFile, 0, SEEK_END );
		size_t nFileSize = ftell( pFile );
		StartAccess( nFileSize, 1024 );
	}
	else
		throw SFileIOError( string("error opening file ") + pszFName );//SetFailed();
}
////////////////////////////////////////////////////////////////////////////////////////////////////
