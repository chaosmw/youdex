#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include "utils.hpp"
#include "safe.hpp"

using namespace std;

namespace eosio {
    const uint64_t max_vote_allowed = 100000ll;
    // (_self, _self)
    struct vote_info {
        uint64_t id; // we can use this id for activity
        uint64_t pair_id;
        account_name from;
        account_name referee; 
        uint32_t    timestamp; 
        
        uint64_t primary_key()const { return id; }

        uint64_t get_pair() const { return pair_id; }
        uint64_t get_from()const { return from; }
        uint64_t get_referee()const { return referee; }
        uint64_t get_timestamp() const { return (uint64_t)timestamp; }
        
        uint128_t get_pair_from() const { return make_key_128(pair_id, from); }

        EOSLIB_SERIALIZE(vote_info, (id)(pair_id)(from)(referee)(timestamp))
    };

    typedef eosio::multi_index< N(votes), vote_info,
        indexed_by< N(bypair), const_mem_fun<vote_info, uint64_t, &vote_info::get_pair> >,
        indexed_by< N(byfrom), const_mem_fun<vote_info, uint64_t, &vote_info::get_from> >,
        indexed_by< N(byref), const_mem_fun<vote_info, uint64_t, &vote_info::get_referee> >,
        indexed_by< N(bytimestamp), const_mem_fun<vote_info, uint64_t, &vote_info::get_timestamp> >,
        indexed_by< N(bypairfrom), const_mem_fun<vote_info, uint128_t, &vote_info::get_pair_from> >
         > vote_table;

} /// namespace eosio