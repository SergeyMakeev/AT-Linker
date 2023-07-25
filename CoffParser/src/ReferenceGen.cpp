#include "StdAfx.h"
#include "Streams.h"
#include <iostream>
#include "CoffFile.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
struct SSection
{
	string szName;
	vector<char> data;
	int nFlags;
	struct SReloc
	{
		int nOffset, nSymbol;
		SReloc( int _nOffset = 0, int _nSymbol = 0 ) : nOffset(_nOffset), nSymbol(_nSymbol) {}
	};
	vector<SReloc> relocs;
};
////////////////////////////////////////////////////////////////////////////////////////////////////
struct SSymbol
{
	string szName;
	int nSection;
	int nWeakRef;
	bool bExternal;

	SSymbol() {}
	SSymbol( const string &_szName, int _nSection, bool _bExternal = true ) 
		: szName(_szName), nSection(_nSection), nWeakRef(-1), bExternal(_bExternal) {}
};
////////////////////////////////////////////////////////////////////////////////////////////////////
struct SStringTable
{
	vector<char> stringTable;
	
	SStringTable() { stringTable.resize(4); }
	void AssignName( SCoffName *pRes, const string &szName )
	{
		memset( pRes, 0, sizeof(*pRes) );
		if ( szName.size() <= 8 )
			strncpy( (char*)pRes, szName.c_str(), 8 );
		else
		{
			int nPtr = stringTable.size();
			for ( size_t k = 0; k < szName.size(); ++k )
				stringTable.push_back( szName[k] );
			stringTable.push_back( 0 );
			((int*)pRes)[1] = nPtr;
		}
	}
};
static void WriteObj( CMemoryStream *pRes, const vector<SSection> &sections, const vector<SSymbol> &symbols )
{
	SStringTable table;
	CMemoryStream &res = *pRes;
	size_t nHeaderSize = sizeof(SCoffImage::SCoffHeader);
	size_t nRawDataStart = nHeaderSize + sizeof(SCoffImage::SCoffSection) * sections.size();
	res.SetSize( nRawDataStart );
	res.Seek( res.GetSize() );
	// write sections
	for ( size_t k = 0; k < sections.size(); ++k )
	{
		const SSection &s = sections[k];
		SCoffImage::SCoffSection section;
		table.AssignName( &section.szName, s.szName );
		const vector<char> &code = s.data;
		section.dwImageSize = 0;
		section.dwAddrStart = 0;
		// data
		section.dwSize = code.size();
		if ( code.size() > 0 )
		{
			section.pData = res.GetSize();
			res.Seek( res.GetSize() );
			res.Write( &code[0], code.size() );
		}
		else
			section.pData = 0;
		// relocs
		vector<SCoffImage::SCoffRelocation> rels( s.relocs.size() );
		for ( size_t i = 0; i < s.relocs.size(); ++i )
		{
			const SSection::SReloc &rSrc = s.relocs[i];
			SCoffImage::SCoffRelocation &r = rels[i];
			r.dwOffset = rSrc.nOffset;
			r.nSymbol = rSrc.nSymbol;
			r.type = IMAGE_REL_I386_DIR32NB;
		}
		section.nRelocs = s.relocs.size();
		if ( rels.size() > 0 )
		{
			section.pRelocs = res.GetSize();
			res.Seek( res.GetSize() );
			res.Write( &rels[0], sizeof(rels[0]) * rels.size() );
		}
		else
			section.pRelocs = 0;
		// line numbers
		section.pLineNumbers = 0;
		section.nLineNumbers = 0;
		section.flags = s.nFlags;
		res.Seek( nHeaderSize + sizeof(section) * k );
		res.Write( &section, sizeof(section) );
	}

	size_t nSymbolsStart = res.GetSize();
	res.Seek( res.GetSize() );
	int nTotalSymbols = 0;
	for ( size_t k = 0; k < symbols.size(); ++k )
	{
		const SSymbol &symbol = symbols[k];
		bool bHasWeakRef = symbol.nWeakRef != -1;
		// write symbols
		SCoffImage::SCoffSymbol sym;
		table.AssignName( &sym.szName, symbol.szName );
		sym.nValue = 0;
		sym.nSection = symbol.nSection;
		sym.nType = 0x20;
		if ( bHasWeakRef )
			sym.nStorageClass = IMAGE_SYM_CLASS_WEAK_EXTERNAL;
		else
			sym.nStorageClass = symbol.bExternal ? IMAGE_SYM_CLASS_EXTERNAL : IMAGE_SYM_CLASS_STATIC;
		sym.nAuxSymbols = bHasWeakRef ? 1 : 0;
		res.Write( &sym, sizeof(sym) );
		++nTotalSymbols;
		if ( bHasWeakRef )
		{
			SCoffImage::SCoffWeakExternal weakRef;
			memset( &weakRef, 0, sizeof(weakRef) );
			weakRef.dwFlags = IMAGE_WEAK_EXTERN_SEARCH_LIBRARY;
			weakRef.dwTagIndex = symbol.nWeakRef;
			res.Write( &weakRef, sizeof(weakRef) );
			++nTotalSymbols;
		}
	}
	// string table
	*((size_t*)&table.stringTable[0]) = table.stringTable.size();
	res.Write( &table.stringTable[0], table.stringTable.size() );
	// write header
	SCoffImage::SCoffHeader header;
	header.machine = 0x14c;
	header.nSections = sections.size();
	header.timeStamp = 0;
	header.pSymbols = nSymbolsStart;
	header.nSymbols = nTotalSymbols;
	header.nOptionalHeaderSize = 0;
	header.flags = 0;
	res.Seek( 0 );
	res.Write( &header, sizeof(header) );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void GenerateReferenceObj( CMemoryStream *pRes, const string &szSymbolName, const vector<string> &refs, 
	const string &szLinkerDirectives )
{
	vector<SSection> sections;
	vector<SSymbol> symbols;
	symbols.push_back( SSymbol( szSymbolName, 1 ) );
	for ( size_t k = 0; k < refs.size(); ++k )
		symbols.push_back( SSymbol( refs[k], IMAGE_SYM_UNDEFINED ) );
	{
		SSection s;
		s.szName = ".text";
		for ( size_t k = 0; k < refs.size(); ++k )
			s.relocs.push_back( SSection::SReloc( k * 4 + 1, k + 1 ) );
		s.data.resize( refs.size() * 4 + 1, 0 );
		s.data[0] = (char)0xc3;
		s.nFlags = IMAGE_SCN_ALIGN_16BYTES | IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_1BYTES;
		sections.push_back( s );
	}
	if ( szLinkerDirectives != "" )
	{
		SSection s;
		s.szName = ".drectve";
		s.data.resize( szLinkerDirectives.size() + 1 );
		strcpy( &s.data[0], szLinkerDirectives.c_str() );
		s.nFlags = IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE;
		sections.push_back( s );
	}
	WriteObj( pRes, sections, symbols );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
void GenerateHookObj( CMemoryStream *pRes, const vector<string> &refs )
{
	vector<SSection> sections;
	vector<SSymbol> symbols;
	symbols.push_back( SSymbol( "ReferenceModuleStubFunction", 1 ) );
	symbols.push_back( SSymbol( "_ReferenceAllModules", 2 ) );
	for ( size_t k = 0; k < refs.size(); ++k )
	{
		SSymbol s( refs[k], IMAGE_SYM_UNDEFINED );
		s.nWeakRef = 0;
		symbols.push_back( s );
	}
	{
		SSection s;
		s.szName = ".text";
		s.data.resize( 1, (char)0xc3 );
		s.nFlags = IMAGE_SCN_ALIGN_16BYTES | IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_1BYTES;
		sections.push_back( s );
	}
	{
		SSection s;
		s.szName = ".text";
		s.data.resize( 1 + refs.size() * 4, 0 );
		s.data[0] = (char)0xc3;
		s.nFlags = IMAGE_SCN_ALIGN_16BYTES | IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_1BYTES;
		for ( size_t k = 0; k < refs.size(); ++k )
			s.relocs.push_back( SSection::SReloc( k * 4 + 1, k * 2 + 2 ) );
		sections.push_back( s );
	}
	WriteObj( pRes, sections, symbols );
}