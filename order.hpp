#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include "exchange_types.hpp"

using namespace eosio;
using namespace std;

namespace dex {

    // caution: the order is deliberately designd for matching routine, do not change it!!!
    // see get_range
    enum order_type: uint8_t {
        RESERVED                    = 0,
        ASK_MARKET                  = 1,
        ASK_LIMITED                 = 2,
        BID_MARKET                  = 3, // so we can use them for tick matching, do NOT change the order!
        BID_LIMITED                 = 4, // biding orders are always at the top price list, when calling get_price
    };

    uint128_t make_match_128(uint16_t pid, uint8_t type, asset price) {
        uint128_t temp = 0;
        uint128_t pid128 = (uint128_t)pid;
        uint128_t type128 = (uint128_t)type;
        // Notic that for bidding orders, we use reverse order!!!
        uint64_t price64 = (type != BID_LIMITED) ? price.amount : static_cast<uint64_t>(-price.amount);
        temp = (pid128 << 72) | (type128 << 64) | price64;
        
        return temp;
    }

    enum close_reason: uint8_t {
        NORMAL                      = 0,
        CANCELLED                   = 1,
        EXPIRED                     = 2,
        NOT_ENOUGH_FEE              = 3,
    };

    const uint8_t ORDER_TYPE_MASK = 0x0F;

    auto temp = make_tuple(0, 0, 0);
    typedef asset   order_price;
    typedef asset   quote_amount;

    // Orders are stored within different scopes?
    // (_self, _self)
    struct order {
        uint64_t        id;
        uint64_t        gid; // primary key
        account_name    owner;
        exch::exchange_pair_id pair_id;
        order_type      type;

        // Timing related fileds
        time            placed_time;
        time            updated_time;
        time            closed_time;
        uint8_t         close_reason; 
        time            expiration_time;
        
        asset           initial;
        asset           remain;
        order_price     price;

        quote_amount    deal;

        void reset() {
            // CAN NOT CHANGE GID
            id              = 0;
            pair_id         = 0;
            type            = RESERVED;
            placed_time     = 0;
            updated_time    = 0;
            closed_time     = 0;
            close_reason    = 0;
            expiration_time = 0;
            initial         = asset(0);
            remain          = asset(0);
            price           = asset(0);
            deal            = asset(0);
        }
        
        auto primary_key() const { return gid; }

        uint64_t get_id() const { return id; }
        uint64_t get_expiration() const { return expiration_time; }
        uint64_t get_owner() const { return owner; }
        uint128_t get_slot() const { return make_key_128(owner, type); }
        uint128_t get_price() const { return make_match_128(pair_id, type, price); }

        EOSLIB_SERIALIZE( order, (id)(gid)(owner)(pair_id)(type)(placed_time)(updated_time)\
            (closed_time)(close_reason)(expiration_time)(initial)(remain)(price)(deal) )
    };

    typedef eosio::multi_index< N(orders), order, 
        indexed_by< N( byid ), const_mem_fun< order, uint64_t, &order::get_id> >,
        indexed_by< N( byexp ), const_mem_fun< order, uint64_t, &order::get_expiration> >,
        indexed_by< N( byprice ), const_mem_fun< order, uint128_t, &order::get_price> >,
        indexed_by< N( byowner ), const_mem_fun< order, uint64_t, &order::get_owner> >, 
        indexed_by< N( byslot ), const_mem_fun< order, uint128_t, &order::get_slot> > 
        > order_table;


    bool match();

    bool is_ask_order(order_type type) {
        if (type == ASK_LIMITED || type == ASK_MARKET) {
            return true;
        }

        return false;
    }

    bool is_limited_order(uint8_t type) {
        if (type == ASK_LIMITED || type == BID_LIMITED) {
            return true;
        }

        return false;
    }

    // pay attention to the order
    void get_range(order_type type, bool match_market, order_type& low, order_type& high) {
        switch (type) {
            case ASK_LIMITED:   high = BID_LIMITED; low = match_market ? BID_MARKET : high; return;
            case ASK_MARKET:    low = high = BID_LIMITED; return;
            case BID_LIMITED:   high = ASK_LIMITED;  low = match_market? ASK_MARKET : high; return;
            case BID_MARKET:    low = high = ASK_LIMITED; return;
            default: eosio_assert(false, "not supported yet");
        } 
    }

    bool parse_order_from_string(string &memo, order& o, bool& match_now);

} /// namespace dex