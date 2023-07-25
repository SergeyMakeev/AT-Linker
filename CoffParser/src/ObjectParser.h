#pragma once

#include <vector>
#include <string>

namespace ObjectParser
{

class ImageParser
{
public:
	virtual int Process( const char *pszFileName, std::vector<std::string> & exportFuncs, std::vector<std::string> & exportData) = 0;
	virtual int GenerateResults( const std::vector<std::string> & exportFuncs, const std::vector<std::string> & exportData, std::vector<std::string> & results) = 0;
};

int GenerateDef(const std::vector<std::wstring> &objFilesList, ImageParser &img, std::vector<std::string> &results);
bool DefIsChanged(const wchar_t* defName, const std::vector<std::string> & newDef);
bool CheckFirstLine( const wchar_t* defName, const char* expectedFirstLine );
int WriteToDef(const wchar_t* defName, const std::vector<std::string> & newDef);

}