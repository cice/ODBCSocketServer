/***************************************************************************
                          main-server.h  -  description
                             -------------------
    begin                : Sat Oct 30 1999
    copyright            : (C) 1999 by Team FXML
    email                : fxml@exite.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "stdafx.h"


#include <iostream>
#include <winsock2.h>
#include "ws-util.h"
#include <map>
#include <string>
#include "fxml.h"
#include "globals.h"

using namespace std;

#import "C:\Program Files\Common Files\System\ado\msado20.tlb" no_namespace rename( "EOF", "adoEOF" )


//// Constants /////////////////////////////////////////////////////////

const int kBufferSize = 1024;
#define trim rtrim  /* trim() is synonomous with rtrim() */
		

//// Prototypes ////////////////////////////////////////////////////////

//Complete documentation for all functions can be found in main-server.cpp
SOCKET SetUpListener(int nPort);
SOCKET AcceptConnection(SOCKET ListeningSocket, sockaddr_in& sinRemote);
bool ReadAndWriteSocket(SOCKET sd);
char * alltrim(char *str);
char * ltrim(char *str);
char * rtrim(char *str) ;
FXMLElement* FindChildByName(std::string sChildName, FXMLElement* fxElement, std::list<FXMLElement*>::iterator iChild);
std::string ExecSQL(std::string sConnectionString, std::string sSQL);
DWORD VariantToXML(void * vPass, _RecordsetPtr rsResult);
inline std::string VarToStr(VARIANT vrVar);
std::string GetADOError(_ConnectionPtr Conn1);
void LogEvent(LPCTSTR pFormat, short nSeverity);
bool IsValidConnection(std::string sInet);
void InitializeGlobalVariables();
BOOL VarToBuf(VARIANT *va, char **buf, int *bufLen) ;
std::string SubStrReplace(std::string cOriginal, char* cSearch, char* cReplace);
bool ShutdownConnection(SOCKET sd);
std::string RecordSetToMSDTD(_RecordsetPtr rsResult);
std::string MSDTDToOurDTD(std::string sInput);

// structures//////////////////////////////////////////////////////
//Used for Variant -> XML conversion
struct varPass
{
	_variant_t vValue;
	CComBSTR   bsValue;
};

//// Globals ///////////////////////////////////////////////////////////
typedef std::map<SOCKET, SOCKET > SOCKETMAP;
SOCKETMAP gmSockets;
CRITICAL_SECTION gcsLock;
int gnMaxThreadCount; //max thread count
int gnDebugLevel;	  //debug level
int gnSQLTimeout;	//sql timeout
int gnThreadTimeout; //thread timeout	
std::string gsHostsAllow; //allowed hosts
std::string gsHostsDeny; //denied hosts
BOOLEAN gbUseCData; //use cdata or not
short gnUseMSDTD; //use MS ADO DTD