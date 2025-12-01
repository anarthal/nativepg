
PostgreSQL has a lot of type that need to be mapped to and from C/C++ types.

Beneath table gives a overview of this mapping (Proposal)

### **PostgreSQL Built-In Types**

| Name | OID | Category | C++ Type | Byte Length | Binary? |
|------|-----|----------|-----------|-------------|---------|
| bool | 16 | boolean | bool | 1 | yes |
| bytea | 17 | binary data | std::vector<uint8_t> | variable | yes |
| char | 18 | internal char | char | 1 | yes |
| name | 19 | identifiers | std::string | variable | yes |
| int8 | 20 | integer | int64_t | 8 | yes |
| int2 | 21 | integer | int16_t | 2 | yes |
| int4 | 23 | integer | int32_t | 4 | yes |
| regproc | 24 | system OID reference | uint32_t | 4 | yes |
| text | 25 | string | std::string | variable | yes |
| oid | 26 | OID | uint32_t | 4 | yes |
| tid | 27 | tuple ID | struct {int32 block;int16 offset;} | 6 | yes |
| xid | 28 | transaction ID | uint32_t | 4 | yes |
| cid | 29 | command ID | uint32_t | 4 | yes |
| oidvector | 30 | OID array | vector<uint32_t> | variable | yes |
| json | 114 | JSON text | std::string | variable | no |
| xml | 142 | XML text | std::string | variable | no |
| xmlarray | 143 | array | unused | n/a | no |
| pg_node_tree | 194 | internal | std::string | variable | no |
| smgr | 210 | storage manager | internal | 2 | no |
| point | 600 | geometric | struct {double x,y;} | 16 | yes |
| lseg | 601 | geometric | struct {point p1,p2;} | 32 | yes |
| path | 602 | geometric | vector<point> | variable | yes |
| box | 603 | geometric | struct {point high,low;} | 32 | yes |
| polygon | 604 | geometric | polygon struct | variable | yes |
| line | 628 | geometric | struct {double A,B,C;} | 24 | yes |
| cidr | 650 | network | struct inet/cidr | variable | yes |
| float4 | 700 | floating | float | 4 | yes |
| float8 | 701 | floating | double | 8 | yes |
| abstime | 702 | obsolete time | int32_t | 4 | yes |
| reltime | 703 | obsolete time | int32_t | 4 | yes |
| tinterval | 704 | obsolete | struct | 12 | yes |
| unknown | 705 | unknown | std::string | variable | no |
| circle | 718 | geometric | struct {point center; double radius;} | 24 | yes |
| money | 790 | currency | int64_t | 8 | yes |
| macaddr | 829 | mac address | array<uint8_t,6> | 6 | yes |
| inet | 869 | IP network | custom struct | variable | yes |
| macaddr8 | 774 | MAC (8 bytes) | array<uint8_t,8> | 8 | yes |
| aclitem | 1033 | ACL item | std::string | variable | no |
| bpchar | 1042 | CHAR(n) | std::string | variable | yes |
| varchar | 1043 | VARCHAR | std::string | variable | yes |
| date | 1082 | date | int32_t (days) | 4 | yes |
| time | 1083 | time | int64_t (μs) | 8 | yes |
| timestamp | 1114 | timestamp | int64_t (μs) | 8 | yes |
| timestamptz | 1184 | timestamp tz | int64_t (μs) | 8 | yes |
| interval | 1186 | interval | struct of 3 ints | 16 | yes |
| timetz | 1266 | time tz | struct | 12 | yes |
| bit | 1560 | fixed bitstring | bitset<> | variable | yes |
| varbit | 1562 | varbit | vector<bool> | variable | yes |
| numeric | 1700 | numeric/decimal | std::string | variable | no |
| refcursor | 1790 | cursor name | std::string | variable | no |
| regprocedure | 2202 | OID ref | uint32_t | 4 | yes |
| regoper | 2203 | OID ref | uint32_t | 4 | yes |
| regoperator | 2204 | OID ref | uint32_t | 4 | yes |
| regclass | 2205 | OID ref | uint32_t | 4 | yes |
| regtype | 2206 | OID ref | uint32_t | 4 | yes |
| regrole | 4096 | OID ref | uint32_t | 4 | yes |
| regnamespace | 4089 | OID ref | uint32_t | 4 | yes |
| uuid | 2950 | UUID | array<uint8_t,16> | 16 | yes |
| jsonb | 3802 | JSONB | std::string | variable | no |
| txid_snapshot | 2970 | txid snapshot | struct | variable | no |
| pg_lsn | 3220 | WAL pointer | uint64_t | 8 | yes |
| tsquery | 3615 | TS query | std::string | variable | no |
| tsvector | 3614 | TS vector | std::string | variable | no |
| range types (int4range…) | 3904+ | range | custom | variable | no |
| smallserial | 21 | alias | int16_t | n/a | yes |
| serial | 23 | alias | int32_t | n/a | yes |
| bigserial | 20 | alias | int64_t | n/a | yes |

Binary Support
•	YES → well-defined binary wire format, easy to implement (int, float, uuid, inet, geometric)
•	NO → complex binary structure or rarely used (jsonb, numeric, tsvector)


### **PostgreSQL Built-In Array Types Table** 

| Array Type | Array OID | Element OID | Element Type | C++ Type                            | Binary? |
|------------|-----------|--------------|---------------|-------------------------------------|---------|
| _bool | 1000 | 16 | bool | std::vector\<bool>                  | yes |
| _bytea | 1001 | 17 | bytea | std::vector<std::vector<uint8_t>>   | yes |
| _char | 1002 | 18 | char | std::vector\<char>                  | yes |
| _name | 1003 | 19 | name | std::vector\<std::string>           | yes |
| _int2 | 1005 | 21 | int2 | std::vector<int16_t>                | yes |
| _int2vector | 1006 | 22 | int2vector | std::vector<int16_t>                | yes |
| _int4 | 1007 | 23 | int4 | std::vector<int32_t>                | yes |
| _regproc | 1008 | 24 | regproc | std::vector<uint32_t>               | yes |
| _text | 1009 | 25 | text | std::vector\<std::string>           | yes |
| _oid | 1028 | 26 | oid | std::vector\<uint32_t>              | yes |
| _tid | 1010 | 27 | tid | std::vector\<tid>                   | yes |
| _xid | 1011 | 28 | xid | std::vector<uint32_t>               | yes |
| _cid | 1012 | 29 | cid | std::vector<uint32_t>               | yes |
| _oidvector | 1013 | 30 | oidvector | std::vector<uint32_t>               | yes |
| _json | 199 | 114 | json | std::vector\<std::string>           | no |
| _xml | 143 | 142 | xml | std::vector\<std::string>           | no |
| _point | 1017 | 600 | point | std::vector\<point>                 | yes |
| _lseg | 1018 | 601 | lseg | std::vector\<lseg>                  | yes |
| _path | 1019 | 602 | path | std::vector\<path>                  | yes |
| _box | 1020 | 603 | box | std::vector\<box>                   | yes |
| _polygon | 1027 | 604 | polygon | std::vector\<polygon>               | yes |
| _line | 629 | 628 | line | std::vector\<line>                  | yes |
| _cidr | 651 | 650 | cidr | std::vector\<cidr>                  | yes |
| _float4 | 1021 | 700 | float4 | std::vector\<float>                 | yes |
| _float8 | 1022 | 701 | float8 | std::vector\<double>                | yes |
| _abstime | 1023 | 702 | abstime | std::vector<int32_t>                | yes |
| _reltime | 1024 | 703 | reltime | std::vector<int32_t>                | yes |
| _tinterval | 1025 | 704 | tinterval | std::vector\<tinterval>             | yes |
| _circle | 719 | 718 | circle | std::vector\<circle>                | yes |
| _money | 791 | 790 | money | std::vector\<int64_t>               | yes |
| _macaddr | 1040 | 829 | macaddr | std::vector\<macaddr>               | yes |
| _inet | 1041 | 869 | inet | std::vector\<inet>                  | yes |
| _macaddr8 | 775 | 774 | macaddr8 | std::vector\<macaddr8>              | yes |
| _bpchar | 1014 | 1042 | bpchar | std::vector\<std::string>           | yes |
| _varchar | 1015 | 1043 | varchar | std::vector\<std::string>           | yes |
| _date | 1182 | 1082 | date | std::vector\<int32_t>               | yes |
| _time | 1183 | 1083 | time | std::vector\<int64_t>               | yes |
| _timestamp | 1115 | 1114 | timestamp | std::vector\<int64_t>               | yes |
| _timestamptz | 1185 | 1184 | timestamptz | std::vector\<int64_t>               | yes |
| _interval | 1187 | 1186 | interval | std::vector\<interval>              | yes |
| _timetz | 1270 | 1266 | timetz | std::vector\<timetz>                | yes |
| _bit | 1561 | 1560 | bit | std::vector\<bitset>                | yes |
| _varbit | 1563 | 1562 | varbit | std::vector<std::vector\<bool>>      | yes || _numeric | 1231 | 1700 | numeric | std::vector<std::string>            | no |
| _refcursor | 2201 | 1790 | refcursor | std::vector\<std::string>           | no |
| _regprocedure | 2207 | 2202 | regprocedure | std::vector<uint32_t>               | yes |
| _regoper | 2208 | 2203 | regoper | std::vector<uint32_t>               | yes |
| _regoperator | 2209 | 2204 | regoperator | std::vector<uint32_t>               | yes |
| _regclass | 2210 | 2205 | regclass | std::vector<uint32_t>               | yes |
| _regtype | 2211 | 2206 | regtype | std::vector<uint32_t>               | yes |
| _regrole | 4097 | 4096 | regrole | std::vector<uint32_t>               | yes |
| _regnamespace | 4090 | 4089 | regnamespace | std::vector<uint32_t>               | yes |
| _uuid | 2951 | 2950 | uuid | std::vector<std::array<uint8_t,16>> | yes |
| _jsonb | 3807 | 3802 | jsonb | std::vector\<std::string>           | no |
| _txid_snapshot | 2949 | 2970 | txid_snapshot | std::vector<txid_snapshot>          | no |
| _pg_lsn | 3221 | 3220 | pg_lsn | std::vector<uint64_t>               | yes |
| _tsquery | 3645 | 3615 | tsquery | std::vector\<std::string>           | no |
| _tsvector | 3643 | 3614 | tsvector | std::vector\<std::string>           | no |
| _int4range | 3905 | 3904 | int4range | std::vector<range<int32_t>>         | no |
| _numrange | 3907 | 3906 | numrange | std::vector<range\<string>>          | no 
| _tsrange | 3909 | 3908 | tsrange | std::vector<range<int64_t>>         | no |
| _tstzrange | 3911 | 3910 | tstzrange | std::vector<range<int64_t>>         | no |
| _daterange | 3913 | 3912 | daterange | std::vector<range<int32_t>>         | no |
| _int8range | 3927 | 3926 | int8range | std::vector<range<int64_t>>         | no |

Binary Support
•	yes: array elements have a well-defined binary wire protocol (ints, floats, uuid, geometric, etc.)
•	no: complex formats (numeric, jsonb, ranges, tsvector, etc.)
