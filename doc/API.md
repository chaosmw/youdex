#YOUDEX-API-DOC

[TOC]

##修改历史

| 日期      |     作者 |   版本 | 描述   |
| :-------- | --------:| :------: |------ |
| 2020年06月06日    |   ChaosMW |  v1.0.0  |open source|



##基础知识
- 账户名
EOS 中的账户名最多12个字母， 由“ . 1 2 3 4 5 a-z” 组成。

- 精度
所有涉及到代币金额的地方都要注意精度对齐，比如"0.1 EOS" 要写成 "0.1000 EOS",  中间有个空格。

- 智能合约

| contract_name      |  desc    |  remark   |
| :-------- | --------| ------ |
| yyy111111111    |   撮合引擎 |   仅测试网络 |
| ppp111111111    |   智能风控|   仅测试网络|
| vns111111111    |  VENUS |  RTC 代币发行合约 仅测试网络 |

- 交易对
EOS 上的DEX(去中心化交易所) 一般会有很多交易对， 比如ABC/EOS, XYZ/EOS， 其中ABC和XYZ 是EOS 上的项目方发行的token（代币）。 这些都是锚定EOS的， 用EOS 做结算，也能锚定其他token做结算； YOUDEX 也会发行自己的token( RTC) 

- 交易
买卖双方挂单后， YOUDEX 通过撮合合约完成代币分割和结算。目前支持4种挂单类型：

| ID      |     类型 |   说明   |
| :-------- | --------:| :------: |
|  1   |   市价卖单 |  按当前市价成交  |
|  2   |   限价卖单 |  符合价格即可成交  |
|  3   |   市价买单 |  按当前市价成交  |
|  4   |   限价买单 |  符合价格即可成交  |

- 用户名转换为uint64的算法
```
const Eos = require("eosjs") // Home.vue 中已import
const BigNumber = require("bignumber.js") // 避免溢出

const accountName = 'myaccount'
const encodedName = new 
BigNumber(Eos.modules.format.encodeName(accountName, false))

```
- 查询table_raws
可参考 https://eosio.stackexchange.com/questions/813/eosjs-gettablerows-lower-and-upper-bound-on-account-name
```
Eos.Localnet({}).getTableRows({
  code: contractName.toString(),
  json: json,
  limit: limit,
  lower_bound: encodedName.toString(),
  scope: contractName.toString(),
  table: tableName.toString(),
  upper_bound: encodedName.plus(1).toString()
})
```

## 网络类型

| 名称      |     说明 |   URI   | 区块浏览器	|
| :-------- | --------| ------ | ------ |
| mainnet    |   EOS主网 |  http://mainnet.meet.one/ |	https://eospark.com/	|
| kylin    |   EOS测试网络 |  http://api-kylin.eoshenzhen.io:8890  |	https://eospark.com/Kylin|

##交易对状态

| 状态      |     说明 |   备注   |
| :-------- | --------| ------ |
| 0    |   PENDING |  待激活  |
| 1    |   ACTIVE |  可下单  |
| 2    |   DEACTIVATED |  临时维护、暂时不可下单 |
| 3    |   DISABLED |  已下线  |


## 交易撮合API 接口
###获取用户代币余额

| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_currency_balance    |   POST | 获取用户余额  |
请求参数中的account即为用户的账户名， code 为发行代币的合约名。
| name      |    type |  desc |   remark |
| :-------- | --------| ------ | ------ |
| account    |  string | 用户账号 |   |
| code | string | 发行代币的合约名 | 如代币为EOS， 则为eosio.token， 其他可以参考quote.contract |
| symbol | string | 代币的名字 | 如代币为EOS， 则为EOS； 其他可以参考quote.symbol |

下面的例子查询账号fff111111111在合约eosio.token中的EOS 余额
```
REQUEST:
---------------------
POST /v1/chain/get_currency_balance HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 75
Accept: */*
Connection: close

{
  "account": "fff111111111",
  "code": "eosio.token",
  "symbol": "EOS"
}
---------------------
RESPONSE:
---------------------
[
  "245.5933 EOS"
]
---------------------
```

### 查询交易对列表
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取交易对列表  |
查询交易表exchpairs，可通过limit设置返回的记录数， response中通过more字段表示是否已达到末尾。 这个例子返回了2个交易对， 第一个为ABC/EOS， 第二个为RTC/EOS。
request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | 撮合引擎合约名  |  测试网络用yyy111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 交易对表名| 必须为exchpairs|
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|


response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| id | uint64 | 交易对的唯一ID | 挂单交易等需要用到这个字段 |
| quote | object | 交易对的标的| 比如第一个为ABC |
| base | object | 锚定币 | |
|quote_precision | int | 标的物的精度 | 比如这里的ABC, 必须是"1.001 ABC" 的形式|
|base_precision | int | 锚定币的精度 | 比如这里的EOS, 必须是"1.0000 EOS" 的形式|
|scale |int |价格精度| 锚定币的价格精度, 比如挂单ABC，E0S的精度为10000， 意味最小可表示的数额为"0.0001 EOS", 但是scale为10000， 则最小可表示的数额为"0.00000001 EOS"| 
| stake_threshold |uint64 | 需要抵押的RTC 阈值 |
| txid | string |交易对的简介所对应的区块交易ID， 参考下面如何获取区块交易信息|

```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 237
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "exchpairs",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 10,
  "key_type": "",
  "index_position": "",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 1,
      "manager": "vns111111111",
      "quote": {
        "sym": "3,ABC",
        "contract": "hhh111111111"
      },
      "base": {
        "sym": "4,EOS",
        "contract": "eosio.token"
      },
      "quote_precision": 1000,
      "base_precision": 10000,
      "scale": 10000,
      "created_time": 1542727744,
      "lastmod_time": 1542728778,
      "status": 1,
      "vote_threshold": 1000,
      "stake_threshold": "10000000000",
      "txid": "cbd14f89cc43b4c6f58868381136f5172cb3138ea34c0cfd0c3d6f8bd54bb18a",
      "r1": 0,
      "r2": 0,
      "r3": 0,
      "r4": 0
    },{
      "id": 2,
      "manager": "vns111111111",
      "quote": {
        "sym": "4,RTC",
        "contract": "vns111111111"
      },
      "base": {
        "sym": "4,EOS",
        "contract": "eosio.token"
      },
      "quote_precision": 10000,
      "base_precision": 10000,
      "scale": 100,
      "created_time": 1542727887,
      "lastmod_time": 1542729007,
      "status": 0,
      "vote_threshold": 1000,
      "stake_threshold": "10000000000",
      "txid": "0bac9bb4728fd29785c7ad4d3e0c82203284d8ae4c38afac53db9ef344c5f1bf",
      "r1": 0,
      "r2": 0,
      "r3": 0,
      "r4": 0
    }
  ],
  "more": false
}
```

###查询交易对24H成交信息
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取交易对列表  |
查询交易表exchpairs，可通过limit设置返回的记录数， response中通过more字段表示是否已达到末尾。 这个例子返回了2个交易对， 第一个为ABC/EOS， 第二个为RTC/EOS。
request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | 撮合引擎合约名  |  测试网络用yyy111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 交易对表名| 必须为exchruntimes|
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|


response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| pair_id | uint64 | 交易对的唯一ID |  |
| ask_orders|uint64| 累计的卖单总数 |
| bid_orders |uint64|  累计的买单总数 | |
| vote_count | uint64 | 累计的投票总数 |
| latest_price | asset | 最新成交价 | |
| open_price | asset | 开盘价 | 比较当前价格和开盘价得到涨幅， 如开盘价格为0，则涨幅为0 |
| close_price | asset | 收盘价 | |
| highest_price | asset | 最高成交价 | |
| lowest_price | asset | 最低成交价 | |
| total_quote | asset | 成交总数（比如ABC） | |
| total_base | asset | 成交金额(比如EOS) |24H成交量 |


```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 240
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "exchruntimes",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 10,
  "key_type": "",
  "index_position": "",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "pair_id": 1,
      "ask_orders": 1,
      "bid_orders": 2,
      "vote_count": 0,
      "latest_price": "0.01000000 EOS",
      "open_price": "0.01000000 EOS",
      "close_price": "0.00000000 EOS",
      "highest_price": "0.01000000 EOS",
      "lowest_price": "0.01000000 EOS",
      "total_quote": "0.100 ABC",
      "total_base": "0.0011 EOS",
      "lastmod_time": 1542728431,
      "r1": 0,
      "r2": 0,
      "r3": 0,
      "r4": 0
    },{
      "pair_id": 2,
      "ask_orders": 0,
      "bid_orders": 0,
      "vote_count": 0,
      "latest_price": "0.000000 EOS",
      "open_price": "0.000000 EOS",
      "close_price": "0.000000 EOS",
      "highest_price": "0.000000 EOS",
      "lowest_price": "0.000000 EOS",
      "total_quote": "0.0000 RTC",
      "total_base": "0.0000 EOS",
      "lastmod_time": 0,
      "r1": 0,
      "r2": 0,
      "r3": 0,
      "r4": 0
    }
  ],
  "more": false
}
```

###获取系统最新的3条通知
取得txid后， 再通过get_transaction 取得具体内容。见下文
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 239
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "notifies",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 3,
  "key_type": "i64",
  "index_position": "2",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 2,
      "txid": "864bc3ffe7e891b97f37f5418284fcf49eb3b9ec7a38f8b57282ab9e89af625a",
      "created_time": 1542729168
    },{
      "id": 1,
      "txid": "8b2ddf5bc7f95d8e697be85b1f91c4ed6ca369a4118e7c203979cb66b306bf4b",
      "created_time": 1542729155
    },{
      "id": 0,
      "txid": "ca9643426fa9ed3de0433ec8cff1a640121d99d776f589b8f9b06daa5e02db8c",
      "created_time": 1542729131
    }
  ],
  "more": true
}
```

### 查询交易对卖单
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取交易对卖单， 默认按价格升序  |

request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | 撮合引擎合约名  |  测试网络用yyy111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 交易挂单表名| 必须为orders|
|lower_bound | string | 下限 | 见下面的说明 | 
|upper_bound | string | 上限 | 见下面的说明 |
| limit| int | 最多获取的记录条数 | 默认**100**|
| key_type | string | 必须为"i128" |
|index_position | int | 必须为4 |
|encode_type | string | 必须为dec|
| json | bool| 是否输出json格式| 设置为true|

lower_bound 和upper_bound为128位整形， 由3个字段拼接而成, 最低位在最右边。

| pair_id      |     order_type |   price   |
| :-------- | --------| ------ |
|[87:72]    |   [71:64] |  [63:0]  |

举例查询交易对（1） 的所有限价卖单(2)
| pair_id      |     order_type |   price   | lower_bound |
| :-------- | --------| ------ |  ------- | 
|1    |   固定为2 |  固定为1  | 0x01000000000000000201000000000000 |

| pair_id      |     order_type |   price   | upper_bound |
| :-------- | --------| ------ |  ------- | 
|1    |   固定为3 |  固定为0  | 0x00000000000000000301000000000000 |

拼接后的128位整形输出为**little endian** 格式的16进制字符串即可。 则获取的卖单范围是：
[ 0x01000000000000000201000000000000, 0x00000000000000000301000000000000 )



response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |


```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 308
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "orders",
  "table_key": "",
  "lower_bound": "0x01000000000000000201000000000000",
  "upper_bound": "0x00000000000000000301000000000000",
  "limit": 100,
  "key_type": "i128",
  "index_position": "4",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 443,
      "gid": 60,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 2,
      "placed_time": 1543586559,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1543586859,
      "initial": "1183990.499 ABC",
      "remain": "1183990.499 ABC",
      "price": "0.00000020 EOS",
      "deal": "0.0000 EOS"
    },{
      "id": 444,
      "gid": 61,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 2,
      "placed_time": 1543586573,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1543586873,
      "initial": "1183990.499 ABC",
      "remain": "1183990.499 ABC",
      "price": "0.00000021 EOS",
      "deal": "0.0000 EOS"
    }
  ],
  "more": false
}
```
代码如下：
```



/**
 * Combine pair_id, order_type and price to 128 bits integer and 
 * output it with little-endian hex string for searching
 * 
 * @param {*} pair_id [87:72]
 * @param {*} order_type [71:64]
 * @param {*} price [63:0]
 */
function get_search_key(pair_id, order_type, price) {
    var buffer = new ArrayBuffer(16); // 128 bits
    var view = new DataView(buffer);

    view.setUint8(0, price, true);
    view.setUint8(8, order_type, true);
    view.setUint16(9, pair_id, true);

    var output = '0x';
    for (var i = 0; i < 16; i++) {
        output += ("0"+(view.getUint8(i).toString(16))).slice(-2).toUpperCase();
    }

    return output;
}

// console.log(get_search_key(1, 2, 1)); // will output 0x01000000000000000201000000000000
// console.log(get_search_key(1, 3, 0)); // will output 0x00000000000000000301000000000000

```

### 查询交易对买单*
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取交易对买单， 默认按价格降序（注意， 下面暂时为升序， EOS下一版本会支持，已提交PR给官方）  |

request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | 撮合引擎合约名  |  测试网络用yyy111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 交易挂单表名| 必须为orders|
|lower_bound | string | 下限 | 见下面的说明 | 
|upper_bound | string | 上限 | 见下面的说明 |
| limit| int | 最多获取的记录条数 | 默认**100**|
| key_type | string | 必须为"i128" |
|index_position | int | 必须为4 |
|encode_type | string | 必须为dec|
| json | bool| 是否输出json格式| 设置为true|

lower_bound 和upper_bound为128位整形， 由3个字段拼接而成, 最低位在最右边。

| pair_id      |     order_type |   price   |
| :-------- | --------| ------ |
|[87:72]    |   [71:64] |  [63:0]  |

举例查询交易对（1） 的所有限价买单(2)
| pair_id      |     order_type |   price   | lower_bound |
| :-------- | --------| ------ |  ------- | 
|1    |   固定为4 |  固定为0  | 0x00000000000000000401000000000000 |

| pair_id      |     order_type |   price   | upper_bound |
| :-------- | --------| ------ |  ------- | 
|1    |   固定为5 |  固定为0  | 0x00000000000000000501000000000000 |

拼接后的128位整形输出为**little endian** 格式的16进制字符串即可。 则获取的卖单范围是：
[ 0x00000000000000000401000000000000, 0x00000000000000000501000000000000 )



response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |


```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 308
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "orders",
  "table_key": "",
  "lower_bound": "0x00000000000000000401000000000000",
  "upper_bound": "0x00000000000000000501000000000000",
  "limit": 100,
  "key_type": "i128",
  "index_position": "4",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 442,
      "gid": 59,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 4,
      "placed_time": 1543586247,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1543586547,
      "initial": "98.8372 EOS",
      "remain": "98.8372 EOS",
      "price": "0.00000013 EOS",
      "deal": "0.000 ABC"
    },{
      "id": 441,
      "gid": 58,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 4,
      "placed_time": 1543586236,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1543586536,
      "initial": "91.2343 EOS",
      "remain": "91.2343 EOS",
      "price": "0.00000012 EOS",
      "deal": "0.000 ABC"
    },{
      "id": 440,
      "gid": 57,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 4,
      "placed_time": 1543586228,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1543586528,
      "initial": "83.6315 EOS",
      "remain": "83.6315 EOS",
      "price": "0.00000011 EOS",
      "deal": "0.000 ABC"
    },{
      "id": 439,
      "gid": 56,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 4,
      "placed_time": 1543586214,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1543586514,
      "initial": "76.0286 EOS",
      "remain": "76.0286 EOS",
      "price": "0.00000010 EOS",
      "deal": "0.000 ABC"
    }
  ],
  "more": false
}
```

### 创建账户
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | 撮合合约名 | 正式网络和测试网络不同|
| action   |   openaccount| 创建账户  |


action字段说明：

| name      |   type |  desc |   remark |
| :-------- | --------| ------ | ------- |
| owner | string | 账户名 | |
| increment | int | 预留挂单数 | （0， 10] |
| ram_payer | string | 同账户名 |也可替人创建账户 |



response 字段说明：（所有交易返回结构都相同, 如发送失败需要提示用户）
| name      |   type |   desc   |   remark |
| :-------- | -------- | ------ | ------- |
| transaction_id | string | 交易Id | 在区块链链上的交易ID |


###注销账户
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | 撮合合约名 | 正式网络和测试网络不同|
| action   |   closeaccount| 注销账目的挂单，撮合未完成的资金原路返回  |


request 字段说明
| name      |    type |  desc |   remark |
| :-------- | --------| ------ | ------ | 
| owner | string | 账户名 | |
| count | int | 删除挂单数 | （0， 10] |



###创建买单
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | base.contract |参考交易对中的实际值, 如果锚定EOS, 则为eosio.token |
| action   |   transfer| 挂买单  |

request 字段说明
| name      |  type |    desc |   remark |
| :-------- | --------| ------ | ------ |
|from | string | 用户账户名 |比如sam111111111 |
|to | string | 撮合合约名| |
|quantity| string | 金额| 注意精度，比如EOS， 必须为"1.0001 EOS"的形式， 参考base.precision 即可， 和价格精度不同， 最小交易金额为0.1000 EOS|
|memo| string | 业务信息| 具体见下文|
**memo字段说明**
- 首尾字符必须为#
- 各字段之间用分号：间隔
- 一共5个字段都是必填的
- 长度不能超过256字节

| 序号      |     说明 |   备注   |
| :-------- | --------| ------ |
| 1    |   pair.id 交易对的ID |  从交易对列表获取  |
| 2|order.type 挂单类型 | 参考交易类型说明， 3或者4|
| 3 |order.price 价格 | 精度需结合base_precision 和 scale，比如base_precision 为10000， scale为10000， 则价格精度为8位， "0.0001 EOS" 要写成 "0.00010000 EOS"， 如果是市价单， 价格则为"0.00000000 EOS", 同样注意精度|
| 4 | 撮合方式| 0或者1， 0代表仅挂单， 1代表挂单并撮合|
| 5 | channel | 渠道账户名， demo暂时设置为wallet|

举例, 用户挂单购买ABC token： #1:3:0.00019000 EOS:0:wallet# 代表在ID为1的交易对上， 挂限价买单(3), 价格为0.00019000 EOS, 仅仅挂单(0), 渠道为(wallet)


###创建卖单
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | quote.contract |参考交易对中的实际值, 如示例中的第一个交易对， 则为hhh111111111 |
| action   |   transfer| 挂卖单  |

request 字段说明
| name      |  type |   desc |   remark |
| :-------- | --------| ------ | ------ |
|from | string | 用户账户名 | |
|to | string | 撮合合约名| |
|quantity| string | 金额| 注意精度，比如ABC， 必须为"1.123 ABC"的形式， 参考quote.precision 即可， 和价格精度不同; 如果是市价单， 最小交易金额为0.1000 EOS|
|memo| string | 业务信息| 具体见下文|
**memo字段说明**
- 首尾字符必须为#
- 各字段之间用分号：间隔
- 一共5个字段都是必填的
- 长度不能超过256字节

| 序号      |     说明 |   备注   |
| :-------- | --------| ------ |
| 1    |   pair.id 交易对的ID |  从交易对列表获取  |
| 2|order.type 挂单类型 | 参考交易类型说明,1或者2|
| 3 |order.price 价格 | 精度需结合base_precision 和 scale，比如base_precision 为10000， scale为10000， 则价格精度为8位， "0.0001 EOS" 要写成 "0.00010000 EOS"， 如果是市价单， 价格则为"0.00000000 EOS", 同样注意精度|
| 4 | 撮合方式| 0或者1， 0代表仅挂单， 1代表挂单并撮合|
| 5 | channel | 渠道账户名， demo暂时设置为wallet|

举例： #1:2:0.00018000 EOS:0:wallet# 代表在ID为1的交易对上， 挂限价卖单(2), 价格为0.00018000 EOS, 仅仅挂单(0), 渠道为(wallet)

###投票
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | eosio.token| 必须为eosio.token |
| action   |   transfer| 给指定交易对投票  |

request 字段说明
| name      |   type | desc |   remark |
| :-------- | --------| ------ | -------- |
|from | string | 用户账户名 |比如sam111111111 |
|to | string | 撮合合约名| 正式网络和测试网络不同|
|quantity| string |金额| 必须为"0.0001 EOS", 否则会失败|
|memo| string |业务信息| vote:pair_id[:refer]具体见下文|

**memo字段说明**
- 各字段之间用分号：间隔
- 一共3个字段, vote 和 pair_id 为必填字段， refer 可选
- 长度不能超过256字节

| 序号      |     说明 |   备注   |
| :-------- | --------| ------ |
| 1 | 业务字段| 必须为"vote" |
| 2   |   pair.id 交易对的ID |  从交易对列表获取， 如已激活，则投票失败  |
| 3|推荐人账号 |不能是自己， 这是个可选参数|

举例： "vote:1" 表示给第一个交易对投票; "vote:3:sam111111111" 表示给第3个交易对投票， 推荐人是sam111111111

###查询用户所有的vote
request字段说明：

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | 撮合引擎合约名  |  测试网络用yyy111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string |交易表名| 必须为votes |
| lower_bound |uint64 | 转换用户账户名后的| 转换算法见上文|
| upper_bound | uint64| lower_bound+1 | | |
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|
| index_position | int | 必须为3| |
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 275
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "votes",
  "table_key": "",
  "lower_bound": "6545437268105511440",
  "upper_bound": "6545437268105511441",
  "limit": 10,
  "key_type": "i64",
  "index_position": "3",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 0,
      "pair_id": 2,
      "from": "fff111111111",
      "referee": "fff222222222",
      "timestamp": 1542990601
    }
  ],
  "more": false
}
```
response 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|id | uint64 | vote id| |
|pair_id | uint64 | 交易对ID | |
|from | string | 用户账户名 | |
|referee | string | 推荐人账户名 | |
|timestamp | unix timestamp | 时间戳 | |


###push挂单
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | 撮合合约名 |正式网络和测试网络不同|
| action   |   push| 撮合自己的挂单 |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|from | string | 用户账户名 | |
|id | uint64 | 挂单ID| |


###查询用户所有的挂单
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取用户的挂单列表  |

可参考 https://eosio.stackexchange.com/questions/813/eosjs-gettablerows-lower-and-upper-bound-on-account-name

request字段说明：

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | 撮合引擎合约名  |  测试网络用yyy111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string |交易表名| 必须为orders |
| lower_bound |uint64 | 转换用户账户名后的| 转换算法见上文|
| upper_bound | uint64| lower_bound+1 | | |
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|
| index_position | int | 必须为5| |

下面获取用户fff111111111 的挂单信息（其中gid 不会改变； id为0表示可用记录， 其他为当前挂单）
```
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 276
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "orders",
  "table_key": "",
  "lower_bound": "6545437268105511440", //fff111111111 --> uint64
  "upper_bound": "6545437268105511441",
  "limit": 10,
  "key_type": "i64",
  "index_position": "5",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 8,
      "gid": 4,
      "owner": "fff111111111",
      "pair_id": 1,
      "type": 3,
      "placed_time": 1542728429,
      "updated_time": 1542728431,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1542728729,
      "initial": "1.0000 EOS",
      "remain": "0.9989 EOS",
      "price": "0.02000000 EOS",
      "deal": "0.100 ABC"
    },{
      "id": 271,
      "gid": 5,
      "owner": "fff111111111",
      "pair_id": 3,
      "type": 3,
      "placed_time": 1542814113,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 1542814413,
      "initial": "1.0000 EOS",
      "remain": "1.0000 EOS",
      "price": "0.001000 EOS",
      "deal": "0.000 AAA"
    },{
      "id": 0,
      "gid": 10,
      "owner": "fff111111111",
      "pair_id": 0,
      "type": 0,
      "placed_time": 0,
      "updated_time": 0,
      "closed_time": 0,
      "close_reason": 0,
      "expiration_time": 0,
      "initial": "0.0000 EOS",
      "remain": "0.0000 EOS",
      "price": "0.0000 EOS",
      "deal": "0.0000 EOS"
    }
  ],
  "more": false
}

```

response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| id | uint64 | 交易的ID | 为0表示空闲可挂单 |
| gid| uint64|开户时分配的id | 不会改变|
| owner| string| 交易用户 | |
| pair_id| uint64 |交易对ID| |
| type| int|挂单类型 |[1,4] |
|placed_time |uint64 | unix timestamp| 下单时间 var date = new Date(unix_timestamp*1000);|
| updated_time|uint64 | unix timestamp| 部分撮合时的更新时间|
|closed_time |uint64 | unix timestamp| 下单时间|
|close_reason | uint8| 关闭订单的原因| |
| expiration_time|uint64 | unix timestamp| 下单过期时间|
| initial|金额 |初始挂单资金 | 以AAA/EOS 卖单为例，则单位为AAA； 买单时单位为EOS|
| remain| 金额| 剩余挂单资金| 单位同initial|
| price |价格 | 挂单价格| |
| deal|已交易的目标token金额 |已成交金额 |以AAA/EOS 卖单为例，则单位为EOS； 买单是单位为AAA |


###取消挂单
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | 撮合合约名 |正式网络和测试网络不同|
| action   |   cancelorder| 取消自己的挂单，未成交的部分原路返回 |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|from | string | 用户账户名 | |
|id | uint64 | 挂单ID| |

### 查询系统通知

```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 239
Accept: */*
Connection: close

{
  "json": true,
  "code": "yyy111111111",
  "scope": "yyy111111111",
  "table": "notifies",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 3, //最近的3条消息
  "key_type": "i64",
  "index_position": "2",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 8,
      "txid": "3f502b57d210089ee2628e31b515fb3e6faf4ce4ae6c42f043ce2bb820fb6279",
      "created_time": 1542542470
    },{
      "id": 7,
      "txid": "6043f95f5ac16e15e23074747145fd1fce27a9b6013dc4fd0947817faff63b98",
      "created_time": 1542542454
    },{
      "id": 6,
      "txid": "a5006e913a09d7b3ed4a28da8e93c8cb6ffaf94a08a472182c2626796d0fe680",
      "created_time": 1542542446
    }
  ],
  "more": true
}
```



##VENUS API 接口
###查询RTC发行数据*

```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 237
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "rtcmarket",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 10,
  "key_type": "",
  "index_position": "",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "supply": "10000000000.0000 RTCCORE",
      "base": {
        "balance": "999921256.2516 RTC",
        "weight": "0.50000000000000000"
      },
      "quote": {
        "balance": "10000787.5000 EOS",
        "weight": "0.50000000000000000"
      }
    }
  ],
  "more": false
}
```
response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| supply | string |  |  |
| base.balance | asset | C |  |
| quote.balance | asset | B| |

###买卖RTC、抵押、反抵押逻辑

- 用户使用EOS 购买RTC， 买入手续费为ratio， 则大约可兑换RTC为 eos_balance* (1-ratio)*C/B。 使用bignumber来计算。

- 卖出RTC时， 大约可兑换EOS为 rtc_balance * B/C * (1-ratio)。使用bignumber来计算。
- 抵押时， 是对用户的liquid RTC 操作， 每次抵押值为1000.0000 RTC 的整数倍， 可先取整，然后拖动slider
- 反抵押是对用户抵押某个交易对的RTC（effect+pending） 操作， 先取整， 再拖动显示。


### 查询全局状态数据*

```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 234
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "global",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 10,
  "key_type": "",
  "index_position": "",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "max_rtc_amount": "10000000000000",
      "total_rtc_reserved": 37499393,
      "total_eos_stake": 375000,
      "sudoer": "vns111111111",
      "admin": "vns111111111",
      "fee_account": "fee222222222",
      "dex_account": "yyy111111111",
      "pixiu": "ppp111111111",
      "ticker": "vns111111111",
      "fee": 4,
      "status": 1,
      "cantransfer": 0,
      "supply": 1000000000,
      "total_stakers": 2,
      "a1": "",
      "a2": "",
      "r1": 0,
      "r2": 0
    }
  ],
  "more": false
}
```

### 注册账号
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   openaccount| 注册账户 |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|owner | string | 用户账户名 | |
|ram_payer | string | RAM 支付账户， 同用户账户名| 实际支付RAM 的账户|

###注销账号
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   closeaccount| 注销账户， 所有余额为0才能成功 |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|owner | string | 用户账户名 | |

###查询账号
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取用户在VENUS合约的资产明细  |


request字段说明：

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 用户账号名|
| table| string |交易表名| 必须为accounts |
| limit| int | 最多获取的记录条数 | 默认1|
| lower_bound |uint64 | 转换用户账户名后的| 转换算法见上文|
| upper_bound | uint64| lower_bound+1 | | |
|index_position | int | 必须为2 |
| json | bool| 是否输出json格式| 设置为true|
| encode_type | string | dec| |

下面获取用户fff111111111 在VENUS 合约中的资产信息
```
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 278
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "accounts",
  "table_key": "",
  "lower_bound": "6545437268105511440", //fff111111111
  "upper_bound": "6545437268105511441",
  "limit": 10,
  "key_type": "i64",
  "index_position": "2",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 0,
      "owner": "fff111111111",
      "total": "1499.9600 RTC",
      "liquid": "499.9600 RTC",
      "staked": "1000.0000 RTC",
      "refund": "0.0000 RTC"
    }
  ],
  "more": false
}

```

response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| owner | string | 用户账户名 |  |
| total | asset | 资产总额 | total = liquid + staked + refund |
| liquid | asset | 流动资产 | 可sellrtc |
| staked | asset | 抵押资产| 抵押到交易对的资产| 
| refund | asset | 取消抵押后等待延时返回的资产|等待时间到，系统会自动返回； 如返回失败，需要用户自己发起refund 请求|

### 购买RTC
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | eosio.token| 必须为eosio.token |
| action   |   transfer| 购买RTC  |

request 字段说明
| name      |   type | desc |   remark |
| :-------- | --------| ------ | -------- |
|from | string | 用户账户名 |比如sam111111111 |
|to | string | VENUS合约名| 正式网络和测试网络不同|
|quantity| string |金额| 注意精度，比如EOS， 必须为"1.0001 EOS"的形式， 最小交易额为0.1000 EOS|
|memo| string |业务信息| 固定为"buyrtc"|


### 卖出RTC
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   sellrtc| 卖出RTC |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|account | string | 用户账户名 | |
|quantity | string | 卖出的RTC金额。注意精度, 例如 "1000.0000 RTC"， 不能超过自己的RTC 余额|


### 抵押分红
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   delegate| 抵押RTC 到指定交易对 |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|from | string | 用户账户名 | |
|quantity | string | 抵押的RTC金额。注意精度, 例如 "1000.0000 RTC" | 必须为1000.0000 RTC 的整数倍， 如抵押到达stake_threshold, 则无法成功， 抵押功能需disable|
|pair_id| uint64 | 交易对ID | 参考查询交易对部分|


###取消抵押
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   undelegate| 取消指定交易对的抵押的RTC |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|from | string | 用户账户名 | |
|quantity | string | 取消抵押的RTC金额。注意精度, 例如 "1000.0000 RTC" | 必须为1000.0000 RTC 的整数倍|
|pair_id| uint64 | 交易对ID | 参考查询交易对部分|

###赎回抵押资金
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   refund| 赎回抵押的RTC |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|owner | string | 用户账户名 | |

###查询用户抵押单个交易对的信息*
request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 抵押投资表名| 必须为stakes|
|lower_bound | string | 下限 | 见下面的说明 | 
| limit| int | 最多获取的记录条数 | 必须为1|
| key_type | string | 必须为"i128" |
|index_position | int | 必须为3 |
|encode_type | string | 必须为dec|
| json | bool| 是否输出json格式| 设置为true|
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 272
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "stakes",
  "table_key": "",
  "lower_bound": "0x104208218410D65A0300000000000000",
  "upper_bound": "",
  "limit": 1,
  "key_type": "i128",
  "index_position": "3",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 2,
      "pair_id": 3,
      "owner": "fff111111111",
      "effect": "0.0000 RTC",
      "pending": "1000.0000 RTC",
      "last_stake_time": 1543039806,
      "bonus_period": 1,
      "bonus_sum": "0.0000 EOS",
      "status": 0
    }
  ],
  "more": true
}
```

response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| id| uint64 | 抵押表Id | |
|pair_id | uint64 |交易对ID | |
|owner | string| 用户名| |
| effect| asset| 有效抵押| |
| pending| asset| 未生效抵押|需要到下一分红周期才会生效 |
| last_stake_time| unix timestamp|最近一次的抵押时间 | |
|bonus_period |uint64| 已领取的分红周期 | **当用户的领取分红周期小于交易对的分红周期时才能领取分红；如有未领取分红， 则无法抵押或反抵押** |
| bonus_sum|asset |用户在这个交易对领取的分红累计 | |

lower_bound 函数：
```
var Long = require("long");
const Eos = require("eosjs");

/**
 * Convert decimal string to hex string with little endian
 * 
 * @param {*} owner 
 */
function uint64_to_le_hex(owner) {
    var longVal = Long.fromString(owner, true, 10);
    var bytes = longVal.toBytesLE();
    var output = '0x';
    for (var i in bytes) {
        output += ("0"+(bytes[i].toString(16))).slice(-2).toUpperCase();
    }

    return output;
}

/**
 * Get unique key for querying stake record of user with specified pair
 * id
 * 
 * @param {*} user 
 * @param {*} pair_id 
 */
function get_unique_stake_with_pair(user, pair_id) {
    var temp = Eos.modules.format.encodeName(user, false);
    var pair_str = uint64_to_le_hex(pair_id);
    var owner_str = uint64_to_le_hex(temp);

    return owner_str+pair_str.substr(2);  
}

var lower_bound = get_unique_stake_with_pair('fff111111111', '1');
console.log(lower_bound); // will output 0x104208218410D65A0100000000000000
```


### 查询投资某个交易对的TOP3用户信息*

| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取交易对投资人资金排行表, 按降序  |

request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 抵押投资表名| 必须为stakes|
|lower_bound | string | 下限 | 见下面的说明 | 
|upper_bound | string | 上限 | 见下面的说明 |
| limit| int | 最多获取的记录条数 | 默认**3**|
| key_type | string | 必须为"i128" |
|index_position | int | 必须为7 |
|encode_type | string | 必须为dec|
| json | bool| 是否输出json格式| 设置为true|

lower_bound 和upper_bound为128位整形， 由2个字段拼接而成, 最低位在最右边。

| pair_id      |     asset   |
| :-------- | --------| 
|[79:64] |  [63:0]  |

举例查询交易对（1） 的投资toplist
| pair_id      |    asset   | lower_bound |
| :-------- | --------| ------ |  ------- | 
|`2`    |  固定为0  | 0x0000000000000000FEFFFFFFFFFFFFFF |

| pair_id      |     asset    | upper_bound |
| :-------- | --------| ------ |
|`1`    |   固定为0 |  0x0000000000000000FFFFFFFFFFFFFFFF |

拼接后的128位整形`取相反数`后输出为**little endian** 格式的16进制字符串即可。 则获取的范围是：
[ 0x0000000000000000FEFFFFFFFFFFFFFF, 0x0000000000000000FFFFFFFFFFFFFFFF )



response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |

```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 306
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "stakes",
  "table_key": "",
  "lower_bound": "0x0000000000000000FEFFFFFFFFFFFFFF",
  "upper_bound": "0x0000000000000000FFFFFFFFFFFFFFFF",
  "limit": 3,
  "key_type": "i128",
  "index_position": "7",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 1,
      "pair_id": 1,
      "owner": "fff222222222",
      "effect": "0.0000 RTC",
      "pending": "2000.0000 RTC",
      "last_stake_time": 1543014236,
      "bonus_period": 1,
      "bonus_sum": "0.0000 EOS",
      "status": 0
    },{
      "id": 0,
      "pair_id": 1,
      "owner": "fff111111111",
      "effect": "0.0000 RTC",
      "pending": "1000.0000 RTC",
      "last_stake_time": 1543014228,
      "bonus_period": 1,
      "bonus_sum": "0.0000 EOS",
      "status": 0
    }
  ],
  "more": false
}
```
lower_bound 和upper_bound 代码:
```
/**
 * Get top 3 staking record for the specified pair_id
 * 
 * @param {*} pair_id 
 */
function get_top_stake_key(pair_id) {
    var pair_str = uint64_to_le_hex('-'+pair_id);
    var asset_str = uint64_to_le_hex('-0');

    return asset_str+pair_str.substr(2);
}

// get top stakers for pair 1
var lower_bound = get_top_stake_key('2');
var upper_bound = get_top_stake_key('1');

console.log(lower_bound); // will output 0x0000000000000000FEFFFFFFFFFFFFFF
console.log(upper_bound); // will output 0x0000000000000000FFFFFFFFFFFFFFFF

// get top stakers for pair 2
var lower_bound = get_top_stake_key('3');
var upper_bound = get_top_stake_key('2');

console.log(lower_bound); // will output 0x0000000000000000FDFFFFFFFFFFFFFF
console.log(upper_bound); // will output 0x0000000000000000FEFFFFFFFFFFFFFF

// get top stakers for pair 3
var lower_bound = get_top_stake_key('4');
var upper_bound = get_top_stake_key('3');

console.log(lower_bound); // will output 0x0000000000000000FCFFFFFFFFFFFFFF
console.log(upper_bound); // will output 0x0000000000000000FDFFFFFFFFFFFFFF

```
### 查询我的交易所
查询用户投资的所有交易对
request字段说明：
| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string |交易表名| 必须为stakes |
| lower_bound |uint64 | 转换用户账户名后的值| 转换算法见上文|
| upper_bound | uint64| lower_bound+1 | | |
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|
| index_position | int | 必须为2| |

response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| id| uint64 | 抵押表Id | |
|pair_id | uint64 |交易对ID | |
|owner | string| 用户名| |
| effect| asset| 有效抵押| |
| pending| asset| 未生效抵押|需要到下一分红周期才会生效 |
| last_stake_time| unix timestamp|最近一次的抵押时间 | |
|bonus_period |uint64| 已领取的分红周期 | **当用户的领取分红周期小于交易对的分红周期时才能领取分红；如有未领取分红， 则无法抵押或反抵押** |
| bonus_sum|asset |用户在这个交易对领取的分红累计 | |
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 276
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "stakes",
  "table_key": "",
  "lower_bound": "6545437268105511440",
  "upper_bound": "6545437268105511441",
  "limit": 10,
  "key_type": "i64",
  "index_position": "2",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 0,
      "pair_id": 1,
      "owner": "fff111111111",
      "effect": "1000.0000 RTC",
      "pending": "0.0000 RTC",
      "last_stake_time": 1542459413,
      "bonus_period": 1,
      "bonus_sum": "0.0000 EOS",
      "status": 0
    }
  ],
  "more": false
}
```

###查询分红历史

###查询用户在某个交易对的当期分红*
request字段说明：
| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string |pair_id | `某个交易对的ID`|
| table| string | 分红历史表名| 必须为exchbonus|
| lower_bound | string | 用户领取的最后分红周期 bonus_period |
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 227
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "1",
  "table": "exchbonus",
  "table_key": "",
  "lower_bound": "1",
  "upper_bound": "",
  "limit": 10,
  "key_type": "",
  "index_position": "",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "period": 1,
      "total": "0.0035 EOS",
      "remain": "0.0035 EOS",
      "factor": "0.00000000000000000",
      "threshold_time": 1542459483,
      "end_time": 0,
      "status": 1
    },{
      "period": 2,
      "total": "0.0000 EOS",
      "remain": "0.0000 EOS",
      "factor": "0.00000000000000000",
      "threshold_time": 1543117600,
      "end_time": 0,
      "status": 0
    }
  ],
  "more": false
}
```
则用户可领取的当期分红为：
stakes.effect * exchbonus.factor
 交易对分红30天一期，如果用户没有及时领取， 也不会过期。 所以每个交易对保留了一份分红记录表 exchbonus, 里面记录着这个交易对每期的分红系数 factor,  假如用户的bonus_peroid 是1， 而此时的exchfees.total_period 是3, 则此时用户可以领取2次分红。 第一分红取lower_bound 为1， 得到factor_1， 用自己的stakes.effect * factor_1 就是第一期的应得分红； 领取后， 第二期， 取lower_bound 为2， 得到factor_2， 用自己的stakes.effect * factor_2 就是第二期的应得分红。 当用户的stakes.bonus_period == exchfee.total_period 时， 表示本期还未生成factor， 直接显示本期分红为0

###查询所有交易对分红资金池
request字段说明：
| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 交易对表名| 必须为exchfees|
| limit| int | 最多获取的记录条数 | 默认10|
| json | bool| 是否输出json格式| 设置为true|


```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 236
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "exchfees",
  "table_key": "",
  "lower_bound": "",
  "upper_bound": "",
  "limit": 10,
  "key_type": "",
  "index_position": "",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "pair_id": 1,
      "incoming": "0.0012 EOS",
      "total": "3000.0000 RTC",
      "effect": "1000.0000 RTC",
      "total_periods": 1,
      "base": {
        "sym": "4,EOS",
        "contract": "eosio.token"
      }
    },{
      "pair_id": 3,
      "incoming": "0.0010 EOS",
      "total": "0.0000 RTC",
      "effect": "0.0000 RTC",
      "total_periods": 1,
      "base": {
        "sym": "4,EOS",
        "contract": "eosio.token"
      }
    },{
      "pair_id": 4,
      "incoming": "0.0022 EOS",
      "total": "0.0000 RTC",
      "effect": "0.0000 RTC",
      "total_periods": 1,
      "base": {
        "sym": "4,EOS",
        "contract": "eosio.token"
      }
    }
  ],
  "more": false
}
```

response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| pair_id | uint64 |交易对ID | |
| incoming| asset | 该交易对分红池资金总额， 单位和下面的base字段相同 | |
| total | asset| 所有用户抵押的RTC总额， 包含pending部分| |
| effect| asset| 所有用户的有效抵押| |
| total_periods| uint64| 交易对的分红周期| |
| base| token|分红资金池token | |


###查询单个交易对分红资金池
| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 交易对表名| 必须为exchfees|
| lower_bound |string |交易对ID | |
| upper_bound | | | |
| index_position| uint64|索引 |必须为1 |
| limit| int | 最多获取的记录条数 | 必须为1|
| json | bool| 是否输出json格式| 设置为true|

```
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 240
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "exchfees",
  "table_key": "",
  "lower_bound": "1",
  "upper_bound": "",
  "limit": 1,
  "key_type": "i64",
  "index_position": "1",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "pair_id": 1,
      "incoming": "0.0131 EOS",
      "total": "1000.0000 RTC",
      "effect": "0.0000 RTC",
      "total_periods": 6,
      "base": {
        "sym": "4,EOS",
        "contract": "eosio.token"
      }
    }
  ],
  "more": true
}

```

response 字段说明：
| name      |   type |  desc   |   remark |
| :-------- | -------- | ------ | ------ |
| pair_id | uint64 |交易对ID | |
| incoming| asset | 该交易对分红池资金总额， 单位和下面的base字段相同 | |
| total | asset| 所有用户抵押的RTC总额， 包含pending部分| |
| effect| asset| 所有用户的有效抵押| |
| total_periods| uint64| 交易对的分红周期| |
| base| token|分红资金池token | |


###领取分红
| 配置      |   参数值  |  说明 | 
| :-------- | --------| ------ |
| contract | VENUS合约名 |正式网络和测试网络不同|
| action   |   claimbonus| 领取交易对的分红 |

request 字段说明
| name      |    type | desc |   remark |
| :-------- | --------| ------ |
|from | string | 用户账户名 | |
|pair_id| uint64 | 交易对ID | 参考查询交易对部分|

###查询最近抵押的账户*
| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 抵押表名| 必须为stakes|
| lower_bound |string |必须为"1" | |
| upper_bound | | | |
| index_position| uint64|索引 |必须为4 |
| limit| int | 最多获取的记录条数 | 默认为10|
| json | bool| 是否输出json格式| 设置为true|
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 238
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "stakes",
  "table_key": "",
  "lower_bound": "1",
  "upper_bound": "",
  "limit": 1,
  "key_type": "i64",
  "index_position": "4",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 1,
      "pair_id": 1,
      "owner": "fff222222222",
      "effect": "0.0000 RTC",
      "pending": "2000.0000 RTC",
      "last_stake_time": 1542468650,
      "bonus_period": 1,
      "bonus_sum": "0.0000 EOS",
      "status": 0
    }
  ],
  "more": true
}
```

###查询最近抵押某个交易对的账户*
| URI      |     METHOD |  说明 | 
| :-------- | --------| ------ |
| /v1/chain/get_table_rows    |   POST | 获取交易对投资人资金排行表, 按降序  |

request字段说明：（未注明的部分和示例一样即可）

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| code    |  string | VENUS合约名  |  测试网络用vns111111111, 正式网络待定  |
| scope| string | 范围 | 同上面的code即可|
| table| string | 抵押投资表名| 必须为stakes|
|lower_bound | string | 下限 | 见下面的说明 | 
|upper_bound | string | 上限 | 见下面的说明 |
| limit| int | 最多获取的记录条数 | 默认为3|
| key_type | string | 必须为"i128" |
|index_position | int | 必须为5 |
|encode_type | string | 必须为dec|
| json | bool| 是否输出json格式| 设置为true|

lower_bound 和upper_bound为128位整形， 由2个字段拼接而成, 最低位在最右边。

| pair_id      |     asset   |
| :-------- | --------| 
|[79:64] |  [63:0]  |

举例查询交易对（1） 的投资toplist
| pair_id      |    asset   | lower_bound |
| :-------- | --------| ------ |  ------- | 
|`2`    |  固定为0  | 0x0000000000000000FEFFFFFFFFFFFFFF |

| pair_id      |     asset    | upper_bound |
| :-------- | --------| ------ |
|`1`    |   固定为0 |  0x0000000000000000FFFFFFFFFFFFFFFF |

拼接后的128位整形`取相反数`后输出为**little endian** 格式的16进制字符串即可。 则获取的范围是：
[ 0x0000000000000000FEFFFFFFFFFFFFFF, 0x0000000000000000FFFFFFFFFFFFFFFF )
```
REQUEST:
---------------------
POST /v1/chain/get_table_rows HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 306
Accept: */*
Connection: close

{
  "json": true,
  "code": "vns111111111",
  "scope": "vns111111111",
  "table": "stakes",
  "table_key": "",
  "lower_bound": "0x0000000000000000FEFFFFFFFFFFFFFF",
  "upper_bound": "0x0000000000000000FFFFFFFFFFFFFFFF",
  "limit": 1,
  "key_type": "i128",
  "index_position": "5",
  "encode_type": "dec"
}
---------------------
RESPONSE:
---------------------
{
  "rows": [{
      "id": 1,
      "pair_id": 1,
      "owner": "fff222222222",
      "effect": "0.0000 RTC",
      "pending": "2000.0000 RTC",
      "last_stake_time": 1543014236,
      "bonus_period": 1,
      "bonus_sum": "0.0000 EOS",
      "status": 0
    }
  ],
  "more": true
}
```
lower_bound 和upper_bound 的计算请参考 get_top_stake_key， 和上面的完全一样。


##风控 API 接口
###查询黑名单
###查询watch合约

##通用接口
### 查询账户资源信息
request字段说明：

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| account_name    |  string | 登录的用户账号  |   |

```
REQUEST:
---------------------
POST /v1/chain/get_account HTTP/1.0
Host: 39.108.231.157:30065
content-length: 36
Accept: */*
Connection: close

{
  "account_name": "fff111111111"
}
---------------------
RESPONSE:
---------------------
{
  "account_name": "fff111111111",
  "head_block_num": 24014098,
  "head_block_time": "2018-12-05T13:23:22.000",
  "privileged": false,
  "last_code_update": "1970-01-01T00:00:00.000",
  "created": "2018-10-07T10:25:13.000",
  "core_liquid_balance": "595.6750 EOS",
  "ram_quota": 513408,
  "net_weight": 20000,
  "cpu_weight": 1010000,
  "net_limit": {
    "used": 153,
    "available": 1446734,
    "max": 1446887
  },
  "cpu_limit": {
    "used": 4491,
    "available": 13467788,
    "max": 13472279
  },
  "ram_usage": 157322,
  "permissions": [{
      "perm_name": "active",
      "parent": "owner",
      "required_auth": {
        "threshold": 1,
        "keys": [{
            "key": "EOS7Bc95sRy8nGua1uToESqqgdBx89aoP5bysWPfmZGW5H42nbdaj",
            "weight": 1
          }
        ],
        "accounts": [],
        "waits": []
      }
    },{
      "perm_name": "owner",
      "parent": "",
      "required_auth": {
        "threshold": 1,
        "keys": [{
            "key": "EOS6qoULPbSerbpNXN7NW9uCWq6EZZg1kZyJXLeRQjyEF5sxegTjW",
            "weight": 1
          }
        ],
        "accounts": [],
        "waits": []
      }
    }
  ],
  "total_resources": {
    "owner": "fff111111111",
    "net_weight": "2.0000 EOS",
    "cpu_weight": "101.0000 EOS",
    "ram_bytes": 512008
  },
  "self_delegated_bandwidth": {
    "from": "fff111111111",
    "to": "fff111111111",
    "net_weight": "1.0000 EOS",
    "cpu_weight": "100.0000 EOS"
  },
  "refund_request": null,
  "voter_info": {
    "owner": "fff111111111",
    "proxy": "",
    "producers": [],
    "staked": 1010000,
    "last_vote_weight": "0.00000000000000000",
    "proxied_vote_weight": "0.00000000000000000",
    "is_proxy": 0,
    "reserved1": 0,
    "reserved2": 0,
    "reserved3": "0 "
  }
}
```
response字段说明：

| name      |    type | desc |   remark |
| :-------- | --------| ------ | ------ |
| cpu_limit.used    |  int | 已使用CPU  |  单位us |
| cpu_limit.available    |  int | 剩余CPU  |  单位us |
| cpu_limit.max    |  int | 总的CPU  |  单位us |
| net_limit.used    |  int | 已使用NET  |  单位byte |
| net_limit.available    |  int | 剩余NET  | 单位byte  |
| net_limit.max    |  int | 总的NET  |  单位byte |
| ram_usage   |  int | 已使用RAM  |  单位byte |
| ram_quota   |  int | 总的RAM  |  单位byte |

 
###查询区块交易
输入获取的txid， 得到交易后取json 对象的trx.trx.actions[0].data 中的内容即可。
```
---------------------
POST /v1/history/get_transaction HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 78
Accept: */*
Connection: close

{
  "id": "864bc3ffe7e891b97f37f5418284fcf49eb3b9ec7a38f8b57282ab9e89af625a"
}
---------------------
RESPONSE:
---------------------
{
  "id": "864bc3ffe7e891b97f37f5418284fcf49eb3b9ec7a38f8b57282ab9e89af625a",
  "trx": {
    "receipt": {
      "status": "executed",
      "cpu_usage_us": 1004,
      "net_usage_words": 16,
      "trx": [
        1,{
          "signatures": [
            "SIG_K1_K6RoW4GsKudaQuEPrYbJRSh4f4xJMUUAhaMogDX1pXjt8amCyqZAYzUhgZsSprGLeMZbcWfcaoM4rgR915BRYEeZ3gNPLE"
          ],
          "compression": "none",
          "packed_context_free_data": "",
          "packed_trx": "ed2df45b0d06f609ae560000000001104208218410bcf70000f0cb653a533201104208218410bcf700000000a8ed32321d0d4f4d472f454f53e4b88ae7babf0e6f6d672f656f735f6f6e6c696e6500"
        }
      ]
    },
    "trx": {
      "expiration": "2018-11-20T15:53:17",
      "ref_block_num": 1549,
      "ref_block_prefix": 1454246390,
      "max_net_usage_words": 0,
      "max_cpu_usage_ms": 0,
      "delay_sec": 0,
      "context_free_actions": [],
      "actions": [{
          "account": "yyy111111111",
          "name": "addnotify",
          "authorization": [{
              "actor": "yyy111111111",
              "permission": "active"
            }
          ],
          "data": {
            "info_cn": "OMG/EOS上线",
            "info_en": "omg/eos_online"
          },
          "hex_data": "0d4f4d472f454f53e4b88ae7babf0e6f6d672f656f735f6f6e6c696e65"
        }
      ],
      "transaction_extensions": [],
      "signatures": [
        "SIG_K1_K6RoW4GsKudaQuEPrYbJRSh4f4xJMUUAhaMogDX1pXjt8amCyqZAYzUhgZsSprGLeMZbcWfcaoM4rgR915BRYEeZ3gNPLE"
      ],
      "context_free_data": []
    }
  },
  "block_time": "2018-11-20T15:52:48.000",
  "block_num": 21694300,
  "last_irreversible_block": 21696032,
  "traces": [{
      "receipt": {
        "receiver": "yyy111111111",
        "act_digest": "1d1f270ba4f8f884b78af23690fd8559ff10827e64a9512f0606f9a41f350c63",
        "global_sequence": 136742282,
        "recv_sequence": 696,
        "auth_sequence": [[
            "yyy111111111",
            1441
          ]
        ],
        "code_sequence": 79,
        "abi_sequence": 64
      },
      "act": {
        "account": "yyy111111111",
        "name": "addnotify",
        "authorization": [{
            "actor": "yyy111111111",
            "permission": "active"
          }
        ],
        "data": {
          "info_cn": "OMG/EOS上线",
          "info_en": "omg/eos_online"
        },
        "hex_data": "0d4f4d472f454f53e4b88ae7babf0e6f6d672f656f735f6f6e6c696e65"
      },
      "context_free": false,
      "elapsed": 237,
      "console": "864bc3ffe7e891b97f37f5418284fcf49eb3b9ec7a38f8b57282ab9e89af625a",
      "trx_id": "864bc3ffe7e891b97f37f5418284fcf49eb3b9ec7a38f8b57282ab9e89af625a",
      "block_num": 21694300,
      "block_time": "2018-11-20T15:52:48.000",
      "producer_block_id": "014b075cc6920703bd3c6aeba3e66abe91015e90fcc56e01f3f8c7c577654e43",
      "account_ram_deltas": [{
          "account": "yyy111111111",
          "delta": 284
        }
      ],
      "except": null,
      "inline_traces": []
    }
  ]
}
```

## 错误处理
###错误信息表

```
REQUEST:
---------------------
POST /v1/chain/push_transaction HTTP/1.0
Host: api-kylin.eoshenzhen.io:8890
content-length: 344
Accept: */*
Connection: close

{
  "signatures": [
    "SIG_K1_KX1SRsvEKy4MBcDoHLEypmwCPE2LCXnTVyP4WpMQ6cAskTyPP5BdSVR2quRQSobMzfBNGeKeWCXzMK5q1od3zLqs2aKwDE"
  ],
  "compression": "none",
  "packed_context_free_data": "",
  "packed_trx": "6a89fa5b6c4b68795df80000000001104208218410bcf70000000000d0b0ae01104208218410d65a00000000a8ed323210104208218410d65ac80000000000000000"
}
---------------------
RESPONSE:
---------------------
{
  "code": 500,
  "message": "Internal Service Error",
  "error": {
    "code": 3050003,
    "name": "eosio_assert_message_exception",
    "what": "eosio_assert_message assertion failure",
    "details": [{
        "message": "assertion failure with message: err_order_not_found",
        "file": "wasm_interface.cpp",
        "line_number": 934,
        "method": "eosio_assert"
      },{
        "message": "pending console output: ",
        "file": "apply_context.cpp",
        "line_number": 72,
        "method": "exec_one"
      }
    ]
  }
}
---------------------
Error 3050003: eosio_assert_message assertion failure
Error Details:
assertion failure with message: err_order_not_found
pending console output: 

```
前端根据提取的 assert message 后的错误消息， 显示当前语言中的error infomation;  当没有找到时， 原样显示。

| Id      |     desc |   remark   |
| :-------- | --------:| :------: |
| err_exceed_supply    |   超出供给 | 联系官方  |


