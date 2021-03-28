#ifdef SB_WIN_BUILD
	#define PlugInExport __declspec(dllexport)
#else
	#define PlugInExport
#endif

#include <stdio.h>
#include "../../licensedinterfaces/basicstringinterface.h"
#include "x2dome.h"

#define PLUGIN_NAME "X2Dome RTI-Dome"

class SerXInterface;
class TheSkyXFacadeForDriversInterface;
class SleeperInterface;
class BasicIniUtilInterface;
class LoggerInterface;
class MutexInterface;
class TickCountInterface;


extern "C" PlugInExport int sbPlugInDisplayName(BasicStringInterface& str);

extern "C" PlugInExport int sbPlugInFactory(	const char* pszSelection,
												const int& nInstanceIndex,
												SerXInterface					* pSerXIn,
												TheSkyXFacadeForDriversInterface* pTheSkyXIn,
												SleeperInterface		* pSleeperIn,
												BasicIniUtilInterface  * pIniUtilIn,
												LoggerInterface			* pLoggerIn,
												MutexInterface			* pIOMutexIn,
												TickCountInterface		* pTickCountIn,
												void** ppObjectOut);
