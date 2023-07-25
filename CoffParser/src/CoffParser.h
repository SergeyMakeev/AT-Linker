#pragma once
#include "ObjectParser.h"

namespace CoffParser
{
	class CoffImageParser : public ObjectParser::ImageParser
	{
	public:
		virtual int Process( const char *pszFileName, std::vector<std::string> &exportFuncs, std::vector<std::string> &exportData ) override;
		virtual int GenerateResults( const std::vector<std::string> & exportFuncs, const std::vector<std::string> & exportData, std::vector<std::string> & results) override;
	};
}