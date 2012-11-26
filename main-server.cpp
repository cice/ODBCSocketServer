/***************************************************************************
                          main-server.cpp  -  description
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
#include "main-server.h"
#include <fstream>
#include <iostream>

using namespace std;
///////////////////////////////////////////////////////
//
//	Name: ProcessSocket
//	Params: sdPass - socket to read/write to 
//	Returns: none
//	Description: The worker thread function. listens to socket
///////////////////////////////////////////////////////
void ProcessSocket(void* sdPass)
{
	SOCKET *sd= (SOCKET*)(sdPass);

	//init OLE
	CoInitialize(NULL);

	//read and write the socket
	ReadAndWriteSocket((*sd));

	CoUninitialize();

	//end the thread
	_endthread();
}

///////////////////////////////////////////////////////
//
//	Name: DoWinsock
//	Params: pcAddress - localhost address
//		nPort - port to listen to 
//	Returns: int
//	Description: Creates new thread for each socket request.  The main
//		socket function.
///////////////////////////////////////////////////////

int DoWinsock(int nPort, BOOL* bGo)
{
	LogEvent("DoWinsock reports establishing listener", 1);
	SOCKET ListeningSocket = SetUpListener(htons(nPort));
	if (ListeningSocket == INVALID_SOCKET) {
		LogEvent(WSAGetLastErrorMessage("establish listener"), 3);
		return 3;
	}

	InitializeCriticalSection(&gcsLock);

	while ((*bGo)) {
		LogEvent("DoWinsock reports waiting for a connection", 1);
		sockaddr_in sinRemote;
		SOCKET sd = AcceptConnection(ListeningSocket, sinRemote);
		if (sd != INVALID_SOCKET) {
			LogEvent(((std::string)"DoWinsock reports connection established from" + (std::string)inet_ntoa(sinRemote.sin_addr)).c_str() , 1);
		}
		else {
			LogEvent(WSAGetLastErrorMessage("accept connection"),2);
			return 3;
		}

		InitializeGlobalVariables();

		//if too many threads active, do not accept connection
		if ((gmSockets.size() > gnMaxThreadCount) || (!IsValidConnection(inet_ntoa(sinRemote.sin_addr))))
		{
			ShutdownConnection(sd);
		}
		else
		{
			//insert socket into open socket list
			EnterCriticalSection(&gcsLock);
			try
			{
				gmSockets.insert(SOCKETMAP::value_type(sd, sd));
			}
			catch(...)
			{
				LogEvent("Exception thrown in socketmap insert", 2);
			};
			LeaveCriticalSection(&gcsLock);

			//start processing
			_beginthread(ProcessSocket, 0, &sd);
		}
	}

	//close any outstanding connections
	SOCKETMAP::iterator iSocket;
	for (iSocket = gmSockets.begin(); iSocket != gmSockets.end(); iSocket++)
	{
		ShutdownConnection((*iSocket).first);
		gmSockets.erase(iSocket);
	}

	DeleteCriticalSection(&gcsLock);

	return 0;
}


///////////////////////////////////////////////////////
//
//	Name: SetUpListener
//	Params: pcAddress - localhost address
//		nPort - port to listen to 
//	Returns: Socket
//	Description: Sets up a listener on the given interface and port, returning the
// listening socket if successful; if not, returns INVALID_SOCKET.
///////////////////////////////////////////////////////

SOCKET SetUpListener(int nPort)
{
	SOCKET sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd != INVALID_SOCKET) {
		sockaddr_in sinInterface;
		sinInterface.sin_family = AF_INET;
		sinInterface.sin_addr.s_addr = htonl (ADDR_ANY);
		sinInterface.sin_port = nPort;
		if (bind(sd, (sockaddr*)&sinInterface, 
				sizeof(sockaddr_in)) != SOCKET_ERROR) {
			listen(sd, SOMAXCONN);
			return sd;
		}
	}

	return INVALID_SOCKET;
}


///////////////////////////////////////////////////////
//
//	Name: AcceptConnection
//	Params: ListeningSocket - socket to accept
//		sinRemote - remote client address
//	Returns: Socket
//	Description: Accepts a socket connection.  If error
//		returns INVALID_SOCKET
///////////////////////////////////////////////////////
SOCKET AcceptConnection(SOCKET ListeningSocket, sockaddr_in& sinRemote)
{
	int nAddrSize = sizeof(sinRemote);
	return accept(ListeningSocket, (sockaddr*)&sinRemote, &nAddrSize);
}


///////////////////////////////////////////////////////
//
//	Name: ReadAndWriteSocket
//	Params: sd - socket to read/write from
//	Returns: BOOLEAN success
//	Description: Reads XML query from socket, writes
//		XML result set
///////////////////////////////////////////////////////
bool ReadAndWriteSocket(SOCKET sd)
{
	// Read data from client
	char acReadBuffer[kBufferSize];
	int nReadBytes = 0; //number of bytes read
	std::string sRead, sTemp, sResult; //string to read, and result
	int nFind; //result var
	BOOL bAllDone = FALSE;//whether the transaction is finished or not

	//variables for setting up timeout
	fd_set read_fds; 
	struct timeval TimeOut; 
	FD_ZERO(&read_fds); 
	FD_SET(sd, &read_fds); 
	TimeOut.tv_sec = gnThreadTimeout; //use timeout of durartion gnThreadTimeout
	TimeOut.tv_usec = 0; 

	try
	{ 
		if (select(FD_SETSIZE, &read_fds, NULL, NULL, &TimeOut) != SOCKET_ERROR) 
		{ 
			if (FD_ISSET(sd, &read_fds)) 
			{ 
				do {
					nReadBytes = recv(sd, acReadBuffer, kBufferSize, 0);
					if (nReadBytes > 0) {
						LogEvent("ReadWriteSocket reports receiving client data", 1);

						//copy over inbound buffer
						sTemp = acReadBuffer;
						sRead.append(sTemp.substr(0,nReadBytes));
						sTemp.erase();

						//search for the end of XML.  If found, we have received
						//all data.  If not found, keep receiving data
						nFind = sRead.find("</request>");
						if (nFind > 0)
						{

							//parse out request
							FXMLParser fxParse; //xml parser
							FXMLElement *fxElem = NULL;
							
							try
							{
								fxElem = fxParse.ParseString(sRead.c_str()); //parse string
							}
							catch(...)
							{
								LogEvent("ReadAndWriteSocket reports fXML Parser exception.", 2);
								break;
							}

							FXMLElement *fxRet = NULL;
							std::string sConnectionString, sSQL;

							//if string is not XML  , exit
							if ((fxElem == NULL) || (fxParse.sError.length() > 0))
							{
								LogEvent("ReadWriteSocket reports receiving invalid XML string", 2);
								break;
							}

							//get connection string
							fxRet = FindChildByName("connectionstring", fxElem, fxElem->mChildren->begin());				
							if (fxRet != NULL) 
								sConnectionString = fxRet->Value();
							else
							{
								LogEvent("ReadWriteSocket reports no connection string found.",2);
								break;
							}

							fxRet = NULL;

							//get SQL
							fxRet =  FindChildByName("sql", fxElem, fxElem->mChildren->begin());				
							if (fxRet != NULL)
								sSQL = fxRet->Value();
							else
							{
								LogEvent("ReadWriteSocket reports no SQL string found.",2);
								break;
							}

							if ((sConnectionString.length() == 0) || (sSQL.length() == 0)) break;

							//now sub in > for &lt; , etc..
							sSQL = SubStrReplace(sSQL, "&lt;", "<" );
							sSQL = SubStrReplace(sSQL, "&gt;", ">");
							sSQL = SubStrReplace(sSQL, "&amp;", "&");

							LogEvent(((std::string)"ReadWriteSocket reports executing SQL:" + sSQL).c_str(), 1);

							//now execute SQL 
							sResult = ExecSQL(sConnectionString, sSQL);

							//now send back response
							nReadBytes = sResult.length();

							int nSentBytes = 0;
							while (nSentBytes < nReadBytes) {
								nSentBytes = send(sd, sResult.c_str() + nSentBytes,
										nReadBytes - nSentBytes, 0);
								if (nSentBytes <= 0)
								{
									//closed connection or something
									LogEvent("ReadWriteSocket reports no data to return", 1);
									break;
								}
							}//while
							bAllDone = TRUE;
							sRead = "";
						} //nfind > 0
						//look for termination request
					}//nreadbytes > 0
				} while ((nReadBytes > 0) && (!bAllDone));
			}//FD_ISSET
		}//select
		else if (nReadBytes == SOCKET_ERROR) {
			LogEvent("ReadWriteSocket reports socket error after data return.  Not necessarily a problem.", 1);
		}

		if (!bAllDone) LogEvent("ReadWriteSocket reports invalid data received, no SQL executed.", 2);
	}
	catch(...)
	{
		LogEvent("ReadAndWriteSocket reports fatal exception reading data.",2);
	}

	try
	{
		SOCKETMAP::iterator iSocket; //socket iterator

		if (ShutdownConnection(sd)) {
			LogEvent("ReadAndWriteSocket reports closing connection", 1);
		}
		else {
			LogEvent(WSAGetLastErrorMessage("shutdown connection"),2);
		}

		EnterCriticalSection(&gcsLock);

		try
		{
			iSocket = gmSockets.find(sd);
			gmSockets.erase(iSocket);
		}
		catch(...)
		{
		}

		LeaveCriticalSection(&gcsLock);
	}
	catch(...)
	{
		LogEvent("ReadAndWriteSocket reports fatal exception closing connection.",2);
	}

	
	return true;
}

///////////////////////////////////////////////////////
//
//	Name: ExecSQL
//	Params: sConnectionString - Connection string to use
//			sSQL - SQL to execute
//	Returns: XML result of SQL statements
//	Description: Executes SQL on specified database.
//		Returns result set in XML
///////////////////////////////////////////////////////
std::string ExecSQL(std::string sConnectionString, std::string sSQL)
{
	USES_CONVERSION;

	//result string
	std::string sResult = "<?xml version=\"1.0\"?>\r\n";
	_variant_t vArray; //result variant
	VARIANT vNull;
	HRESULT hr;

	_ConnectionPtr	Conn1;	//the connection to the database
	_CommandPtr	Cmd1;		//the command object
	_RecordsetPtr rsResult;		//recordset to be returned
	varPass stPass; //to pass to getvar xml

	try{
		hr = Conn1.CreateInstance(__uuidof(Connection));
		Conn1->ConnectionString = T2OLE(sConnectionString.c_str());
		Conn1->ConnectionTimeout = gnSQLTimeout;
		Conn1->CommandTimeout = gnSQLTimeout;

		Conn1->Open(L"", L"", L"", -1);

		hr = Cmd1.CreateInstance(__uuidof(Command));
		Cmd1->ActiveConnection = Conn1;
		//now, execute the SQL Statement
		Cmd1->CommandText = T2OLE((char*)sSQL.c_str());

		//create the recordset object
		hr = rsResult.CreateInstance(_uuidof(Recordset));
		
		rsResult->PutRefSource(Cmd1);

		//initialize the optional parameters
		VariantInit(&vNull);
		vNull.vt = VT_ERROR;
		vNull.scode = DISP_E_PARAMNOTFOUND;

		//execute the SQL command directly
		rsResult->Open(T2OLE((char*)sSQL.c_str()), vNull, adOpenForwardOnly, adLockReadOnly, adCmdText);

		//if the recordset is closed, no data is returned.  Successful execution
		if (rsResult->State ==  adStateClosed )
		{
			Conn1 = NULL;
			rsResult = NULL;
			Cmd1 = NULL;
			sResult += "<result state=\"success\">\r\n</result>\r\n";
			return sResult;
		}

		if (rsResult->adoEOF == VARIANT_FALSE)
		{
			rsResult->Move(0);

			if (gnUseMSDTD == 1)
			{
				//use MS DTD to retrieve data
				sResult = RecordSetToMSDTD(rsResult);
			}
			else if (gnUseMSDTD == 2)
			{

				//convert recordset to string, then to our DTD
				sResult = RecordSetToMSDTD(rsResult);
				sResult = MSDTDToOurDTD(sResult);
			}
			else /*if (gnUseMSDTD == 0)*/
			{
				//get the result set rows
				vArray = rsResult->GetRows(adGetRowsRest);

				//convert array to string
				//return the string
				stPass.vValue = vArray;
				stPass.bsValue = L"";
				VariantToXML(&stPass, rsResult);
				sResult += OLE2T(stPass.bsValue);
			}
		}
		else
		{
			Conn1 = NULL;
			rsResult = NULL;
			Cmd1 = NULL;
			sResult += "<result state=\"success\">\r\n</result>\r\n";
			return sResult;
		}
	}
	catch(_com_error &e)
	{
		std::string sError = GetADOError(Conn1);
		sResult += (std::string)"<result state=\"failure\">\r\n<error>" + sError + (std::string)"</error>\r\n</result>\r\n";
	}
	catch(...)
	{
		sResult += (std::string)"<result state=\"failure\">\r\n<error>Unknown Error</error>\r\n</result>\r\n";
	};

	//close everything up!
	Conn1 = NULL;
	rsResult = NULL;
	Cmd1 = NULL;

	return sResult;

}

///////////////////////////////////////////////////////
//
//	Name: GetADOError
//	Params: Conn1 - connection pointer
//	Returns: ADO error string
//	Description: determines ADO error(s) that occurred 
///////////////////////////////////////////////////////
std::string GetADOError(_ConnectionPtr Conn1)
{
	USES_CONVERSION;
   ErrorsPtr   Errs1 = NULL;
    ErrorPtr    Err1  = NULL;

	std::string sRet("ADO Error. Description: ");
    long        nCount;

   try
    {
	    if( Conn1 == NULL ) return sRet + (std::string)"NULL Connection";
  
		// Enumerate Errors Collection and display properties of each object.
        Errs1  = Conn1->GetErrors();
        nCount = Errs1->GetCount();

        // Loop through Errors Collection
        for( long i = 0; i < nCount; i++ )
        {
            // Get Error Item
            Err1 = Errs1->GetItem((_variant_t)((long)i) );
			sRet += (std::string)" Description: " + (std::string)OLE2T(Err1->GetDescription());
		}
	
		if( Errs1 != NULL )  Errs1->Release(); 
		if( Err1  != NULL )  Err1->Release();  
   }
   catch(...)
   {
	   sRet += " Unknown Error";
   }
	
   return sRet;
}

///////////////////////////////////////////////////////
//
//	Name: MSDTDToOurDTD
//	Params: sInput - string to convert
//	Returns: XML string in our format
//	Description: converts from XML string in MS format to our format
///////////////////////////////////////////////////////
std::string MSDTDToOurDTD(std::string sInput)
{
	FXMLParser fxParse; //xml parser
	FXMLElement *fxElem = NULL;
	std::string sResult = "<?xml version=\"1.0\" ?>\r\n";
	FXMLElement *fxRet = NULL; //search element
	std::list<FXMLElement*>::iterator iChildLoc;	//iterator for children
	std::map<std::string, std::string> mChildAttributes; //attributes
	std::map<std::string, std::string>::iterator iAttribLoc;	

	try
	{
		fxElem = fxParse.ParseString(sInput.c_str()); //parse string
	}
	catch(...)
	{
		LogEvent("MSDTDToOurDTD reports fXML Parser exception.", 2);
		return sResult + (std::string)"<result state=\"failure\">\r\n<error>Unknown exception</error>\r\n</result>\r\n";
	}

	//the idea is to iterate through each rs:data column and print it out in our format
	fxRet = FindChildByName("rs:data", fxElem, fxElem->mChildren->begin());

	if (fxRet != NULL)
	{
		if (fxRet->mChildren != NULL)
		{
			if (!fxRet->mChildren->empty())
			{
				sResult += "<result state=\"success\">\r\n";
    			for (iChildLoc = fxRet->mChildren->begin(); iChildLoc != fxRet->mChildren->end(); ++iChildLoc)
				{
					sResult += "<row>\r\n";
					if (*iChildLoc)
					{
						//get the attribute list here
						mChildAttributes = (*iChildLoc)->mAttributes;
						if (!mChildAttributes.empty())
						{
							for (iAttribLoc = mChildAttributes.begin(); iAttribLoc != mChildAttributes.end(); ++iAttribLoc)
							{
								//always print out the name too because if a field is null,
								//it will not print out!
								sResult += "<column name=\"" + (*iAttribLoc).first + "\">" + (*iAttribLoc).second + "</column>\r\n";
							}
						}
				   }
					sResult += "</row>\r\n";
				}
			}
		}
	}

	sResult += "</result>\r\n";
	return sResult;
}

//converts recordset to MS dtd
std::string RecordSetToMSDTD(_RecordsetPtr rsResult)
{
	std::string sRet = "ERROR: Conversion failure"; //return string
	std::string sTempName = "c:\\temp\\msxml.tmp"; //temp file name
	//first, get temp file name to save recordset to
	DWORD dwTmpResult;  //temporary results
	LPTSTR lpTmpDir = (char*)malloc(256);
	LPTSTR lpTmpFileName = (char*)malloc(256);  //the directory names
	DWORD dwBufferSize = 256;	//buffer size
	LPCTSTR lpcPrefix = "DTZ";	//prefix for the temp file
	UINT uResult;	//result of the temp file
	UINT uPass = 0;	
	std::string sBuffer(""); //line of file to read
	BOOLEAN bError = FALSE;

	dwTmpResult = GetTempPath(dwBufferSize, lpTmpDir);
	if (dwTmpResult != 0)
	{
		uResult = ::GetTempFileNameA(lpTmpDir, lpcPrefix, uPass, lpTmpFileName);
		//now save the file name by accessing the map
		sTempName = lpTmpFileName;

	}

	if (!sTempName.empty()) 
	{
	  //save the recordser to disk
	  try
	  {
		remove(sTempName.c_str());
		rsResult->Save(sTempName.c_str(), adPersistXML);
	  }
      catch(_com_error e)
	  {
	 	sRet = e.ErrorMessage();
		bError = TRUE;
	  }

	  //now read file into string 
	   std::ifstream File(sTempName.c_str(), ios::in);
	  //open the file
	  if(File)
	  {
		//read file into string
		if (!bError)
		{
			sRet = "";
			while(!File.eof())
			{
				std::getline(File, sBuffer);
				sBuffer.append("\r\n");
				sRet.append(sBuffer);
			}
		}
		//now remove the file
		File.close();
		remove(sTempName.c_str());
	  }
	}

	free(lpTmpDir);
	free(lpTmpFileName);
	return sRet;
}



///////////////////////////////////////////////////////
//
//	Name: FindChildByName
//	Params: fxElement - FXMLElement to search
//			sChildName - child name to return
//	Returns: fxElement with name sChildName, or NULL if not found
//	Description: searches lr an fxElement for child name sChildName
//		returns fxElement or NULL if not found
///////////////////////////////////////////////////////

FXMLElement* FindChildByName(std::string sChildName, FXMLElement* fxElement, std::list<FXMLElement*>::iterator iChild)
{
	//recursively search through child nodes until element name is found
	if (fxElement == NULL)
	{
		LogEvent("FindChildByName reports no child found.", 2);
		return NULL; //sanity check
	}

	std::list<FXMLElement*>::iterator iChildLoc;	//iterator for children
	FXMLElement *fxRet = NULL; //return element
	std::string sCompare = alltrim(_strlwr((char*)fxElement->Name().c_str()));

	//stopping condition
	if (sCompare.compare((std::string)alltrim(_strlwr((char*)sChildName.c_str()))) == 0) 
	{
		return fxElement;
	}

	if (fxElement->mChildren != NULL)
	{
		if (!fxElement->mChildren->empty())
		{
    	    for (iChildLoc = fxElement->mChildren->begin(); iChildLoc != fxElement->mChildren->end(); ++iChildLoc)
            {
                if (*iChildLoc)
                {
					//recursive call here
					fxRet = FindChildByName(sChildName, (*iChildLoc), iChildLoc);
					if (fxRet != NULL) return fxRet;
               }
			}
		}
	}
	return NULL;
}


///////////////////////////////////////////////////////
//
//	Name: LogEvent
//	Params: pFormat - Message string
//		nSeverity - severity of event
//	Returns: none
//	Description: Logs Event to event log IF severity is >= gnDebugLevel
//nSeverity = 1 for informational
//			= 2 for warnings
//			= 3 for errors
///////////////////////////////////////////////////////
void LogEvent(LPCTSTR pFormat, short nSeverity)
{
    TCHAR    chMsg[2048];
    HANDLE  hEventSource;
    LPTSTR  lpszStrings[1];
    va_list pArg;

	//ensure event is severe enough to log
	if (nSeverity < gnDebugLevel) return;
	if (strlen(pFormat) > 2040) return;

    va_start(pArg, pFormat);
    _vstprintf(chMsg, pFormat, pArg);
    va_end(pArg);

    lpszStrings[0] = chMsg;

     /* Get a handle to use with ReportEvent(). */
    hEventSource = RegisterEventSource(NULL, gsServiceName);
    if (hEventSource != NULL)
    {
        /* Write to event log. */
		WORD wEventSource = EVENTLOG_INFORMATION_TYPE;
		if (nSeverity == 1) wEventSource = EVENTLOG_INFORMATION_TYPE;
		else if (nSeverity == 2) wEventSource = EVENTLOG_WARNING_TYPE;
		else if (nSeverity == 3) wEventSource = EVENTLOG_ERROR_TYPE;

        ReportEvent(hEventSource, wEventSource, 0, 0, NULL, 1, 0, (LPCTSTR*) &lpszStrings[0], NULL);
        DeregisterEventSource(hEventSource);
    }

 }


//code to trim whitespace
 
/* Remove trailing whitespace */
 
char * rtrim(char *str) {
  char *s, *p; int c;
  s = p = str;
  while ((c = *s++)) if (c > ' ') p = s;
  *p = '\0';
  return str;
}
 
/* Remove leading whitespace */
 
char * ltrim(char *str) {
  char *s, *p;
  for (s = p = str; *s && *s <= ' '; s++) ;
  while ((*p++ = *s++)) ;
  return str;
}
 
/* Combination of the two */
 
char * alltrim(char *str) {
  return rtrim(ltrim(str));
}


///////////////////////////////////////////////////////
//
//	Name: VariantToXML
//	Params: vPass - variant containing array to convert 
//	Returns: XML string
//	Description: Converts a variant array into xml string
//		Encapsulates all data in CDATA segment to prevent
//		problems when passing back binary/tagged data
///////////////////////////////////////////////////////
DWORD VariantToXML(void * vPass, _RecordsetPtr rsResult)
{
	CComBSTR bsXML = "<result state=\"success\">\r\n";
	HRESULT hr; //result
	SAFEARRAY* psa = NULL; //array
	long lUBound, lURowBound; //upper bound
	long lIndex[2]; //index
	_variant_t vResult; //result
	_variant_t vIn = ((varPass*)vPass)->vValue;

	try
	{

		if (vIn.vt == (VT_ARRAY | VT_VARIANT))
		{

			psa = V_ARRAY(&vIn);
			//get the size of the 2nd dimension
			hr = SafeArrayGetUBound(psa, 2, &lUBound);
			if (FAILED(hr)) goto ErrorExit;

			//get size of first dim
			hr = SafeArrayGetUBound(psa, 1, &lURowBound);
			if (FAILED(hr)) goto ErrorExit;

			for (lIndex[1] = 0; lIndex[1] <= lUBound; lIndex[1]++)
			{
				bsXML += "<row>\r\n";

				for (lIndex[0] = 0; lIndex[0] <= lURowBound; lIndex[0]++)
				{
					VariantInit(&vResult);

					//start column heading
					//print column names just for the first set of cols returned

					if (lIndex[1] == 0)
					{
						bsXML += "<column name=\"";
						bsXML += (CComBSTR)((BSTR)rsResult->Fields->Item[lIndex[0]]->Name);
						bsXML += "\">";
					}
					else
						bsXML += "<column>";

					try{
						//convert var to string
						SafeArrayGetElement(psa, &lIndex[0], &vResult);
						bsXML += CComBSTR((VarToStr(vResult)).c_str());
					}
					catch(...)
					{}

					bsXML += "</column>\r\n";
					//clear variant
					VariantClear(&vResult);
				}
				bsXML += "</row>\r\n";
			}
		}
	}
	catch(...)
	{
		LogEvent("VariantToXML reports exception", 2);
	}

ErrorExit:
	bsXML += "</result>\r\n";
	((varPass*)vPass)->bsValue = bsXML;
	return 0;
}

///////////////////////////////////////////////////////
//
//	Name: VarToStr
//	Params: vrVar - variant to convert to string
//	Returns: string
//	Description: Converts a variant into a string
///////////////////////////////////////////////////////
inline std::string VarToStr(VARIANT vrVar){
    long lTemp=0;  //temp long
   short sTemp=0; //temp short
   char cbuf[50];
   std::string sCurTemp; //temp string for currency

   USES_CONVERSION;

   try
   {
	   if (vrVar.scode == DISP_E_PARAMNOTFOUND){
			return "";
		}

	   switch (vrVar.vt){
			case VT_BSTR:	
				{
					if (gbUseCData)
					{
						//usr cdata
						sCurTemp = "![CDATA[";
						sCurTemp += SubStrReplace((std::string)OLE2T(vrVar.bstrVal), "]]>", "]]&amp;");
						sCurTemp += "]]>";
					}
					else
					{
						sCurTemp = OLE2T(vrVar.bstrVal);
						sCurTemp = SubStrReplace(sCurTemp, "&", "&amp;");
						sCurTemp = SubStrReplace(sCurTemp, "<", "&lt;");
						sCurTemp = SubStrReplace(sCurTemp, ">", "&gt;");
					}
					return sCurTemp;
					break;
				}
			case VT_I2:
				{
					sTemp=vrVar.iVal; 
					_itoa(sTemp,cbuf,10);
					return (std::string)cbuf;
					break;
				}
			case VT_I4:
				{
					lTemp=vrVar.lVal; 
					_ltoa(lTemp,cbuf,10);						
					return (std::string)cbuf;
					break;
				}
			case VT_R8:
				{
					sprintf(cbuf, "%0.5f", vrVar.dblVal);
					return (std::string)cbuf;
				}
			case VT_R4:
				sprintf(cbuf, "%e", (double)V_R4(&vrVar));
				return (std::string)cbuf;
			case VT_DECIMAL:
 				sprintf(cbuf, "%d", V_I4(&vrVar));
				return (std::string)cbuf;
			case VT_BOOL:
				return ( V_BOOL(&vrVar) ? _T("TRUE") : _T("FALSE"));
			case VT_DATE:
				 SYSTEMTIME sysTime;                                              																				 
				 VariantTimeToSystemTime(vrVar.date, &sysTime);
				 sprintf(cbuf, "%d-%d-%d %d:%d:%d", sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute, sysTime.wSecond);
				 return (std::string)cbuf;
			case VT_CY:
				_i64toa( vrVar.cyVal.int64, cbuf, 10 );
				sCurTemp = cbuf;
				return sCurTemp.substr(0,sCurTemp.length() - 4) + (std::string)"." + sCurTemp.substr(sCurTemp.length() - 4, 4);
			case (VT_ARRAY | VT_UI1):
				{
					//blob
					int nTemp;
					char **cBigPass = new char*;
					if (VarToBuf(&vrVar, cBigPass, &nTemp))
					{
						sCurTemp = "![CDATA[";
						sCurTemp += (std::string)(*cBigPass);
						sCurTemp += "]]>";
						free(cBigPass);
						return sCurTemp;
					}
					else
						return (std::string)"BINARY DATA";
				}
			default:return "";
			}
   }
   catch(...)
   {
	   LogEvent("VarToStr reports exception", 2);
   }
   return "";
}

///////////////////////////////////////////////////////
//
//	Name: SubStrReplace
//	Params: cOriginal - original string to search
//			cSearch - char to search for
//			cReplace - char to replace
//	Returns: string with cSearch replaced by cReplace
//	Description: Searches and replaces occurences of cSearch
//		in cOriginal with cReplace
///////////////////////////////////////////////////////
std::string SubStrReplace(std::string cOriginal, char* cSearch, char* cReplace)
{
	std::string sRet = cOriginal; //set return string
	int nFind; //location of found string

	try
	{
		//search for char string
		nFind = sRet.find(cSearch, 0);
		while (nFind >= 0)
		{
			sRet = sRet.substr(0, nFind) + (std::string)cReplace + sRet.substr(nFind + strlen(cSearch), sRet.length() - nFind);

			nFind = sRet.find(cSearch, nFind + strlen(cReplace));
		}
	}
	catch(...)
	{
		LogEvent("SubStrReplace reports an exception.", 2);
	}

	return sRet;
}

///////////////////////////////////////////////////////
//
//	Name: InitializeGlobalVariables
//	Params: none
//	Returns: none
//	Description: Re-reads registry and sets globals accordingly
///////////////////////////////////////////////////////
void InitializeGlobalVariables()
{

	HKEY hRegKey;	//for the registry
	DWORD dwDataType;	//registry data type
	DWORD dwDataSize = 256;	//registry data size
	char szHostsAllowString[256] ; //connection string
	char szDataString[256]; //event detail setting
	char szHostsDenyString[256]; //DMS PAth setting

	// Set host and port.
	gnMaxThreadCount = 5;
	gnSQLTimeout = 15;
	gnThreadTimeout = 30;
	gsHostsAllow = "*";
	gsHostsDeny = "";
	gbUseCData = FALSE;
	gnUseMSDTD = 0; //default
	gnDebugLevel = 1; //default


	//get debug level
	if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, "SOFTWARE\\ODBCSocketServer",
					  0, KEY_READ, &hRegKey) == ERROR_SUCCESS)
	{
		if (RegQueryValueEx (hRegKey, "DebugLevel", NULL, &dwDataType, (LPBYTE) &szDataString, &dwDataSize) == ERROR_SUCCESS)
			gnDebugLevel = atoi(szDataString);
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "SQLTimeout", NULL, &dwDataType, (LPBYTE) &szDataString, &dwDataSize) == ERROR_SUCCESS)
			gnSQLTimeout = atoi(szDataString);
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "SocketTimeout", NULL, &dwDataType, (LPBYTE) &szDataString, &dwDataSize) == ERROR_SUCCESS)
			gnThreadTimeout = atoi(szDataString);
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "IP.Allow", NULL, &dwDataType, (LPBYTE) &szHostsAllowString, &dwDataSize) == ERROR_SUCCESS)
			gsHostsAllow = szHostsAllowString;
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "IP.Deny", NULL, &dwDataType, (LPBYTE) &szHostsDenyString, &dwDataSize) == ERROR_SUCCESS)
			gsHostsDeny = szHostsDenyString;
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "UseCDATA", NULL, &dwDataType, (LPBYTE) &szDataString, &dwDataSize) == ERROR_SUCCESS)
			gbUseCData = atoi(szDataString);
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "UseMSDTD", NULL, &dwDataType, (LPBYTE) &szDataString, &dwDataSize) == ERROR_SUCCESS)
			gnUseMSDTD = atoi(szDataString);
		dwDataSize = 256;
		if (RegQueryValueEx (hRegKey, "MaxThreadCount", NULL, &dwDataType, (LPBYTE) &szDataString, &dwDataSize) == ERROR_SUCCESS)
			gnMaxThreadCount = atoi(szDataString);
	}
	RegCloseKey(hRegKey);

	//ensure IP.allow and IP.deny are ";" terminated
	if (!gsHostsAllow.empty())
	{
		if (gsHostsAllow.at(gsHostsAllow.length() - 1) != ';') gsHostsAllow += ";";
	}
	if (!gsHostsDeny.empty())
	{
		if (gsHostsDeny.at(gsHostsDeny.length() - 1) != ';') gsHostsDeny += ";";
	}
}


///////////////////////////////////////////////////////
//
//	Name: VarToBuf
//	Params: va - binary variant to convert
//			buf - character buffer
//			buflen - length of buffer returned
//	Returns: boolean success
//	Description: Converts binary variant to char buffer
///////////////////////////////////////////////////////
BOOL VarToBuf(VARIANT *va, char **buf, int *bufLen) 
{
	long lBound; //boundry
	void *varBuf; //temp buffer
	HRESULT result; //result
 
	try
	{
		// Check for the variant type
		if (va->vt != (VT_ARRAY | VT_UI1)) {
		   return FALSE;
		}
 
		// How many bytes?
		if (FAILED(SafeArrayGetUBound(va->parray, 1, &lBound))) {
			  return FALSE;
		}
 
		// OK copy it into our local buffer
		result = SafeArrayAccessData(va->parray, &varBuf);
		if (FAILED(GetScode(result))) {
		  return FALSE;
		}

		lBound++;
		//copy array
		*buf = new char[lBound];
		memcpy(*buf, varBuf, lBound);
		SafeArrayUnaccessData(va->parray);

		//set length
		*bufLen = (int) lBound;
	}
	catch(...)
	{
		LogEvent("VarToBuf reports exception", 2);
	}
	return TRUE;
}

//// ShutdownConnection ////////////////////////////////////////////////
// Gracefully shuts the connection sd down.  Returns true if we're
// successful, false otherwise.

bool ShutdownConnection(SOCKET sd)
{
	// Disallow any further data sends.  This will tell the other side
	// that we want to go away now.  If we skip this step, we don't
	// shut the connection down nicely.
 
	if (shutdown(sd, SD_SEND) == SOCKET_ERROR)
	{
		LogEvent("Socket Error in shutdown function", 1);
	}

	// Receive any extra data still sitting on the socket.  After all
	// data is received, this call will block until the remote host
	// acknowledges the TCP control packet sent by the shutdown above.
	// Then we'll get a 0 back from recv, signalling that the remote
	// host has closed its side of the connection.
	char acReadBuffer[kBufferSize];
	while (1) {
		int nNewBytes = recv(sd, acReadBuffer, kBufferSize, 0);
		if (nNewBytes == SOCKET_ERROR) {
			break;
		}
		else if (nNewBytes != 0) {
			LogEvent("Shutdown connection reports incoming bytes while terminating connection", 2);
		}
		else {
			// Okay, we're done!
			break;
		}
	}


     
	  // Close the socket.
	if (closesocket(sd) == SOCKET_ERROR) {
		LogEvent("Socket Error in closesocket function", 1);
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////
//
//	Name: IsValidConnection
//	Params: sInet - internet address to judge
//	Returns: TRUE if connection valid, FALSE otherwise
//	Description: uses IP.allow and IP.deny to determine
//		if connection is allowed
///////////////////////////////////////////////////////
bool IsValidConnection(std::string sInet)
{
	BOOL bRet = FALSE; //default to false
	BOOL bInHostsAllow, bInHostsDeny ; //loop var
	int i = 0; //loop var
	std::string sCur; //cur string to examine
	std::string sInO1, sInO2, sInO3, sInO4; //incoming ip address octets
	std::string sCompO1, sCompO2, sCompO3, sCompO4; //comparison IP octets
	char ch; //char to examine
	short nOctetCount = 1; //octet count

	//we will first check IP.allow.  If "*", then check IP.deny.
	//If not "*", see if IP address matches entry in IP.allow.  
	//If no match, no access
	
	//first, parse inbound ip address
	for (i = 0; i < sInet.length(); i++)
	{
		ch = sInet.at(i);
		if (ch == '.')
		{
			//set octet
			if (nOctetCount == 1)
			{
				sInO1 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 2)
			{
				sInO2 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 3)
			{
				sInO3 = sCur;
				nOctetCount++;
				sCur = "";
			}
		}
		else
			sCur += ch;
	}
	sInO4 = sCur; //set last octet!

	if (gsHostsAllow.compare("*;") == 0)
	{
		//check IP.deny
		sCur = "";
		nOctetCount = 1;
		i = 0;
		while (i < gsHostsDeny.length())
		{
			ch = gsHostsDeny.at(i);
			if ((ch == ';') || (ch == '.'))
			{
				//assign octet!
				if (nOctetCount == 1)
				{
					sCompO1 = sCur;
					nOctetCount++;
					sCur = "";
				}
				else if (nOctetCount == 2)
				{
					sCompO2 = sCur;
					nOctetCount++;
					sCur = "";
				}
				else if (nOctetCount == 3)
				{
					sCompO3 = sCur;
					nOctetCount++;
					sCur = "";
				}
				else if (nOctetCount == 4)
				{
					sCompO4 = sCur;
					nOctetCount=1;
					sCur = "";
				}
				if (ch == ';')
				{
					//perform the comparison
					//compare first octet
					if (sCompO1.compare("*") == 0)
					{
						//both are *, accept!
						return TRUE;
					}
					if (sCompO1.compare(sInO1) == 0)
					{
						//compare second
						if (sCompO2.compare("*") == 0)
						{
							//wham! denied
							LogEvent(((std::string)"Connection rejected (in IP.deny) from: " + sInet).c_str(), 2);
							return FALSE;
						}
						if (sCompO2.compare(sInO2) == 0)
						{
							//compare third
							if (sCompO3.compare("*") == 0)
							{
								//denied!
								LogEvent(((std::string)"Connection rejected (in IP.deny) from: " + sInet).c_str(), 2);
								return FALSE;
							}
							if (sCompO3.compare(sInO3) == 0)
							{
								//compare 4th
								if (sCompO4.compare("*") == 0)
								{
									//denied
									LogEvent(((std::string)"Connection rejected (in IP.deny) from: " + sInet).c_str(), 2);
									return FALSE;
								}
								if (sCompO4.compare(sInO4) == 0)
								{
									//denied
									LogEvent(((std::string)"Connection rejected (in IP.deny) from: " + sInet).c_str(), 2);
									return FALSE;
								}
							}
						}
					}
				}
			}
			else
			{
				sCur += ch;
			}
			i++;
		}//while

		//if we made it down here, accept
		return TRUE;
	
	}//if IP.allow  = *

	//if IP.allow <> *, then check the IP address individually as above
	//purpose of this loop: to correctly set bInHostsAllow -- is the IP allowed?
	sCur = "";
	nOctetCount = 1;
	i = 0;
	bInHostsAllow = FALSE;
	while (i < gsHostsAllow.length())
	{
		ch = gsHostsAllow.at(i);
		if ((ch == ';') || (ch == '.'))
		{
			//assign octet!
			if (nOctetCount == 1)
			{
				sCompO1 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 2)
			{
				sCompO2 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 3)
			{
				sCompO3 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 4)
			{
				sCompO4 = sCur;
				nOctetCount=1;
				sCur = "";
			}
			if (ch == ';')
			{
				//perform the comparison
				//compare first octet
				if (sCompO1.compare("*") == 0)
				{
					//in hosts allow
					bInHostsAllow = TRUE;
				}
				if (sCompO1.compare(sInO1) == 0)
				{
					//compare second
					if (sCompO2.compare("*") == 0)
					{
						bInHostsAllow = TRUE;
					}
					if (sCompO2.compare(sInO2) == 0)
					{
						//compare third
						if (sCompO3.compare("*") == 0)
						{
							bInHostsAllow = TRUE;
						}
						if (sCompO3.compare(sInO3) == 0)
						{
							//compare 4th
							if (sCompO4.compare("*") == 0)
							{
								bInHostsAllow = TRUE;
							}
							if (sCompO4.compare(sInO4) == 0)
							{
								//specifically in IP.allow.  return true
								return TRUE;
							}
						}
					}
				}
			}
		}
		else
		{
			sCur += ch;
		}
		i++;
	}//while

	if (!bInHostsAllow) 
	{
		//not in hosts allow, reject
		LogEvent(((std::string)"Connection rejected (not in IP.allow) from: " + sInet).c_str(), 2);
		return FALSE;
	}

	//lastly, check if in bInHostsDeny
	bInHostsDeny = FALSE;

	//check IP.deny
	sCur = "";
	nOctetCount = 1;
	i = 0;
	while (i < gsHostsDeny.length())
	{
		ch = gsHostsDeny.at(i);
		if ((ch == ';') || (ch == '.'))
		{
			//assign octet!
			if (nOctetCount == 1)
			{
				sCompO1 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 2)
			{
				sCompO2 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 3)
			{
				sCompO3 = sCur;
				nOctetCount++;
				sCur = "";
			}
			else if (nOctetCount == 4)
			{
				sCompO4 = sCur;
				nOctetCount=1;
				sCur = "";
			}
			if (ch == ';')
			{
				//perform the comparison
				//compare first octet
				if (sCompO1.compare("*") == 0)
				{
					//both are *, accept!
					bInHostsDeny = TRUE;
				}
				if (sCompO1.compare(sInO1) == 0)
				{
					//compare second
					if (sCompO2.compare("*") == 0)
					{
						//wham! denied
						bInHostsDeny =  TRUE;
					}
					if (sCompO2.compare(sInO2) == 0)
					{
						//compare third
						if (sCompO3.compare("*") == 0)
						{
							//denied!
							bInHostsDeny =  TRUE;
						}
						if (sCompO3.compare(sInO3) == 0)
						{
							//compare 4th
							if (sCompO4.compare("*") == 0)
							{
								//denied
								bInHostsDeny =  TRUE;
							}
							if (sCompO4.compare(sInO4) == 0)
							{
								//denied
								bInHostsDeny =  TRUE;
							}
						}
					}
				}
			}
		}
		else
		{
			sCur += ch;
		}
		i++;
	}//while

	if (bInHostsDeny)
	{
		LogEvent(((std::string)"Connection rejected (in IP.deny) from: " + sInet).c_str(), 2);
		return FALSE;
	}
	
	//we are okay!
	return TRUE;
}