//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "XmlLiteExtension.h"

using namespace Orc;

HRESULT XmlLiteExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_CreateXmlReader, "CreateXmlReader");
        Get(m_CreateXmlWriter, "CreateXmlWriter");

        Get(m_CreateXmlReaderInputWithEncodingName, "CreateXmlReaderInputWithEncodingName");
        Get(m_CreateXmlWriterOutputWithEncodingName, "CreateXmlWriterOutputWithEncodingName");

        m_bInitialized = true;
    }
    return S_OK;
}

HRESULT XmlLiteExtension::LogError(HRESULT err, IXmlReader* pReader)
{
    LPCWSTR szErrMessage = NULL;

    switch (err)
    {
        case MX_E_INPUTEND:
            szErrMessage = L"unexpected end of input";
            break;
        case MX_E_ENCODING:
            szErrMessage = L"unrecognized encoding";
            break;
        case MX_E_ENCODINGSWITCH:
            szErrMessage = L"unable to switch the encoding";
            break;
        case MX_E_ENCODINGSIGNATURE:
            szErrMessage = L"unrecognized input signature";
            break;
        case WC_E_WHITESPACE:
            szErrMessage = L"whitespace expected";
            break;
        case WC_E_SEMICOLON:
            szErrMessage = L"semicolon expected";
            break;
        case WC_E_GREATERTHAN:
            szErrMessage = L"'>' expected";
            break;
        case WC_E_QUOTE:
            szErrMessage = L"quote expected";
            break;
        case WC_E_EQUAL:
            szErrMessage = L"equal expected";
            break;
        case WC_E_LESSTHAN:
            szErrMessage = L"well - formedness constraint : no '<' in attribute value";
            break;
        case WC_E_HEXDIGIT:
            szErrMessage = L"hexadecimal digit expected";
            break;
        case WC_E_DIGIT:
            szErrMessage = L"decimal digit expected";
            break;
        case WC_E_LEFTBRACKET:
            szErrMessage = L"'[' expected";
            break;
        case WC_E_LEFTPAREN:
            szErrMessage = L"'(' expected";
            break;
        case WC_E_XMLCHARACTER:
            szErrMessage = L"illegal xml character";
            break;
        case WC_E_NAMECHARACTER:
            szErrMessage = L"illegal name character";
            break;
        case WC_E_SYNTAX:
            szErrMessage = L"incorrect document syntax";
            break;
        case WC_E_CDSECT:
            szErrMessage = L"incorrect CDATA section syntax";
            break;
        case WC_E_COMMENT:
            szErrMessage = L"incorrect comment syntax";
            break;
        case WC_E_CONDSECT:
            szErrMessage = L"incorrect conditional section syntax";
            break;
        case WC_E_DECLATTLIST:
            szErrMessage = L"incorrect ATTLIST declaration syntax";
            break;
        case WC_E_DECLDOCTYPE:
            szErrMessage = L"incorrect DOCTYPE declaration syntax";
            break;
        case WC_E_DECLELEMENT:
            szErrMessage = L"incorrect ELEMENT declaration syntax";
            break;
        case WC_E_DECLENTITY:
            szErrMessage = L"incorrect ENTITY declaration syntax";
            break;
        case WC_E_DECLNOTATION:
            szErrMessage = L"incorrect NOTATION declaration syntax";
            break;
        case WC_E_NDATA:
            szErrMessage = L"NDATA expected";
            break;
        case WC_E_PUBLIC:
            szErrMessage = L"PUBLIC expected";
            break;
        case WC_E_SYSTEM:
            szErrMessage = L"SYSTEM expected";
            break;
        case WC_E_NAME:
            szErrMessage = L"name expected";
            break;
        case WC_E_ROOTELEMENT:
            szErrMessage = L"one root element";
            break;
        case WC_E_ELEMENTMATCH:
            szErrMessage = L"well - formedness constraint : element type match";
            break;
        case WC_E_UNIQUEATTRIBUTE:
            szErrMessage = L"well - formedness constraint : unique attribute spec";
            break;
        case WC_E_TEXTXMLDECL:
            szErrMessage = L"text / xmldecl not at the beginning of input";
            break;
        case WC_E_LEADINGXML:
            szErrMessage = L"leading \"xml\"";
            break;
        case WC_E_TEXTDECL:
            szErrMessage = L"incorrect text declaration syntax";
            break;
        case WC_E_XMLDECL:
            szErrMessage = L"incorrect xml declaration syntax";
            break;
        case WC_E_ENCNAME:
            szErrMessage = L"incorrect encoding name syntax";
            break;
        case WC_E_PUBLICID:
            szErrMessage = L"incorrect public identifier syntax";
            break;
        case WC_E_PESINTERNALSUBSET:
            szErrMessage = L"well - formedness constraint : pes in internal subset";
            break;
        case WC_E_PESBETWEENDECLS:
            szErrMessage = L"well - formedness constraint : pes between declarations";
            break;
        case WC_E_NORECURSION:
            szErrMessage = L"well - formedness constraint : no recursion";
            break;
        case WC_E_ENTITYCONTENT:
            szErrMessage = L"entity content not well formed";
            break;
        case WC_E_UNDECLAREDENTITY:
            szErrMessage = L"well - formedness constraint : undeclared entity";
            break;
        case WC_E_PARSEDENTITY:
            szErrMessage = L"well - formedness constraint : parsed entity";
            break;
        case WC_E_NOEXTERNALENTITYREF:
            szErrMessage = L"well - formedness constraint : no external entity references";
            break;
        case WC_E_PI:
            szErrMessage = L"incorrect processing instruction syntax";
            break;
        case WC_E_SYSTEMID:
            szErrMessage = L"incorrect system identifier syntax";
            break;
        case WC_E_QUESTIONMARK:
            szErrMessage = L"'?' expected";
            break;
        case WC_E_CDSECTEND:
            szErrMessage = L"no ']]>' in element content";
            break;
        case WC_E_MOREDATA:
            szErrMessage = L"not all chunks of value have been read";
            break;
        case WC_E_DTDPROHIBITED:
            szErrMessage = L"DTD was found but is prohibited";
            break;
        case WC_E_INVALIDXMLSPACE:
            szErrMessage = L"xml:space attribute with invalid value";
            break;
        case NC_E_QNAMECHARACTER:
            szErrMessage = L"illegal qualified name character";
            break;
        case NC_E_QNAMECOLON:
            szErrMessage = L"multiple colons in qualified name";
            break;
        case NC_E_NAMECOLON:
            szErrMessage = L"colon in name";
            break;
        case NC_E_DECLAREDPREFIX:
            szErrMessage = L"declared prefix";
            break;
        case NC_E_UNDECLAREDPREFIX:
            szErrMessage = L"undeclared prefix";
            break;
        case NC_E_EMPTYURI:
            szErrMessage = L"non default namespace with empty uri";
            break;
        case NC_E_XMLPREFIXRESERVED:
            szErrMessage = L"\"xml\" prefix is reserved and must have the http ://www.w3.org/XML/1998/namespace URI";
            break;
        case NC_E_XMLNSPREFIXRESERVED:
            szErrMessage = L"\"xmlns\" prefix is reserved for use by XML";
            break;
        case NC_E_XMLURIRESERVED:
            szErrMessage =
                L"xml namespace URI(http://www.w3.org/XML/1998/namespace) must be assigned only to prefix \"xml\"";
            break;
        case NC_E_XMLNSURIRESERVED:
            szErrMessage = L"xmlns namespace URI(http ://www.w3.org/2000/xmlns/) is reserved and must not be used";
            break;
        case SC_E_MAXELEMENTDEPTH:
            szErrMessage = L"element depth exceeds limit in XmlReaderProperty_MaxElementDepth";
            break;
        case SC_E_MAXENTITYEXPANSION:
            szErrMessage = L"entity expansion exceeds limit in XmlReaderProperty_MaxEntityExpansion";
            break;
        case WR_E_NONWHITESPACE:
            szErrMessage = L"writer: specified string is not whitespace";
            break;
        case WR_E_NSPREFIXDECLARED:
            szErrMessage = L"writer : namespace prefix is already declared with a different namespace";
            break;
        case WR_E_NSPREFIXWITHEMPTYNSURI:
            szErrMessage =
                L"writer : It is not allowed to declare a namespace prefix with empty URI(for example xmlns : p = "
                L"\"\").";
            break;
        case WR_E_DUPLICATEATTRIBUTE:
            szErrMessage = L"writer : duplicate attribute";
            break;
        case WR_E_XMLNSPREFIXDECLARATION:
            szErrMessage = L"writer : can not redefine the xmlns prefix";
            break;
        case WR_E_XMLPREFIXDECLARATION:
            szErrMessage = L"writer : xml prefix must have the http ://www.w3.org/XML/1998/namespace URI";
            break;
        case WR_E_XMLURIDECLARATION:
            szErrMessage =
                L"writer : xml namespace URI(http ://www.w3.org/XML/1998/namespace) must be assigned only to prefix "
                L"\"xml\"";
            break;
        case WR_E_XMLNSURIDECLARATION:
            szErrMessage =
                L"writer : xmlns namespace URI(http ://www.w3.org/2000/xmlns/) is reserved and must not be used";
            break;
        case WR_E_NAMESPACEUNDECLARED:
            szErrMessage = L"writer : namespace is not declared";
            break;
        case WR_E_INVALIDXMLSPACE:
            szErrMessage =
                L"writer : invalid value of xml : space attribute(allowed values are \"default\" and \"preserve\")";
            break;
        case WR_E_INVALIDACTION:
            szErrMessage = L"writer : performing the requested action would result in invalid XML document";
            break;
        case WR_E_INVALIDSURROGATEPAIR:
            szErrMessage = L"writer : input contains invalid or incomplete surrogate pair";
            break;
        case XML_E_INVALID_DECIMAL:
            szErrMessage = L"character in character entity is not a decimal digit as was expected.";
            break;
        case XML_E_INVALID_HEXIDECIMAL:
            szErrMessage = L"character in character entity is not a hexadecimal digit as was expected.";
            break;
        case XML_E_INVALID_UNICODE:
            szErrMessage = L"character entity has invalid Unicode value.";
            break;
        default:
            break;
    }

    if (pReader != nullptr)
    {
        UINT dwLineNumber = 0L, dwLinePosition = 0L;
        pReader->GetLineNumber(&dwLineNumber);
        pReader->GetLinePosition(&dwLinePosition);

        Log::Error(
            L"XmlLite: '{}' (line: {}, pos: {} [{}])", szErrMessage, dwLineNumber, dwLinePosition, SystemError(err));
    }
    else
    {
        Log::Error(L"XmlLite: {} [{}]", szErrMessage, SystemError(err));
    }
    return S_OK;
}

XmlLiteExtension::~XmlLiteExtension() {}
