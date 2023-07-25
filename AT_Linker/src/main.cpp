#include <tchar.h>
#include <windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include <time.h>
#include "../../CoffParser/src/ObjectParser.h"
#include "../../CoffParser/src/CoffParser.h"
#include "../../CoffParser/src/ElfParser.h"

#define LINKER_MAX_PATH (32768)


#pragma comment(lib, "Shlwapi.lib")

#define ERROR_READ_RESPONSE_FILE(x)    -100 + x;
#define ERROR_LOAD_OBJECTIVE_FILE(x)   -200 + x;
#define ERROR_WRITE_OBJECTIVE_FILE(x)  -300 + x;
#define ERROR_NO_LINKER_PATH(x)        -400 + x;
#define ERROR_CREATE_TEMP_FILE(x)      -500 + x;
#define ERROR_WRITE_RESPONDENT_FILE(x) -600 + x;
#define ERROR_CREATE_LINKER_PROCESS(x) -700 + x;
#define ERROR_GENERATE_DEF_FILE(x)     -800 + x;
#define ERROR_WRITE_DEF_FILE(x)        -900 + x;


bool verboseOut = false;

bool isQuote(wchar_t c)
{
	return (c == '\"');
}

struct ImportantParams
{
	std::vector<std::wstring> objList;
	std::vector<std::wstring> olstFiles;
	std::wstring defName;
	std::wstring linkerPath;
	std::wstring objListPath;
	bool hasDef;
	bool hasEmd;
	bool genObjList;

	ImportantParams() : hasDef(false), hasEmd(false), genObjList(false) { }
};

namespace LnkPrm
{
	enum Prm
	{
		None = -1,
		EmptyPrm = 0,
		ResponseFile = 1,
		Defgen = 2,
		DefFile = 3,
		OriginLinker = 4,
		GenerateObjectList = 5,
		Implib = 6,
		O = 7,
		ObjFile = 8,
		ObjListFile = 9,
		OFile = 10,
		EmdFile = 11,
	};
}


__forceinline LnkPrm::Prm FindParameter( const wchar_t *rawParam )
{
	std::wstring param = rawParam;
	param.erase( std::remove_if( param.begin(), param.end(), isQuote ), param.end() );

	const std::wstring::size_type n = param.size();

	if( n == 0 )
		return LnkPrm::EmptyPrm;

	if( param[0] == L'@' )
		return LnkPrm::ResponseFile;

	if( param[0] == '/' && n > 4 )
	{
		if( wcsncmp( &param[1], L"DEF", 3 ) == 0 )
		{
			if( param[4] == ':' )
				return LnkPrm::DefFile;

			if( wcsncmp( &param[4], L"GEN", 3 ) == 0 )
				return LnkPrm::Defgen;
		}

		if( wcsncmp( &param[1], L"lorig:", 6 ) == 0 )
			return LnkPrm::OriginLinker;

		if( wcsncmp( &param[1], L"objlist", 7 ) == 0 )
			return LnkPrm::GenerateObjectList;

		if( wcsncmp( &param[1], L"IMPLIB:", 7 ) == 0 )
			return LnkPrm::Implib;
	}
	else
	{
		if( wcsncmp( &param[0], L"-o", 2 ) == 0 )
			return LnkPrm::O;

		if( n > 2 && wcsncmp( &param[n - 2], L".o", 2 ) == 0 )
			return LnkPrm::ObjFile;

		if( n > 4 && wcsncmp( &param[n - 4], L".emd", 4 ) == 0)
			return LnkPrm::EmdFile;

		if( n > 4 && wcsncmp( &param[n - 4], L".obj", 4 ) == 0 )
			return LnkPrm::ObjFile;

		if( n > 5 && wcsncmp( &param[n - 5], L".olst", 5 ) == 0 )
			return LnkPrm::ObjListFile;
	}

	return LnkPrm::None;
}


void RemoveInvalidParamsAndGetImportant(std::vector<std::wstring> &commandLineParams, ImportantParams &prm)
{
	bool needGenerateDefFile = false;

	bool hasDefOption = false;
	bool hasEmfOption = false;
	bool hasDefGenOption = false;
	bool hasObjPath = false;

	bool needErase = false;
	for(auto it = commandLineParams.begin(); it != commandLineParams.end(); it = needErase ? commandLineParams.erase(it) : it + 1 )
	{
		const std::wstring & param = *it;
		size_t n = param.size();
		LnkPrm::Prm prmIndex = FindParameter( param.c_str() );

		if( verboseOut && prmIndex != LnkPrm::None )
		{
			printf("param: '%d' in string '%s'\n", (int)prmIndex, param.c_str());
		}

		needErase = false;
		switch(prmIndex)
		{
		case LnkPrm::ResponseFile:
			{
				needErase = true;
			}
			break;

		case LnkPrm::Defgen:
			{
				hasDefGenOption = true;
				needErase = true;
			}
			break;

		case LnkPrm::DefFile:
			{
				prm.defName = &param[5];
				prm.defName.erase(std::remove_if(prm.defName.begin(), prm.defName.end(), isQuote), prm.defName.end());
				hasDefOption = true;
			}
			break;

		case LnkPrm::EmdFile:
			{
				int startPos = param[0] == L'\"' ? 1 : 0;
				prm.defName = &param[startPos];
				prm.defName.erase(std::remove_if(prm.defName.begin(), prm.defName.end(), isQuote), prm.defName.end());
				hasEmfOption = true;
			}
			break;

		case LnkPrm::OriginLinker:
			{
				prm.linkerPath = &param[7];
				prm.linkerPath.erase(std::remove_if(prm.linkerPath.begin(), prm.linkerPath.end(), isQuote), prm.linkerPath.end());
				needErase = true;
			}
			break;

		case LnkPrm::GenerateObjectList:
			{
				prm.genObjList = true;
				needErase = true;
			}			
			break;

		case LnkPrm::Implib:
			{
				int startPos = param[8] == L'\"' ? 9 : 8;
				prm.objListPath = &param[startPos];
				hasObjPath = true;
			}
			break;

		case LnkPrm::O:
			{
				++it;
				int startPos = (*it)[0] == L'\"' ? 1 : 0;
				prm.objListPath = &(*it)[startPos];
				hasObjPath = true;
			}
			break;

		case LnkPrm::OFile:
		case LnkPrm::ObjFile:
			{
				std::wstring param_copy = param;
				param_copy.erase( std::remove( param_copy.begin(), param_copy.end(), '\"' ), param_copy.end() );
				prm.objList.push_back(param);
			}
			break;
		case LnkPrm::ObjListFile:
			{
				std::wstring param_copy = param;
				param_copy.erase( std::remove( param_copy.begin(), param_copy.end(), '\"' ), param_copy.end() );
				prm.olstFiles.push_back(param_copy);
				needErase = true;
			}
			break;
		}
	}

	if( hasObjPath )
	{
		// change extension
		size_t ind = prm.objListPath.find_last_of(L'.');
		if( ind < 0)
		{
			ind = prm.objListPath.size();
		}

		prm.objListPath.resize(ind + 5);
		prm.objListPath.replace(ind, 5, L".olst");

		if (verboseOut)
		{
			printf("/OUT: = found, objListPath = '%S'\n", prm.objListPath.c_str());
		}
	}


	prm.hasDef |= hasDefOption && hasDefGenOption;
	prm.hasEmd |= hasEmfOption && hasDefGenOption;
}

void ToMBCS( std::string *pRes, const std::wstring &szSrc )
{
	static int nCodePage = GetACP();

	const int length = int(szSrc.length());

	const int nBuffLen = length * 2 + 10;
	pRes->resize( nBuffLen );
	const int nLength = WideCharToMultiByte( nCodePage, 0, szSrc.c_str(), length, &((*pRes)[0]), nBuffLen, 0, 0 );
	pRes->resize( nLength );
}


void ParseResponseFileUTF16(const wchar_t* rspText, DWORD charsCount, std::vector<std::wstring> & commandLineParams)
{
	bool insideQuotes = false;
	std::wstring argument;
	for(DWORD i = 0; i < charsCount; i++)
	{
		wchar_t chr = rspText[i];

		if (chr == L'\n' && rspText[i+1] == L'\r')
		{
			i++;
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == L'\r' && rspText[i+1] == L'\n')
		{
			i++;
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == L' ' && insideQuotes == false)
		{
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == L'\r' || chr == L'\n')
		{
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == L'\"')
		{
			insideQuotes = !insideQuotes;
		}

		argument += chr;
	}

	if (argument.size() > 0)
	{
		commandLineParams.push_back(argument);
	}
}

void ParseResponseFileAnsi(const char* rspText, DWORD charsCount, std::vector<std::wstring> & commandLineParams)
{
	bool insideQuotes = false;
	std::wstring argument;
	for(DWORD i = 0; i < charsCount; i++)
	{
		char chr = rspText[i];

		if (chr == '\n' && rspText[i+1] == '\r')
		{
			i++;
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == '\r' && rspText[i+1] == '\n')
		{
			i++;
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == ' ' && insideQuotes == false)
		{
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == '\r' || chr == '\n')
		{
			if (!argument.empty())
			{
				commandLineParams.push_back(argument);
				argument = L"";
			}
			continue;
		}

		if (chr == '\"')
		{
			insideQuotes = !insideQuotes;
		}

		argument += chr;
	}

	if (argument.size() > 0)
	{
		commandLineParams.push_back(argument);
	}
}

int ParseResponseFile(const unsigned char* rspFile, DWORD rspSize, std::vector<std::wstring> & commandLineParams, bool &isAnsi)
{	
	if (rspSize < 2)
	{
		printf("Can't parse file - file too small\n");
		return -1;
	}

	if (rspFile[0] == 0xFE && rspFile[1] == 0xFF)
	{
		printf("Can't parse file - invalid BOM header wrong endianness!\n");
		return -2;
	}

	if (rspFile[0] == 0xFF && rspFile[1] == 0xFE)
	{
		DWORD charsCount = (rspSize - 2) / sizeof(wchar_t);
		const wchar_t* rspText = (const wchar_t*)(rspFile + 2);

		isAnsi = false;
		ParseResponseFileUTF16(rspText, charsCount, commandLineParams);
		return 0;
	}
	
	isAnsi = true;
	const char* rspText = (const char*)(rspFile);
	ParseResponseFileAnsi(rspText, rspSize, commandLineParams);
	return 0;
}

bool GetLastWriteTime(const wchar_t* fileName, FILETIME & time)
{
	WIN32_FIND_DATA ff;

	bool bHasDefFile = false;
	HANDLE hf;
	hf = FindFirstFile( fileName, &ff );
	if ( hf != INVALID_HANDLE_VALUE )
	{
		time = ff.ftLastWriteTime;
		return true;
	}
	return false;
}

int GenerateDefFile(const wchar_t* defName, const std::vector<std::wstring> & objFileList, bool useElfFormat)
{
	printf("Generate DEF file '%S'\n", defName);

	FILETIME defTime;
	bool defFileExist = GetLastWriteTime(defName, defTime);
	if (defFileExist == false && verboseOut)
	{
		printf("DEF file is not exist '%S'\n", defName);
	}

	char objCountBuffer[100];
	if( useElfFormat )
	{
		sprintf_s(objCountBuffer, "//ObjectCount=%d", (int)objFileList.size());
	}
	else
	{
		sprintf_s(objCountBuffer, ";ObjectCount=%d", (int)objFileList.size());
	}


	bool bNeedBuildDefFile = !defFileExist;

	if( !bNeedBuildDefFile)
	{
		bNeedBuildDefFile = !ObjectParser::CheckFirstLine( defName, objCountBuffer );
	}

	if( !bNeedBuildDefFile )
	{
		FILETIME objTime;
		for each(const std::wstring & objFilename in objFileList)
		{
			const wchar_t* objName = objFilename.c_str();

			bool objExist = GetLastWriteTime(objName, objTime);
			if (objExist == false)
			{
				printf("Warning. Can't get obj file timestamp. '%S'\n", objName);
				bNeedBuildDefFile = true;
			}

			if ( defFileExist && CompareFileTime( &objTime, &defTime ) > -1 )
			{
				bNeedBuildDefFile = true;
				break;
			}
		}
	}


	if (!bNeedBuildDefFile)
	{
		if (verboseOut)
		{
			printf("DEFGEN: Skip def file update\n");
		}

		return 0;
	}

	if( verboseOut )
	{
		for each(const std::wstring & objFilename in objFileList)
		{
			printf("%S\n", objFilename.c_str());
		}
	}

	// Добавляем в количество использованных объектных файлов, что бы избежать ситуации,
	// когда объектник удаляется а def файл не перегенерируется.
	std::vector<std::string> res;
	res.push_back(std::string(objCountBuffer));


	int errNum = 0;
	if( useElfFormat )
	{
		wchar_t buff[100];
		_wsplitpath_s(defName, 0, 0, 0, 0, buff, 100, 0, 0 );

		wchar_t buff2[100];
		swprintf_s( buff2, L"Library: %s {", buff );

		std::string str;
		ToMBCS( &str, std::wstring(buff2) );

		res.push_back( str );
		errNum = ObjectParser::GenerateDef(objFileList, ElfParser::ElfImageParser(), res);
		res.push_back("}");
	}
	else
	{
		errNum = ObjectParser::GenerateDef(objFileList, CoffParser::CoffImageParser(), res);
	}

	if (errNum != 0)
	{
		printf("DEFGEN: Errors found while parsing obj files\n");
		return ERROR_GENERATE_DEF_FILE(errNum);
	}

	if (!ObjectParser::DefIsChanged(defName, res))
	{
		if (verboseOut)
		{
			printf("DEFGEN: No new exports found\n");
		}
		return 0;
	}

	if (verboseOut)
	{
		printf("DEFGEN: Write to DEF\n");
	}
	errNum = ObjectParser::WriteToDef(defName, res);
	if( errNum != 0)
	{
		return ERROR_WRITE_DEF_FILE(errNum);
	}

	return 0;
}

void JoinString(const std::vector<std::wstring> &lines, std::vector<BYTE> &content)
{
	std::string buffer;
	buffer.reserve(1024);

	bool bInsertNewLine = false;
	for each(const std::wstring &param in lines)
	{
		if (bInsertNewLine)
		{
			content.push_back(0x0D);
			content.push_back(0x0A);
		}

		ToMBCS(&buffer, param);
		for(size_t charIdx = 0; charIdx < buffer.size(); charIdx++)
		{
			char chr = buffer[charIdx];
			content.push_back(chr);
		}

		bInsertNewLine = true;
	}
}

int WriteToFile(const wchar_t *filename, LPCVOID data, DWORD bytesTotal)
{
	HANDLE file = CreateFile(filename, GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		printf("Can't create new file '%S', error: %d\n", filename, GetLastError());
		return -1;
	}

	DWORD bytesWritten = 0;
	BOOL r = WriteFile(file, data, bytesTotal, &bytesWritten, NULL);
	if (r == FALSE || bytesWritten != bytesTotal)
	{
		printf("Can't write file '%S', error: %d\n", filename, GetLastError());
		CloseHandle(file);
		return -2;
	}

	r = FlushFileBuffers(file);
	if (r == FALSE)
	{
		printf("Can't flush file '%S'\n", filename);
		CloseHandle(file);
		return -3;
	}

	CloseHandle(file);

	return 0;
}

int WriteLinesToFile(const wchar_t *filename, const std::vector<std::wstring> &lines, int lineCapacity)
{
	std::vector<BYTE> content;
	content.reserve(lines.size() * lineCapacity * sizeof(BYTE));

	JoinString( lines, content );

	return WriteToFile( filename, &content[0], (DWORD)(content.size() * sizeof(BYTE)) );
}

int ReadResponseFile(const wchar_t *filename, std::vector<std::wstring> & lines, bool &isAnsi)
{
	HANDLE responseFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (responseFile == INVALID_HANDLE_VALUE)
	{
		printf("Can't open file (open file failed), error: %d\n", GetLastError());
		return -10;
	}

	DWORD responseFileSize = GetFileSize(responseFile, NULL);

	unsigned char* rspFile = new unsigned char[responseFileSize];
	if (rspFile == nullptr)
	{
		printf("Can't open file (out of memory)\n");
		return -20;
	}

	memset(rspFile, 0, responseFileSize);

	DWORD readedBytes = 0;
	BOOL res = ReadFile(responseFile, rspFile, responseFileSize, &readedBytes, NULL);
	if (res == FALSE)
	{
		delete[] rspFile;
		rspFile = nullptr;

		printf("Can't open file (can't read file), error: %d\n", GetLastError());
		return -30;
	}

	CloseHandle(responseFile);


	int exitCode = ParseResponseFile(rspFile, responseFileSize, lines, isAnsi);

	delete[] rspFile;
	rspFile = nullptr;

	return exitCode;
}

int GenerateTempFilePath(wchar_t *tempFilePath)
{
	wchar_t tempDirPath[LINKER_MAX_PATH];
	UINT dirPathSize = GetTempPathW(LINKER_MAX_PATH, tempDirPath);

	if( dirPathSize == 0 )
	{
		printf("Can't get temp directory path\n");
		return -1;
	}

	UINT filePathSize = GetTempFileName(tempDirPath, L"lst", 0, tempFilePath);
	if( filePathSize == 0 )
	{
		printf("Can't generate temp filename\n");
		return -2;
	}

	PathRemoveExtension(tempFilePath);
	BOOL res = PathAddExtension(tempFilePath, L".rsp");
	if( res != TRUE)
	{
		printf("Can't change extension temp filename\n");
		return -3;
	}

	return 0;
}

DWORD RunOriginalLinker(wchar_t *commandLine, const wchar_t *currentDirectory)
{
	DWORD dwFlags = CREATE_DEFAULT_ERROR_MODE/* | CREATE_NO_WINDOW*/;

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory( &si, sizeof( STARTUPINFO ) );
	ZeroMemory( &pi, sizeof( PROCESS_INFORMATION ) );
	si.cb = sizeof( STARTUPINFO );
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
	si.wShowWindow = SW_HIDE;

	BOOL res = CreateProcess( NULL, commandLine, NULL, NULL, TRUE, dwFlags, NULL, currentDirectory, &si, &pi );
	if (res == FALSE)
	{
		printf("Can't execute original link.exe! (create process failed), error: %d\n", GetLastError());
		return ERROR_CREATE_LINKER_PROCESS(0);
	}

	DWORD exitCode = 0;
	if ( pi.hProcess )
	{
		WaitForSingleObject( pi.hProcess, INFINITE );
		GetExitCodeProcess( pi.hProcess, &exitCode );
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return exitCode;
}

int _tmain(int argc, _TCHAR* argv[])
{
	std::string version = 
#ifdef _WIN64
		"x64";
#else
		"x86";
#endif

	printf("Allods Team, %s proxy linker v0.15.0\n", version.c_str());

	if (verboseOut)
	{
		printf("[%d] -----------------------------------------------------------\n", GetCurrentProcessId());
	}

	bool isAnsi = false;
	std::vector<std::wstring> cmdLineParams;
	std::vector<std::wstring> responseFileParams;
	wchar_t* responseFileName = nullptr;

	//////////////////////////////////////////////////////////////////////////
	// Parse input args
	//////////////////////////////////////////////////////////////////////////
	for(int i = 1; i < argc; i++)
	{
		wchar_t* p = argv[i];
		if (p[0] == '@')
		{
			if (responseFileName == nullptr)
			{
				responseFileName = (p + 1);
			} 
			else
			{
				printf("Warning: multiple response files detected!\n");
			}
		}

		cmdLineParams.push_back(argv[i]);

		if (verboseOut)
		{
			printf("[%d] %S\n", i, cmdLineParams.back().c_str());
		}
	}

	ImportantParams prms;
	DWORD errCode = 0;

	//////////////////////////////////////////////////////////////////////////
	// Find important params
	//////////////////////////////////////////////////////////////////////////
	RemoveInvalidParamsAndGetImportant(cmdLineParams, prms);
	if (responseFileName)
	{
		errCode = ReadResponseFile(responseFileName, responseFileParams, isAnsi);
		if (errCode != 0)
		{
			wprintf(L"Error on read response file %s\n", responseFileName);
			return ERROR_READ_RESPONSE_FILE(errCode);
		}

		//если в командной строке нет нужных параметров, то возможно они есть в response файле
		RemoveInvalidParamsAndGetImportant(responseFileParams, prms);
	}

	//////////////////////////////////////////////////////////////////////////
	// Load all objective file list
	//////////////////////////////////////////////////////////////////////////
	if( !prms.olstFiles.empty() && !prms.genObjList )
	{
		for each(const std::wstring &path in prms.olstFiles)
		{
			bool ansiOlstFile;
			errCode = ReadResponseFile(path.c_str(), responseFileParams, ansiOlstFile);
			if (errCode != 0)
			{
				wprintf(L"Error on read objective file %s\n", path);
				return ERROR_LOAD_OBJECTIVE_FILE(errCode);
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Generate Def file
	//////////////////////////////////////////////////////////////////////////
	if(!prms.defName.empty() && (prms.hasDef || prms.hasEmd))
	{
		clock_t begin = clock();

		errCode = GenerateDefFile(prms.defName.c_str(), prms.objList, prms.hasEmd );
		if (errCode != 0)
		{
			printf("DEFGEN: Error while generate def file. Error #%d\n", errCode);
		}

		double defGenTimeInSeconds = (clock() - begin) / (double)CLOCKS_PER_SEC;
		printf("DefGen time %3.2f sec\n", defGenTimeInSeconds);
	}

	wchar_t currentDirectory[LINKER_MAX_PATH];
	GetCurrentDirectory( LINKER_MAX_PATH, currentDirectory );

	if (verboseOut)
	{
		printf("Current dir : %S\n", currentDirectory);
	}

	const int reservedPathLenght = 100;
	if( prms.genObjList )
	{
		//////////////////////////////////////////////////////////////////////////
		// Write objective file list and exit
		//////////////////////////////////////////////////////////////////////////
		errCode = WriteLinesToFile(prms.objListPath.c_str(), prms.objList, reservedPathLenght );
		if (errCode != 0)
		{
			wprintf(L"Error on write objective file %s\n", prms.objListPath);
			return ERROR_WRITE_OBJECTIVE_FILE(errCode);
		}

		wprintf(L"   Creating object list %s", prms.objListPath.c_str());
	}
	else
	{
		//////////////////////////////////////////////////////////////////////////
		// Run original linker
		//////////////////////////////////////////////////////////////////////////
		if( prms.linkerPath.empty())
		{
			printf("Parameters contains no origin linker path\n");
			return ERROR_NO_LINKER_PATH(0);
		}

		wchar_t commandLine[LINKER_MAX_PATH];
		wcscpy_s(commandLine, prms.linkerPath.c_str());

		wchar_t copyResponseFileName[LINKER_MAX_PATH];
		copyResponseFileName[0] = L'\0';

		for each(const std::wstring & param in cmdLineParams)
		{
			wcscat_s(commandLine, L" \"");
			wcscat_s(commandLine, param.c_str());
			wcscat_s(commandLine, L"\"");
		}

		if( responseFileParams.size() > 0 )
		{
			errCode = GenerateTempFilePath(copyResponseFileName);
			if (errCode != 0)
			{
				printf("Error on create temp file path\n");
				return ERROR_CREATE_TEMP_FILE(errCode);
			}

			errCode = WriteLinesToFile( copyResponseFileName, responseFileParams, reservedPathLenght );
			if (errCode != 0)
			{
				wprintf(L"Error on write respondent file %s\n", copyResponseFileName);
				return ERROR_WRITE_RESPONDENT_FILE(errCode);
			}

			wcscat_s(commandLine, L" \"@");
			wcscat_s(commandLine, copyResponseFileName);
			wcscat_s(commandLine, L"\"");
		}

		if (verboseOut)
		{
			printf("%S\n", commandLine);
		}

		_flushall();

		clock_t begin = clock();
		errCode = RunOriginalLinker( commandLine, currentDirectory);
		double originalLinkerTimeInSeconds = (clock() - begin) / (double)CLOCKS_PER_SEC;
		printf("Original linker time %3.2f sec\n", originalLinkerTimeInSeconds);


		if (copyResponseFileName[0] != L'\0')
		{
			BOOL res = DeleteFile(copyResponseFileName);
			if (res == FALSE)
			{
				printf("Warning: Can't delete response file copy\n");
			}
		}
	}

	return errCode;
}