/***************************************************************************
                          fxmlelement.cpp  -  description
                             -------------------
    begin                : Sat Oct 30 1999
    copyright            : (C) 1999 by Team FXML
    email                : fxml@exite.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the BSD License.                                   *
 *                                                                         *
 ***************************************************************************/

#include "stdafx.h"
#include "fxmlelement.h"
#include <iostream>

using namespace std;

///////////////////////////////////////////////////////
//
//	Name: Value
//	Params: set - new value, get - nothing
//	Returns: set - nothing, get - value
//	Description: wrappers for value member
///////////////////////////////////////////////////////

std::string FXMLElement::Value()
{
    return sValue;
}

void FXMLElement::Value(std::string sNewValue)
{
#ifdef _DEBUG
    cout << "Setting Value to: " << sNewValue.c_str() << endl;
#endif
    sValue = sNewValue;
}
///////////////////////////////////////////////////////
//
//	Name: Name
//	Params: set - new Name, get - nothing
//	Returns: set - nothing, get - Name
//	Description: wrappers for Name member
///////////////////////////////////////////////////////

std::string FXMLElement::Name()
{
    return sName;
}

void FXMLElement::Name(std::string sNewName)
{
#ifdef _DEBUG
    cout << "Setting Name to: " << sNewName.c_str() << endl;
#endif
    sName = sNewName;
}

void FXMLElement::SetAttributePair(std::string sAttributeName, std::string sAttributeValue)
{
#ifdef _DEBUG
    cout << "Found attibute pair: " << sAttributeName.c_str() << " --- " << sAttributeValue.c_str() << endl;
#endif
    
    mAttributes.insert(std::make_pair(sAttributeName, sAttributeValue));
}

///////////////////////////////////////////////////////
//
//	Name: Dump
//	Params: none
//	Returns: XML string of fxmlelement variables
//	Description: dumps element contents to a string.  Does not place XML headers
///////////////////////////////////////////////////////

std::string FXMLElement::Dump()
{
	std::string sRet; //string to return
	std::map<std::string, std::string>::iterator iAttribLoc;	//map iterator
	std::list<FXMLElement*>::iterator iChildLoc;	//iterator for children

	sRet = (std::string)"<" + sName + (std::string)" ";

	if (!mAttributes.empty())
	{
		for (iAttribLoc = mAttributes.begin(); iAttribLoc != mAttributes.end(); ++iAttribLoc)
		{
			sRet += (*iAttribLoc).first + (std::string)"=\"" + (*iAttribLoc).second + (std::string)"\" ";
		}
	}

	//close name/attrib section
	sRet += (std::string)">";
	//add value
	sRet += sValue;

	//now process children
	if (mChildren != NULL)
	{
		if (!mChildren->empty())
		{
    	    for (iChildLoc = mChildren->begin(); iChildLoc != mChildren->end(); ++iChildLoc)
            {
                if (*iChildLoc)
                {
					//recursive call here
					sRet += (*iChildLoc)->Dump();
               }
			}
		}
	}
	//close element
	sRet += (std::string)"</" + sName + (std::string)">\n";
	return sRet;
}

FXMLElement::FXMLElement()
{
	//contstructor. Set stuff to NULL
   mChildren = NULL;
   fxParent = NULL;
}

FXMLElement::~FXMLElement()
{
    //destructor, lets erase everything
    fxParent = NULL;
	try
	{
		mAttributes.clear();
		
		if (mChildren)
		{
			// clear the old stack
			std::list<FXMLElement*>::iterator iter;

			if (mChildren != NULL)
			{

				//now clear and delete the list!
				for (iter = mChildren->begin(); iter != mChildren->end(); ++iter)
				{
					if (*iter)
					{
						delete (*iter);
						(*iter) = NULL;
					}
				}

				mChildren->clear();
				delete mChildren;
				mChildren = NULL;
			}
		}
	}
	catch(...)
	{};
}