#include "stdafx.h"
#include "ObjectParser.h"

#include <iostream>
#include <fstream>

namespace ObjectParser
{
	static void MergeStrings( vector<string> *pRes )
	{
		typedef hash_map<string, bool> CHashStr;
		CHashStr d;
		for ( int k = 0; k < pRes->size(); ++k )
			d[ (*pRes)[k] ];

		pRes->resize(0);
		for ( CHashStr::iterator i = d.begin(); i != d.end(); ++i )
			pRes->push_back( i->first );
		sort( pRes->begin(), pRes->end() );
	}

	int GenerateDef(const std::vector<std::wstring> &objFilesList, ImageParser &img, std::vector<std::string> &results)
	{
		std::string ansiFileName;

		vector<string> exportFuncs;
		vector<string> exportData;

		for ( int k = 0; k < objFilesList.size(); ++k )
		{
			const std::wstring & unicodeFileName = objFilesList[k];
			ansiFileName = std::string( unicodeFileName.begin(), unicodeFileName.end() );

			int errCode = img.Process( ansiFileName.c_str(), exportFuncs, exportData );
			if (errCode != 0)
			{
				printf("Error GenerateDef, in obj file: %s\n", ansiFileName.c_str());
				return errCode;
			}
		}

		MergeStrings( &exportFuncs );
		MergeStrings( &exportData );

		int errCode = img.GenerateResults( exportFuncs, exportData, results );
		if ( errCode != 0)
		{
			return errCode;
		}

		return 0;
	}

	bool DefIsChanged(const wchar_t *defName, const std::vector<std::string> &newDef)
	{
		std::ifstream f( defName );

		if ( !f.good() )
			return true;

		int index = 0;

		while ( !f.eof() )
		{
			char szBuf[10000];
			f.getline( szBuf, 10000 );

			if (index >= newDef.size())
				return true;

			if (strcmp(newDef[index].c_str(), szBuf) != 0)
			{
				return true;
			}
		}

		return false;
	}

	int WriteToDef(const wchar_t *defName, const std::vector<std::string> &newDef)
	{
		HANDLE file = CreateFile(defName, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file == INVALID_HANDLE_VALUE)
		{
			printf("Can't create %S\n", defName);
			return -1;
		}

		for ( int k = 0; k < newDef.size(); ++k )
		{
			std::string s = newDef[k];
			s += "\n";

			DWORD bytesCount = s.length();
			DWORD bytesWritten;
			BOOL res = WriteFile(file, s.c_str(), bytesCount, &bytesWritten, NULL);
			if (res == FALSE || bytesWritten != bytesCount)
			{
				printf("%S file write error!\n", defName);
				return -2;
			}
		}

		CloseHandle(file);

		return 0;
	}

	bool CheckFirstLine( const wchar_t* defName, const char* expectedFirstLine )
	{
		std::ifstream f( defName );
		if ( !f.good() )
			return false;

		char firstLineBuf[100];
		f.getline( firstLineBuf, 100 );
		return strcmp( firstLineBuf, expectedFirstLine ) == 0;
	}

}