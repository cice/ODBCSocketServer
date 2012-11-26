//globals.cpp 
#include "stdafx.h"
#include "globals.h"

//convert variant to XML BSTR
//done once as thread process, once directly
DWORD VariantToXML(void * vPass)
{
	CComBSTR bsXML = "<result>\n";
	HRESULT hr; //result
	SAFEARRAY* psa = NULL; //array
	long lUBound, lURowBound; //upper bound
	long lIndex[2]; //index
	_variant_t vResult; //result
	_variant_t vIn = ((varPass*)vPass)->vValue;

	if (vIn.vt == (VT_ARRAY | VT_VARIANT))
	{

		psa = V_ARRAY(&vIn);
		//get the size of the 2nd dimension
		hr = SafeArrayGetUBound(psa, 2, &lUBound);
		if (FAILED(hr)) goto ErrorExit;

		//get size of first dim
		hr = SafeArrayGetUBound(psa, 1, &lURowBound);
		if (FAILED(hr)) goto ErrorExit;

		for (lIndex[0] = 0; lIndex[0] <= lURowBound; lIndex[0]++)
		{
			bsXML += "<row>\n";

			for (lIndex[1] = 0; lIndex[1] <= lUBound; lIndex[1]++)
			{
				VariantInit(&vResult);

				//start column heading
				bsXML += "<column>";

				try{
					//convert var to string
					SafeArrayGetElement(psa, &lIndex[0], &vResult);
					bsXML += CComBSTR((VarToStr(vResult)).c_str());
				}
				catch(...)
				{}

				bsXML += "</column>\n";
				//clear variant
				VariantClear(&vResult);
			}
			bsXML += "</row>\n";
		}
	}
ErrorExit:
	bsXML += "</result>\0";
	((varPass*)vPass)->bsValue = bsXML;
	return 0;
}

//simple variant conversion
std::string VarToStr(VARIANT vrVar){
   BSTR	bsTemp;
   long lTemp=0; 
   short sTemp=0;
   char cbuf[50];
	USES_CONVERSION;

   if (vrVar.scode == DISP_E_PARAMNOTFOUND){
		return "";
	}

   switch (vrVar.vt){
		case VT_BSTR:	
			{
				bsTemp=vrVar.bstrVal; 
				return OLE2T(vrVar.bstrVal);
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
		default:return "";
		}
   return "";
}