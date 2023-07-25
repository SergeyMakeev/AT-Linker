#include "stdafx.h"
#include "Streams.h"
#include "ElfFile.h"
#include "ElfParser.h"

#define ELF_ST_TYPE(info) ((info) & 0xf)
#define ELF_ST_BIND(info) ((info) >> 4)

namespace ElfParser
{
	template<typename TOffset>
	struct OffsetAndSize
	{
		TOffset offset;
		TOffset size;
	};

	struct ElfImage
	{
		vector<byte> image;
		byte8 e_shnum;

		int ParseIdentPartHeader( bool &is32bit )
		{
			const int EI_CLASS = 4;
			const int EI_DATA = 5;
			const int EI_VERSION = 6;
			const int ELFDATA2LSB = 1;
			const int ELFCLASS32 = 1;
			const int ELFCLASS64 = 2;

			const ELFIdent &ident = *(ELFIdent*)&image[0];
			if( ident[0] != 0x7f || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F' )
			{
				printf( "Invalid header prefix\n" );
				return -1;
			}

			if( ident[EI_DATA] != ELFDATA2LSB )
			{
				printf( "Support only ELFDATA2LSB encoding now\n" );
				return -2;
			}

			if( ident[EI_CLASS] != ELFCLASS32 && ident[EI_CLASS] != ELFCLASS64 )
			{
				printf( "Invalid file class\n" );
				return -3;
			}

			if( ident[EI_VERSION] != 1 )
			{
				printf( "Unknown file version\n" );
				return -4;
			}

			is32bit = ident[EI_CLASS] == ELFCLASS32;
			return 0;
		}

		template<typename TOffset>
		int GetStringTable( const ElfHeader<TOffset> &header, byte4 sectionIndex, OffsetAndSize<TOffset> &stringTable )
		{
			SectionHeader<TOffset> *sections = (SectionHeader<TOffset> *)&image[(size_t)header.e_shoff];
			if( sectionIndex == 0 || sectionIndex >= e_shnum)
			{
				printf("Section index is out of range\n");
				return -1;
			}

			SectionHeader<TOffset> &section = sections[sectionIndex];
			if( section.sh_size == 0 )
			{
				printf("Section is empty\n");
				return -2;
			}

			stringTable.offset = section.sh_offset;
			stringTable.size = section.sh_size;

			return 0;
		}

		template<typename TOffset>
		int GetObjectStringAndSymbolTables( const ElfHeader<TOffset> &header, OffsetAndSize<TOffset> headersStringTable, int &stringTableIndex, int &symbolTableIndex )
		{
			const int SHT_SYMTAB = 2;
			const int SHT_STRTAB = 3;

			stringTableIndex = -1;
			symbolTableIndex = -1;

			SectionHeader<TOffset> *sections = (SectionHeader<TOffset> *)&image[(size_t)header.e_shoff];
			for (int i = 0; i < e_shnum; i++)
			{
				SectionHeader<TOffset> &section = sections[i];
				if( section.sh_name >= headersStringTable.size )
				{
					printf("Invalid section name index\n");
					return -1;
				}

				char *sectionName = (char *)&image[(size_t)(headersStringTable.offset + section.sh_name)];

				if( section.sh_type == SHT_STRTAB && strcmp(".strtab", sectionName) == 0  )
				{
					if( stringTableIndex != -1 )
					{
						printf("File contains multiple string tables\n");
						return -2;
					}

					stringTableIndex = i;
				}
				else if( section.sh_type == SHT_SYMTAB && strcmp(".symtab", sectionName) == 0  )
				{
					if( symbolTableIndex != -1 )
					{
						printf("File contains multiple symbol tables\n");
						return -3;
					}

					symbolTableIndex = i;
				}
			}

			if( stringTableIndex == -1 )
			{
				printf("File contains no string tables\n");
				return -4;
			}

			if( symbolTableIndex == -1 )
			{
				printf("File contains no symbol tables\n");
				return -5;
			}

			return 0;
		}

		template<typename TOffset>
		int GetSymbols( const ElfHeader<TOffset> &header, int objectSymbolTableIndex, OffsetAndSize<TOffset> objectStringTable, std::vector<std::string> &result )
		{
			const int STT_FUNC = 2;
			const int STB_GLOBAL = 1;
			const int STB_WEAK = 2;

			SectionHeader<TOffset> *sections = (SectionHeader<TOffset> *)&image[(size_t)header.e_shoff];
			SectionHeader<TOffset> &objSymbolTableSec = sections[objectSymbolTableIndex];
			SymbolHeader<TOffset> *symbols = (SymbolHeader<TOffset> *)&image[(size_t)objSymbolTableSec.sh_offset];

			TOffset symbolsCount = objSymbolTableSec.sh_size / objSymbolTableSec.sh_entsize;
			if (symbolsCount * sizeof (SymbolHeader<TOffset>) > objSymbolTableSec.sh_size + 1)
			{
				printf("Invalid sh_entsize\n");
				return -1;
			}

			if( symbolsCount == 0 )
			{
				printf("Symbol table is empty\n");
				return -2;
			}

			char *table = (char *)&image[(size_t)objectStringTable.offset];

			for (int i = 0; i < symbolsCount; i++)
			{
				SymbolHeader<TOffset> &symbol = symbols[i];

				int stType = ELF_ST_TYPE(symbol.st_info);
				int stBind = ELF_ST_BIND(symbol.st_info);

				if( stType != STT_FUNC || ( stBind != STB_GLOBAL /*&& stBind != STB_WEAK - weak ссылка не нужна, потому как там хранятся функции, имеющие реализацию в хедерах */ ) )
					continue;

				if( symbol.st_name >= objectStringTable.size )
				{
					printf("Invalid function name size");
					return -3;
				}

				result.push_back( std::string(&table[symbol.st_name]) );
			}

			return 0;
		}

		template<typename TOffset>
		int Parse( std::vector<std::string> &result )
		{
			int errCode = 0;
			const ElfHeader<TOffset> &header = *(ElfHeader<TOffset> *)&image[0];

			SectionHeader<TOffset> *sections = (SectionHeader<TOffset> *)&image[(size_t)header.e_shoff];
			e_shnum = header.e_shnum;
			if( e_shnum == 0 )
			{
				e_shnum = sections[0].sh_size;
			}
			byte4 shstrndx = header.e_shstrndx;
			if ( shstrndx == 0xffff )
			{
				shstrndx = sections[0].sh_link;
			}

			//////////////////////////////////////////////////////////////////////////
			// Get section header string table
			//////////////////////////////////////////////////////////////////////////
			OffsetAndSize<TOffset> headersStringTable = {0, 0};
			errCode = GetStringTable( header, shstrndx, headersStringTable );
			if( errCode != 0 )
			{
				printf("Invalid section header string table\n");
				return -20 + errCode;
			}

			//////////////////////////////////////////////////////////////////////////
			// Get object string and symbol table indexes
			//////////////////////////////////////////////////////////////////////////
			int objestStringTableIndex = 0;
			int objectSymbolTableIndex = 0;
			errCode = GetObjectStringAndSymbolTables( header, headersStringTable, objestStringTableIndex, objectSymbolTableIndex );
			if( errCode != 0 )
			{
				printf("Invalid file sections\n");
				return -30 + errCode;
			}

			//////////////////////////////////////////////////////////////////////////
			// Get object string table
			//////////////////////////////////////////////////////////////////////////
			OffsetAndSize<TOffset> objectStringTable = {0, 0};
			errCode = GetStringTable( header, objestStringTableIndex, objectStringTable );
			if( errCode != 0 )
			{
				printf("Invalid object string table\n");
				return -40 + errCode;
			}

			//////////////////////////////////////////////////////////////////////////
			// Get function names
			//////////////////////////////////////////////////////////////////////////
			errCode = GetSymbols( header, objectSymbolTableIndex, objectStringTable, result );
			if( errCode != 0 )
			{
				printf("Invalid symbols table\n");
				return -500 + errCode;
			}

			return 0;
		}

		int ReadAndParse( CDataStream &f, std::vector<std::string> &result )
		{
			int errCode = 0;
			image.resize( f.GetSize() );
			f.Read( &image[0], image.size() );

			//////////////////////////////////////////////////////////////////////////
			// Parse header
			//////////////////////////////////////////////////////////////////////////
			bool is32bit = false;
			errCode = ParseIdentPartHeader( is32bit );
			if( errCode != 0 )
			{
				printf("Invalid file header\n");
				return -10 + errCode;
			}

			errCode = is32bit 
				? Parse<byte4>( result )	
				: Parse<byte8>( result );

			return errCode;
		}
	};


	int ElfImageParser::Process( const char *pszFileName, std::vector<std::string> &exportFuncs, std::vector<std::string> &exportData )
	{
		CFileStream f;
		f.OpenRead( pszFileName );

		ElfImage img;

		int errCode = img.ReadAndParse(f, exportFuncs);
		return errCode;
	}

	int ElfImageParser::GenerateResults( const std::vector<std::string> & exportFuncs, const std::vector<std::string> & exportData, std::vector<std::string> & results)
	{
		if( exportFuncs.size() == 0 )
			return 0;

		results.push_back("export: {");

		for ( int i = 0; i < exportFuncs.size(); ++i )
		{
			const std::string &str = exportFuncs[i];
			results.push_back( str );
		}

		results.push_back("}");

		return 0;
	}
}
