//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once


namespace TaskUtils {


	using namespace System;
	using namespace System::Collections;
	using namespace System::Data;
	using namespace System::Data::Common;
	using namespace System::Runtime::InteropServices;

	ref class LogQuery;
	ref class LogRecordset;
	ref class LogRecord;

	[Reflection::DefaultMember("Item")]
	public ref class LogParserDataReader :    IDataReader, IDataRecord, IEnumerable
	{
	private:
		System::Data::DataTable ^m_Schema;
		
		LogQuery     ^m_Query;
		LogRecordset ^m_RecordSet;
		LogRecord    ^m_CurrentRecord;

		int TypeInteger;
		int TypeReal;
		int TypeString;
		int TypeTimeStamp;
		int TypeNull;
		
		array<int>                ^m_columnLPTypes;
		array<String ^>           ^m_columnNames;
		System::Collections::Generic::Dictionary<System::String ^, int> ^m_columnOrdinals;
		array<Type^>              ^m_columnTypes;

	public:


		LogParserDataReader(void);

		void DeduceSchema();

		// IEnumerable
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
		property int FieldCount 
		{
			virtual int get() {
				throw gcnew System::Data::DataException();
			}
		};
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


		~LogParserDataReader(void);

	};

};
