#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>

using namespace eosio;
using namespace std;

namespace vns {
#define RTC_SYMBOL S(4, RTC)
const uint64_t each_rtc_stake = 1000*10000ll; // 10 EOS at initial stage
    // every exchange pair has a exch_fee record
    // (_self, _self)
    struct exch_fee {
        uint64_t    pair_id;
        asset       incoming;   // EOS or RTC?  
        asset       total;      // total staked rtc asset
        asset       effect;     // effect rtc for bonus
        uint64_t    total_periods;
        extended_symbol base;

        uint64_t primary_key()const { return  pair_id; }
        // since we can support different base tokens, such as EOS or RTC
        // so it's only a reference for amount
        uint64_t get_incoming() const { return static_cast<uint64_t>(-incoming.amount); } 
        EOSLIB_SERIALIZE( exch_fee, (pair_id)(incoming)(total)(effect)(total_periods)(base) )
    };

    typedef eosio::multi_index< N(exchfees), exch_fee,
            indexed_by<N(byincoming), const_mem_fun<exch_fee, uint64_t, &exch_fee::get_incoming>>
            > exchfee_table;
} /// namespace vns