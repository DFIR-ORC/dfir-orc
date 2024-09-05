//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include <vcclr.h>

#include "OrcLib.h"

using namespace System;
using namespace System::Collections;
using namespace System::Data;
using namespace System::Data::Common;
using namespace System::Runtime::InteropServices;

class CSVFileReader;
class CSVRecord;

namespace TaskUtils {

	[Reflection::DefaultMember("Item")]
	public ref class CSVDataReader :   
		IDataReader, 
			IDataRecord, 
			IEnumerable
		{

		private:
			CSVFileReader* m_pReader;
			CSVRecord*     m_pCurrentRecord;

			DataTable^     m_Schema;

			System::DateTime m_minvalue;
			System::DateTime m_maxvalue;

			void DeduceSchema();

		public:

			

			void OpenFile(System::String^ pFileName);
			void OpenFile(System::String^ pFileName,  System::Boolean bfirstRowIsColumnNames, System::Char wcSeparator, System::Char wcQuote, System::String^ szDateFormat, System::Int32 dwSkipLines);

			void PeekHeaders();
			void PeekHeaders(System::String^  szHeadersFileName);

			void PeekTypes();
			void PeekTypes(System::Int32 cbLinesToPeek);

			property System::String ^DateFormat {

				void set(System::String ^formatString) {
					if(m_pReader == NULL) Marshal::ThrowExceptionForHR(E_POINTER);
					HRESULT hr = E_FAIL;

					pin_ptr<const wchar_t> szFormatString = PtrToStringChars(formatString);

					if(FAILED(hr = m_pReader->SetDateFormat(szFormatString)))  Marshal::ThrowExceptionForHR(hr);
				};
			};

			property int FieldCount 
			{
				virtual int get() {
					const CSVRecordSchema& schema = m_pReader->GetSchema();
					return schema.dwNbColumns;
				}
			};

			virtual IEnumerator^ GetEnumerator();

			// IDataReader

			virtual void Close(void);
			virtual System::Data::DataTable ^GetSchemaTable(void);
			virtual bool NextResult(void);
			virtual bool Read(void);
			virtual property int Depth {int get(void);};
			virtual property bool IsClosed {bool get(void);};
			virtual property int RecordsAffected {int get(void);};
			// IDataRecord

			virtual property System::Object ^Item[int]{ virtual System::Object ^get(int); };
			virtual property System::Object ^Item[System::String ^]{ virtual System::Object ^get(System::String ^);};

			virtual System::String ^GetName(int);
			virtual System::String ^GetDataTypeName(int);
			virtual System::Type ^GetFieldType(int);
			virtual System::Object ^GetValue(int);
			virtual int GetValues(cli::array<System::Object ^,1> ^);
			virtual int GetOrdinal(System::String ^);

			virtual bool GetBoolean(int);
			virtual unsigned char GetByte(int);
			virtual __int64 GetBytes(int,__int64,cli::array<unsigned char,1> ^,int,int);
			virtual wchar_t GetChar(int);
			virtual __int64 GetChars(int,__int64,cli::array<wchar_t,1> ^,int,int);
			virtual System::Guid GetGuid(int);
			virtual short GetInt16(int);
			virtual int GetInt32(int);
			virtual __int64 GetInt64(int);
			virtual float GetFloat(int);
			virtual double GetDouble(int);
			virtual System::String ^GetString(int);
			virtual System::Decimal GetDecimal(int);
			virtual System::DateTime GetDateTime(int);
			virtual System::Data::IDataReader ^GetData(int);
			virtual bool IsDBNull(int);


			// Deallocate the native object on a destructor
			~CSVDataReader() {
				delete m_pReader;
			}

		protected:
			// Deallocate the native object on the finalizer just in case no destructor is called
			!CSVDataReader() {
				delete m_pReader;
			}


		};
};
