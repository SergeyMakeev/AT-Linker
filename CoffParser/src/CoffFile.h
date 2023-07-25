#pragma once

typedef unsigned short word;
typedef unsigned int dword;
typedef unsigned char byte;

#define Intel80386 (0x014c)
#define IntelAMD64 (0x8664)

//
// https://download.microsoft.com/download/e/b/a/eba1050f-a31d-436b-9281-92cdfeae4b45/pecoff.doc
// https://en.wikibooks.org/wiki/X86_Disassembly/Windows_Executable_Files
//
// https://github.com/tuliom/binutils-gdb/blob/master/bfd/coffcode.h

// binutils-gdb вообще рулят!! там описание большинства obj и прочих файлов которые компайлер выплевывает
// https://github.com/tuliom/binutils-gdb/blob/7f3c034326ce5d487e897826a12c3a4b9d457b49/include/coff/pe.h
// https://github.com/tuliom/binutils-gdb/blob/7f3c034326ce5d487e897826a12c3a4b9d457b49/bfd/coffcode.h
//

#pragma pack(push, 1)
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef char SCoffName[8];
struct SCoffImage
{
/*
	static const char bigObjclassID[16] =
	{
		0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xa9, 0x4b, 0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8
	};
*/

	struct SCoffHeaderBigObj
	{
		/* ANON_OBJECT_HEADER_V2 header.  */
		word Sig1;
		word Sig2;
		word Version;
		word machine;
		dword timeStamp;
		unsigned char classID[16];
		dword sizeOfData;
		dword flags;
		dword metaDataSize;
		dword metaDataOffset;

		/* BIGOBJ specific.  */
		dword nSections;
		dword pSymbols;
		dword nSymbols;
	};

	struct SCoffSymbolBigObj
	{
		SCoffName szName;
		int nValue;
		dword nSection;
		word nType;
		byte nStorageClass;
		byte nAuxSymbols;
	};
	static_assert(sizeof(SCoffSymbolBigObj) == 20, "Invalid struct size");


	struct SCoffSectionDefinitionBigObj
	{
		dword dwSize;
		word nRelocs;
		word nLineNumbers;
		dword dwChecksum;
		word nNumber;
		byte nSelection;
		byte reserved;
		word highNumber;
		byte unused[2];
	};
	static_assert(sizeof(SCoffSectionDefinitionBigObj) == 20, "Invalid struct size");



	struct SCoffHeader
	{
		word machine;
		word nSections;
		dword timeStamp;
		dword pSymbols;
		dword nSymbols;
		word nOptionalHeaderSize;
		word flags;
	};
	struct SCoffSection
	{
		SCoffName szName;
		dword dwImageSize;
		dword dwAddrStart;
		dword dwSize;
		dword pData;
		dword pRelocs;
		dword pLineNumbers;
		word nRelocs;
		word nLineNumbers;
		dword flags;
	};
	struct SCoffRelocation
	{
		dword dwOffset;
		dword nSymbol;
		word type;
	};
	struct SCoffSymbol
	{
		SCoffName szName;
		int nValue;
		word nSection;
		word nType;
		byte nStorageClass;
		byte nAuxSymbols;
	};
	static_assert(sizeof(SCoffSymbol) == 18, "Invalid struct size");

	struct SCoffSectionDefinition
	{
		dword dwSize;
		word nRelocs;
		word nLineNumbers;
		dword dwChecksum;
		word nNumber;
		byte nSelection;
		byte unused[3];
	};
	static_assert(sizeof(SCoffSectionDefinition) == 18, "Invalid struct size");

	struct SCoffWeakExternal
	{
		dword dwTagIndex;
		dword dwFlags;
		byte unused[10];
	};

	dword timeStamp;
	int numSymbols;
	int numSections;
	int nameOffset;
	SCoffSection *pSections;
	vector<byte> image;


	SCoffSymbol *pSymbolsStd;
	SCoffSymbolBigObj *pSymbolsBig;

	struct SymDesc
	{
		byte numAuxSymbols;
		int nSection;
	};



	byte GetNumAuxSymbols(int index)
	{
		if (pSymbolsStd)
		{
			return pSymbolsStd[index].nAuxSymbols;
		}

		return pSymbolsBig[index].nAuxSymbols;
	}

	void GetSymbol (int index, SCoffSymbolBigObj * result)
	{
		if (pSymbolsStd)
		{
			result->nAuxSymbols = pSymbolsStd[index].nAuxSymbols;
			result->nSection = pSymbolsStd[index].nSection;
			result->nStorageClass = pSymbolsStd[index].nStorageClass;
			result->nType = pSymbolsStd[index].nType;
			result->nValue = pSymbolsStd[index].nValue;
			memcpy(result->szName, pSymbolsStd[index].szName, sizeof(SCoffName));
			return;
		}

		result->nAuxSymbols = pSymbolsBig[index].nAuxSymbols;
		result->nSection = pSymbolsBig[index].nSection;
		result->nStorageClass = pSymbolsBig[index].nStorageClass;
		result->nType = pSymbolsBig[index].nType;
		result->nValue = pSymbolsBig[index].nValue;
		memcpy(result->szName, pSymbolsBig[index].szName, sizeof(SCoffName));
	}

	byte GetSectionSelectionFromSectionDefinition (int index)
	{
		if (pSymbolsStd)
		{
			SCoffImage::SCoffSectionDefinition &def = *(SCoffImage::SCoffSectionDefinition*)&pSymbolsStd[index];
			return def.nSelection;
		}

		SCoffImage::SCoffSectionDefinitionBigObj &def = *(SCoffImage::SCoffSectionDefinitionBigObj*)&pSymbolsBig[index];
		return def.nSelection;
	}

	bool ParseCoff(SCoffHeader* header, const char* fileName)
	{
		if ( header->machine != Intel80386 && header->machine != IntelAMD64 )
		{
			std::cout << fileName << " is not a COFF file (invalid machine type)" << std::endl;
			return false;
		}
		if ( header->nOptionalHeaderSize != 0 )
		{
			std::cout << "optional header is not supported " << fileName << std::endl;
			return false;
		}

		pSections = (SCoffSection*)&image[ sizeof(SCoffHeader) ];
		pSymbolsStd = (SCoffSymbol*)&image[ header->pSymbols ];
		pSymbolsBig = nullptr;
		nameOffset = header->pSymbols + header->nSymbols * sizeof(SCoffSymbol);
		numSymbols = header->nSymbols;
		numSections = header->nSections;
		timeStamp = header->timeStamp;
		return true;
	}

	bool ParseCoffBigObj(SCoffHeaderBigObj* header, const char* fileName)
	{
		static const unsigned char bigObjclassID[16] =
		{
			0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xa9, 0x4b, 0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8
		};

		if ( header->machine != Intel80386 && header->machine != IntelAMD64 )
		{
			std::cout << fileName << " is not a COFF file (invalid machine type)" << std::endl;
			return false;
		}

		for(int i = 0; i < 16; i++)
		{
			if (header->classID[i] != bigObjclassID[i])
			{
				std::cout << fileName << " has invalid class ID for bigobj" << std::endl;
				return false;
			}
		}

		pSections = (SCoffSection*)&image[ sizeof(SCoffHeaderBigObj) ];

		pSymbolsStd = nullptr;
		pSymbolsBig = (SCoffSymbolBigObj*)&image[ header->pSymbols ];

		nameOffset = header->pSymbols + header->nSymbols * sizeof(SCoffSymbolBigObj);
		numSymbols = header->nSymbols;
		numSections = header->nSections;
		timeStamp = header->timeStamp;

		return true;
	}

	bool Read( CDataStream &f )
	{
		image.resize( f.GetSize() );
		f.Read( &image[0], image.size() );

		SCoffHeaderBigObj* pBigObjHeader = (SCoffHeaderBigObj*)&image[0];
		SCoffHeader* pHeader = (SCoffHeader*)&image[0];
		if(pBigObjHeader->Sig1 == 0x0 && pBigObjHeader->Sig2 == 0xFFFF && pHeader->machine != Intel80386 && pHeader->machine != IntelAMD64 )
		{
			//this is bigobj
			bool r = ParseCoffBigObj(pBigObjHeader, f.GetName());
			if (r == false)
				return false;
		} else
		{
			bool r = ParseCoff(pHeader, f.GetName());
			if (r == false)
				return false;
		}

		return true;
	}
};
#pragma pack(pop)
////////////////////////////////////////////////////////////////////////////////////////////////////
inline void GetName( const SCoffImage &image, const SCoffName &a, string *pRes )
{
	const int *pA = (const int*)&a;
	if ( pA[0] != 0 )
	{
		const int N_TEMP_BUF = 10;
		char szBuf[N_TEMP_BUF];
		memset( szBuf, 0, N_TEMP_BUF );
		ASSERT( N_TEMP_BUF > sizeof(a) );
		strncpy( szBuf, (const char*)&a, sizeof(a) );
		*pRes = szBuf;
		return;
	}
	*pRes = (const char*)&image.image[ image.nameOffset + pA[1] ];
}