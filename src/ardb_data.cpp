/*
 * ardb_data.cpp
 *
 *  Created on: 2013-4-3
 *      Author: wqy
 */
#include "ardb_data.hpp"
#include "util/helpers.hpp"

namespace ardb
{
	int compare_values(const ValueArray& a, const ValueArray& b)
	{
		uint32 minsize = a.size() > b.size() ? b.size() : a.size();
		for (uint32 i = 0; i < minsize; i++)
		{
			int cmp = a[i].Compare(b[i]);
			if (cmp != 0)
			{
				return cmp > 0 ? 1 : -1;
			}
		}
		return a.size() < b.size() ? -1 : (a.size() > b.size() ? 1 : 0);
	}

	bool TableKeyIndex::operator<(const TableKeyIndex& other) const
	{
		return compare_values(keyvals, other.keyvals) < 0 ? true : false;
	}

	Condition::Condition(const std::string& name, CompareOperator compareop,
	        const Slice& value, LogicalOperator logic) :
			keyname(name), cmp(compareop), logicop(logic)
	{
		smart_fill_value(value, keyvalue);
	}

	TableIndexKeyObject::TableIndexKeyObject(const Slice& tablename,
	        const Slice& keyname, const Slice& v, DBID id) :
			KeyObject(tablename, TABLE_INDEX, id), kname(keyname)
	{
		smart_fill_value(v, keyvalue);
	}

	SetKeyObject::SetKeyObject(const Slice& k, const ValueObject& v, DBID id) :
			KeyObject(k, SET_ELEMENT, id), value(v)
	{

	}
	SetKeyObject::SetKeyObject(const Slice& k, const Slice& v, DBID id) :
			KeyObject(k, SET_ELEMENT, id)
	{
		smart_fill_value(v, value);
	}

	ZSetKeyObject::ZSetKeyObject(const Slice& k, const ValueObject& v, double s,
	        DBID id) :
			KeyObject(k, ZSET_ELEMENT, id), value(v), score(s)
	{

	}

	ZSetKeyObject::ZSetKeyObject(const Slice& k, const Slice& v, double s,
	        DBID id) :
			KeyObject(k, ZSET_ELEMENT, id), score(s)
	{
		smart_fill_value(v, value);
	}

	ZSetScoreKeyObject::ZSetScoreKeyObject(const Slice& k, const ValueObject& v,
	        DBID id) :
			KeyObject(k, ZSET_ELEMENT_SCORE, id), value(v)
	{

	}

	ZSetScoreKeyObject::ZSetScoreKeyObject(const Slice& k, const Slice& v,
	        DBID id) :
			KeyObject(k, ZSET_ELEMENT_SCORE, id)
	{
		smart_fill_value(v, value);
	}

	void encode_key(Buffer& buf, const KeyObject& key)
	{
		uint32 header = (uint32) (key.db << 8) + key.type;
		BufferHelper::WriteFixUInt32(buf, header);
		BufferHelper::WriteVarSlice(buf, key.key);
		switch (key.type)
		{
			case HASH_FIELD:
			{
				const HashKeyObject& hk = (const HashKeyObject&) key;
				BufferHelper::WriteVarSlice(buf, hk.field);
				break;
			}
			case LIST_ELEMENT:
			{
				const ListKeyObject& lk = (const ListKeyObject&) key;
				BufferHelper::WriteFixFloat(buf, lk.score);
				break;
			}
			case SET_ELEMENT:
			{
				const SetKeyObject& sk = (const SetKeyObject&) key;
				encode_value(buf, sk.value);
				break;
			}
			case ZSET_ELEMENT:
			{
				const ZSetKeyObject& sk = (const ZSetKeyObject&) key;
				BufferHelper::WriteFixDouble(buf, sk.score);
				encode_value(buf, sk.value);
				break;
			}
			case ZSET_ELEMENT_SCORE:
			{
				const ZSetScoreKeyObject& zk = (const ZSetScoreKeyObject&) key;
				encode_value(buf, zk.value);
				break;
			}
			case TABLE_INDEX:
			{
				const TableIndexKeyObject& index =
				        (const TableIndexKeyObject&) key;
				BufferHelper::WriteVarSlice(buf, index.kname);
				encode_value(buf, index.keyvalue);
				BufferHelper::WriteVarUInt32(buf, index.index.keyvals.size());
				ValueArray::const_iterator it = index.index.keyvals.begin();
				while (it != index.index.keyvals.end())
				{
					encode_value(buf, *it);
					it++;
				}
				break;
			}
			case TABLE_COL:
			{
				const TableColKeyObject& col = (const TableColKeyObject&) key;
				BufferHelper::WriteVarUInt32(buf, col.keyvals.size());
				ValueArray::const_iterator it = col.keyvals.begin();
				while (it != col.keyvals.end())
				{
					encode_value(buf, *it);
					it++;
				}
				BufferHelper::WriteVarSlice(buf, col.col);
				break;
			}
			case LIST_META:
			case ZSET_META:
			case SET_META:
			case TABLE_META:
			case TABLE_SCHEMA:
			default:
			{
				break;
			}
		}
	}

	bool peek_dbkey_header(const Slice& key, DBID& db, KeyType& type)
	{
		Buffer buf(const_cast<char*>(key.data()), 0, key.size());
		uint32 header;
		if (!BufferHelper::ReadFixUInt32(buf, header))
		{
			return false;
		}
		type = (KeyType)(header & 0xFF);
		db = header >> 8;
		return true;
	}

	KeyObject* decode_key(const Slice& key, KeyObject* expected)
	{
		Buffer buf(const_cast<char*>(key.data()), 0, key.size());
		uint32 header;
		if (!BufferHelper::ReadFixUInt32(buf, header))
		{
			return NULL;
		}
		uint8 type = header & 0xFF;
		uint32 db = header >> 8;
		if (NULL != expected)
		{
			if (type != expected->type || db != expected->db)
			{
				return NULL;
			}
		}
		Slice keystr;
		if (!BufferHelper::ReadVarSlice(buf, keystr))
		{
			return NULL;
		}
		if (NULL != expected)
		{
			if (keystr != expected->key)
			{
				return NULL;
			}
		}
		switch (type)
		{
			case HASH_FIELD:
			{
				Slice field;
				if (!BufferHelper::ReadVarSlice(buf, field))
				{
					return NULL;
				}
				return new HashKeyObject(keystr, field, db);
			}
			case LIST_ELEMENT:
			{
				float score;
				if (!BufferHelper::ReadFixFloat(buf, score))
				{
					return NULL;
				}
				return new ListKeyObject(keystr, score, db);
			}

			case SET_ELEMENT:
			{
				SetKeyObject* sk = new SetKeyObject(keystr, Slice(), db);
				if (!decode_value(buf, sk->value, false))
				{
					DELETE(sk);
					return sk;
				}
				return sk;
			}
			case ZSET_ELEMENT:
			{
				ZSetKeyObject* zsk = new ZSetKeyObject(keystr, Slice(), 0, db);
				double score;
				if (!BufferHelper::ReadFixDouble(buf, score)
				        || !decode_value(buf, zsk->value))
				{
					DELETE(zsk);
					return NULL;
				}
				zsk->score = score;
				return zsk;
			}
			case ZSET_ELEMENT_SCORE:
			{
				ZSetScoreKeyObject* zsk = new ZSetScoreKeyObject(keystr,
				        Slice(), db);
				if (!decode_value(buf, zsk->value))
				{
					DELETE(zsk);
					return NULL;
				}
				return zsk;
			}
			case TABLE_INDEX:
			{
				Slice kname;
				if (!BufferHelper::ReadVarSlice(buf, kname))
				{
					return NULL;
				}
				TableIndexKeyObject* ik = new TableIndexKeyObject(keystr, kname,
				        ValueObject(), db);
				if (!decode_value(buf, ik->keyvalue))
				{
					DELETE(ik);
					return NULL;
				}
				uint32 len;
				if (!BufferHelper::ReadVarUInt32(buf, len))
				{
					DELETE(ik);
					return NULL;
				}
				for (uint32 i = 0; i < len; i++)
				{
					ValueObject v;
					if (!decode_value(buf, v))
					{
						DELETE(ik);
						return NULL;
					}
					ik->index.keyvals.push_back(v);
				}
				return ik;
			}
			case TABLE_COL:
			{
				TableColKeyObject* tk = new TableColKeyObject(keystr, Slice(),
				        db);
				uint32 len;
				if (!BufferHelper::ReadVarUInt32(buf, len))
				{
					DELETE(tk);
					return NULL;
				}
				for (uint32 i = 0; i < len; i++)
				{
					ValueObject v;
					if (!decode_value(buf, v))
					{
						DELETE(tk);
						return NULL;
					}
					tk->keyvals.push_back(v);
				}
				Slice col;
				if (!BufferHelper::ReadVarSlice(buf, col))
				{
					DELETE(tk);
					return NULL;
				}
				tk->col = col;
				return tk;
			}
			case SET_META:
			case ZSET_META:
			case LIST_META:
			case TABLE_META:
			case TABLE_SCHEMA:
			default:
			{
				return new KeyObject(keystr, (KeyType) type, db);
			}
		}
	}

	int value_convert_to_raw(ValueObject& v)
	{
		if (v.type == EMPTY)
		{
			return -1;
		}
		if (v.type != RAW)
		{
			int64_t iv = v.v.int_v;
			double dv = v.v.double_v;
			v.type = RAW;
			v.v.raw = new Buffer(16);
			if (v.type == INTEGER)
			{
				v.v.raw->Printf("%lld", iv);
			}
			else if (v.type == DOUBLE)
			{
				double min = -4503599627370495LL; /* (2^52)-1 */
				double max = 4503599627370496LL; /* -(2^52) */
				iv = (int64_t) dv;
				if (dv > min && dv < max && dv == ((double) iv))
				{
					v.v.raw->Printf("%lld", iv);
				}
				else
				{
					v.v.raw->Printf("%.17g", dv);
				}
			}
			return 0;
		}
		return -1;
	}

	int value_convert_to_number(ValueObject& v)
	{
		if (v.type == EMPTY)
		{
			return -1;
		}
		if (v.type != RAW)
		{
			return 0;
		}
		int64_t intv;
		double dv;
		if (raw_toint64(v.v.raw->GetRawReadBuffer(), v.v.raw->ReadableBytes(),
		        intv))
		{
			v.Clear();
			v.type = INTEGER;
			v.v.int_v = intv;
			return 1;
		}
		else if (raw_todouble(v.v.raw->GetRawReadBuffer(),
		        v.v.raw->ReadableBytes(), dv))
		{
			v.Clear();
			v.type = DOUBLE;
			v.v.double_v = dv;
			return 1;
		}
		return -1;
	}

	void encode_value(Buffer& buf, const ValueObject& value)
	{
		BufferHelper::WriteFixUInt8(buf, value.type);
		switch (value.type)
		{
			case EMPTY:
			{
				break;
			}
			case INTEGER:
			{
				BufferHelper::WriteVarInt64(buf, value.v.int_v);
				break;
			}
			case DOUBLE:
			{
				BufferHelper::WriteFixDouble(buf, value.v.double_v);
				break;
			}
			default:
			{
				if (NULL != value.v.raw)
				{
					BufferHelper::WriteVarUInt32(buf,
					        value.v.raw->ReadableBytes());
					buf.Write(value.v.raw->GetRawReadBuffer(),
					        value.v.raw->ReadableBytes());
				}
				else
				{
					BufferHelper::WriteVarInt64(buf, 0);
				}
				break;
			}
		}
	}

	bool decode_value(Buffer& buf, ValueObject& value, bool copyRawValue)
	{
		value.Clear();
		if (!BufferHelper::ReadFixUInt8(buf, value.type))
		{
			return false;
		}
		switch (value.type)
		{
			case EMPTY:
			{
				break;
			}
			case INTEGER:
			{
				if (!BufferHelper::ReadVarInt64(buf, value.v.int_v))
				{
					return false;
				}
				break;
			}
			case DOUBLE:
			{
				if (!BufferHelper::ReadFixDouble(buf, value.v.double_v))
				{
					return false;
				}
				break;
			}
			default:
			{
				uint32_t len;
				if (!BufferHelper::ReadVarUInt32(buf, len)
				        || buf.ReadableBytes() < len)
				{
					return false;
				}

				if (copyRawValue)
				{
					value.v.raw = new Buffer(len);
					buf.Read(value.v.raw, len);
				}
				else
				{
					const char* tmp = buf.GetRawReadBuffer();
					value.v.raw = new Buffer(const_cast<char*>(tmp), 0, len);
					buf.SkipBytes(len);
				}
				break;
			}
		}
		return true;
	}
	void smart_fill_value(const Slice& value, ValueObject& valueobject)
	{
		if (value.empty())
		{
			valueobject.type = EMPTY;
			return;
		}
		int64_t intv;
		double doublev;
		char first_char = value.data()[0];
		if (first_char != '+' && value.data()[0] != '-'
		        && (first_char < '0' || first_char > '9'))
		{
			valueobject.type = RAW;
			char* v = const_cast<char*>(value.data());
			valueobject.v.raw = new Buffer(v, 0, value.size());
		}
		else
		{
			if (raw_toint64(value.data(), value.size(), intv))
			{
				valueobject.type = INTEGER;
				valueobject.v.int_v = intv;
			}
			else if (raw_todouble(value.data(), value.size(), doublev))
			{
				valueobject.type = DOUBLE;
				valueobject.v.double_v = doublev;
			}
			else
			{
				valueobject.type = RAW;
				char* v = const_cast<char*>(value.data());
				valueobject.v.raw = new Buffer(v, 0, value.size());
			}
		}

	}
	void fill_raw_value(const Slice& value, ValueObject& valueobject)
	{
		valueobject.type = RAW;
		char* v = const_cast<char*>(value.data());
		valueobject.v.raw = new Buffer(v, 0, value.size());
	}
}

