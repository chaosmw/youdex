#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include "utils.hpp"
#include "safe.hpp"

using namespace std;

namespace exch {
    static const uint64_t max_order_id      = safe<uint64_t>::max().value;
    const uint64_t max_exch_pair_id         = 0x0000FFFF;
    typedef name token_name;
    // Define the status of exchange pair
    enum status : uint8_t {
        PENDING         = 0, // wait for staking and voting
        ACTIVE          = 1, // ready for exchange
        DEACTIVATED     = 2, // disabled temprarily
        DISABLED        = 3, // shutdown
    };

    bool is_valid_exchange_status(uint8_t status) {
        return (status == PENDING || status == ACTIVE || status == DEACTIVATED || status == DISABLED);
    }

    typedef uint16_t exchange_pair_id;

    // (_self, _self)
    struct exchange_runtime {
        uint64_t        pair_id;

        uint64_t        ask_orders;
        uint64_t        bid_orders;
        uint64_t        vote_count;

        // K line data within 24 hours
        asset           latest_price;
        asset           open_price;
        asset           close_price;
        asset           highest_price;
        asset           lowest_price;

        asset           total_quote;
        asset           total_base;

        time            lastmod_time;
        asset           current_quote; // to trace incoming quote balance
        // future extension
        uint64_t        r1;
        uint64_t        r2;

        auto primary_key() const { return pair_id; }

        uint64_t get_ask_orders() const { return ask_orders; }
        uint64_t get_bid_orders() const { return bid_orders; }
        uint64_t get_total_orders() const { return ask_orders + bid_orders; }
        uint64_t get_votes() const { return vote_count; }
        uint64_t get_total_quote() const { return static_cast<uint64_t>(-total_quote.amount); } 
        uint64_t get_total_base() const { return static_cast<uint64_t>(-total_base.amount); }

        EOSLIB_SERIALIZE(exchange_runtime, (pair_id)(ask_orders)(bid_orders)(vote_count)
                                           (latest_price)(open_price)(close_price)(highest_price)(lowest_price)
                                           (total_quote)(total_base)(lastmod_time)(current_quote)(r1)(r2)) 
    };

    typedef eosio::multi_index< N(exchruntimes), exchange_runtime,
        indexed_by< N( byask ), const_mem_fun< exchange_runtime, uint64_t, &exchange_runtime::get_ask_orders> >,
        indexed_by< N( bybid ), const_mem_fun< exchange_runtime, uint64_t, &exchange_runtime::get_bid_orders> >,
        indexed_by< N( bytotal ), const_mem_fun< exchange_runtime, uint64_t, &exchange_runtime::get_total_orders> >,
        indexed_by< N( byvote ), const_mem_fun< exchange_runtime, uint64_t, &exchange_runtime::get_votes> >,
        indexed_by< N( byquote ), const_mem_fun< exchange_runtime, uint64_t, &exchange_runtime::get_total_quote> >,
        indexed_by< N( bybase ), const_mem_fun< exchange_runtime, uint64_t, &exchange_runtime::get_total_base> >

        > exchange_runtime_table; 

    // Define exchange pair
    // (_self, _self)
    struct exchange_pair {
        uint64_t        id;      // We support at most 65536 exchange pairs
        account_name    manager; 
        extended_symbol quote;
        extended_symbol base;
        uint64_t        quote_precision; // save cpu with space
        uint64_t        base_precision;
        uint64_t        scale;
        time            created_time;
        time            lastmod_time;
        uint8_t         status;
        uint64_t        vote_threshold;
        uint64_t        stake_threshold; // RTC, pay attention to precision

        checksum256     txid;

        uint64_t        r1; 
        uint64_t        r2;
        uint64_t        r3;
        uint64_t        r4;

        auto primary_key() const { return id; }

        uint64_t get_quote() const { return quote.name(); }
        uint64_t get_base() const { return base.name(); }
        uint64_t get_status() const { return status; }
        uint128_t get_pair() const { return make_key_128(quote.value, base.value); }

        EOSLIB_SERIALIZE( exchange_pair, (id)(manager)(quote)(base)(quote_precision)(base_precision)(scale)(created_time)(lastmod_time)(status)\
                                        (vote_threshold)(stake_threshold)(txid)(r1)(r2)(r3)(r4))
    };

    typedef eosio::multi_index< N(exchpairs), exchange_pair,
        indexed_by< N( byquote ), const_mem_fun< exchange_pair, uint64_t, &exchange_pair::get_quote> >,
        indexed_by< N( bybase ), const_mem_fun< exchange_pair, uint64_t, &exchange_pair::get_base> >,
        indexed_by< N( bystatus ), const_mem_fun< exchange_pair, uint64_t, &exchange_pair::get_status> >,
        indexed_by< N( bypair ), const_mem_fun< exchange_pair, uint128_t, &exchange_pair::get_pair> >
        > exchange_pair_table;

    bool check_symbol(const symbol_type& base, const symbol_type& price, uint64_t scale) {
        do {
            // check symbol name
            if (base.name() != price.name()) break;
            // check precision
            uint64_t bp = base.precision();
            uint64_t pp = price.precision();
            int64_t p10 = 1;

            while (bp < pp && p10 < scale) {
                p10 *= 10; bp++;
            }

            if (scale != p10 || bp != pp) break;

            return true;
        } while (0);

        return false;
    }

    symbol_type get_price_symbol(const symbol_type& base, uint64_t scale) {
        uint64_t precision = base.precision();
        int64_t p10 = 1;
        while (p10 < scale && precision < 18/*max_precision*/) {
            p10 *= 10; precision++;
        }

        return (base.name() << 8) | precision;
    }

    symbol_type append_order(account_name self, uint64_t pair_id, bool ask, account_name contract, symbol_type symbol, asset& quantity) {
        exchange_pair_table exchpairs(self, self);
        auto itr = exchpairs.find(pair_id);
        
        eosio_assert(itr != exchpairs.end(), "err_invalid_pair_id");
        eosio_assert(itr->status == ACTIVE, "err_pair_not_activated");
        eosio_assert(check_symbol(itr->base, symbol, itr->scale), "err_declared_symbol");

        if (ask) {
            eosio_assert(itr->quote.contract == contract, "err_declared_contract");
        } else {
            eosio_assert(itr->base.contract == contract, "err_declared_contract");
        }
        // update runtime information
        exchange_runtime_table exchruntimes(self, self);
        auto runtime_itr = exchruntimes.find(pair_id);
        eosio_assert(runtime_itr != exchruntimes.end(), "err_runtime_not_found");

        exchruntimes.modify(runtime_itr, 0, [&](auto& er) {
            if (ask) {
                eosio_assert(er.ask_orders < max_order_id, "err_exceed_max_order" );
                er.ask_orders++;
                // check malicious contract
                auto current_quote = get_current_balance(self, itr->quote);
                auto temp = er.current_quote + quantity;
                // TODO: we had planned to detect every outgoing asset, however, this will 
                // introduce more CPU usage.
                if (current_quote != temp) {
                    exchpairs.modify(itr, 0, [&](auto& ep) {
                        ep.status = DEACTIVATED; 
                    });
                    // update it
                    quantity = current_quote - er.current_quote;
                    er.current_quote = current_quote;
                    print_f("abnormal contract for pair %, suspend it anyway\n", pair_id);
                } else {
                    er.current_quote = temp;
                }
            } else {
                eosio_assert(er.bid_orders < max_order_id, "err_exceed_max_order" );
                er.bid_orders++; 
            }
        });

        return ask ? itr->base : itr->quote;
    }

} /// namespace exch