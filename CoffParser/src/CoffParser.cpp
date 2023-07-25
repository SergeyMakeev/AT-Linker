// StripComdat.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Streams.h"
#include <iostream>
#include "CoffFile.h"
#include "CoffParser.h"

#include <iostream>
#include <fstream>

namespace CoffParser
{

////////////////////////////////////////////////////////////////////////////////////////////////////
static string GetExportName( const string &szName )
{
	const char* str = szName.c_str();

	bool bIsAlnum = true;
	for ( int k = 0; k < szName.size(); ++k )
	{
		char c = szName[k];
		if ( !isalnum( c ) && c != '_' )
		{
			bIsAlnum = false;
			break;
		}
	}
	if ( bIsAlnum)
	{
		if ( szName[0] == '_' )
			return szName.substr( 1, szName.size() - 1 );
	}
	return szName;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
static bool DoesStartWith( const string &sz, const char *pszCheck )
{
	int nSize = strlen( pszCheck );
	if ( sz.size() <= nSize )
		return false;
	return strncmp( &sz[0], pszCheck, nSize ) == 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
static int GatherPublicSymbols( SCoffImage *p, vector<string> *pResFunc, vector<string> *pResData )
{
	vector<char> sections;
	sections.resize( p->numSections, 0 );
	// export data & mark non merging comdats

	for ( int k = 0; k < p->numSymbols; k += p->GetNumAuxSymbols(k) + 1 )
	{
		SCoffImage::SCoffSymbolBigObj symb;
		p->GetSymbol(k, &symb);
		//SCoffImage::SCoffSymbol &symb = p->pSymbols[k];
		int nSection = symb.nSection;
		if ( nSection > 0 && nSection <= sections.size() )
		{
			if ( symb.nType == 0 && symb.nStorageClass == IMAGE_SYM_CLASS_EXTERNAL )
			{
				// variable
				string szName;
				GetName( *p, symb.szName, &szName );
				if ( DoesStartWith( szName, "??" ) )
					continue;
				if ( DoesStartWith( szName, "__real" ) )
					continue;
				pResData->push_back( GetExportName( szName ) );
				//std::cout << "data " << szName.c_str() << std::endl;
			}
			if ( strncmp(symb.szName, ".text", 5 ) == 0 && symb.nAuxSymbols >= 1 && symb.nStorageClass == IMAGE_SYM_CLASS_STATIC )
			{
				//SCoffImage::SCoffSectionDefinitionBigObj section;
				byte nSelection = p->GetSectionSelectionFromSectionDefinition(k+1);
				//SCoffImage::SCoffSectionDefinition &def = *(SCoffImage::SCoffSectionDefinition*)&p->pSymbols[k+1];
				if ( nSelection == IMAGE_COMDAT_SELECT_NODUPLICATES )
					sections[nSection - 1] = 1;
			}
		}
	}
	// export functions
	for ( int k = 0; k < p->numSymbols; k += p->GetNumAuxSymbols(k) + 1 )
	{
		SCoffImage::SCoffSymbolBigObj symb;
		p->GetSymbol(k, &symb);
		//SCoffImage::SCoffSymbol &symb = p->pSymbols[k];
		int nSection = symb.nSection;
		if ( 
			nSection > 0 && nSection <= sections.size() && 
			symb.nType == 0x20 &&
			symb.nStorageClass == IMAGE_SYM_CLASS_EXTERNAL
			)
		{
			// skip merging comdats
			if ( ( p->pSections[ nSection - 1 ].flags & IMAGE_SCN_LNK_COMDAT ) != 0 )
			{
				if ( sections[ nSection - 1 ] == 0 )
					continue;
			}
			// function
			string szName;
			GetName( *p, symb.szName, &szName );
			pResFunc->push_back( GetExportName( szName ) );
			//std::cout << "func " << szName.c_str() << std::endl;
		}
	}


	return 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
int CoffImageParser::Process( const char *pszFileName, std::vector<std::string> &exportFuncs, std::vector<std::string> &exportData )
{
	SCoffImage src;
	{
		CFileStream f;
		f.OpenRead( pszFileName );
		if ( !src.Read( f ) )
		{
			return -1;
		}
	}
	if ( src.timeStamp == 0 )
	{
		// ignore our generated obj file
		return 0;
	}
	return GatherPublicSymbols( &src, &exportFuncs, &exportData );
}
////////////////////////////////////////////////////////////////////////////////////////////////////
static bool IsIgnored( const char *record, const vector<string> &ignores )
{
	for each (const string &ignore in ignores)
	{
		if (strstr(record, ignore.c_str()) != nullptr)
		{
			return true;
		}
	}

	return false;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
int CoffImageParser::GenerateResults( const std::vector<std::string> & exportFuncs, const std::vector<std::string> & exportData, std::vector<std::string> & results)
{
	vector<string> ignores;
	{
		std::ifstream f( "DefBuildIgnores.txt" );
		if ( f.is_open() )
		{
			char buffer[2048];
			while ( !f.eof() ) 
			{
				buffer[0] = 0;
				f.getline(buffer, 2048);

				if ( buffer[0] ) 
				{
					ignores.push_back( buffer );
				}
			}
		}
	}

	results.push_back("EXPORTS");

	for ( int k = 0; k < exportFuncs.size(); ++k )
	{
		if ( !IsIgnored(exportFuncs[k].c_str(), ignores) )
		{
			results.push_back(exportFuncs[k]);
		}
	}

	//for ( int k = 0; k < exportData.size(); ++k )
	//{
	//	if ( !IsIgnored(exportData[k].c_str(), ignores) )
	//	{
	//		results.push_back(exportData[k]);
	//	}
	//}

	return 0;
}
////////////////////////////////////////////////////////////////////////////////////////////////////
}
