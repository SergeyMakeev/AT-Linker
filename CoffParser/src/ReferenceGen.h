#pragma once

class CMemoryStream;
void GenerateReferenceObj( CMemoryStream *pRes, const string &szSymbolName, const vector<string> &refs, 
	const string &szLinkerDirectives );
void GenerateHookObj( CMemoryStream *pRes, const vector<string> &refs );