#pragma once
#include <eosiolib/asset.hpp>
#include <eosiolib/multi_index.hpp>

using namespace eosio;

// Define special account
struct special_account {
    account_name    name;
    uint32_t        created_time;
    uint64_t primary_key() const { return name; }

    EOSLIB_SERIALIZE(special_account, (name)(created_time))
};

typedef eosio::multi_index<N(specialacnts), special_account> specialacnt_table;

struct watch_contract {
    account_name    contract;
    uint32_t        created_time;
    uint64_t primary_key() const { return contract; }

    EOSLIB_SERIALIZE(watch_contract, (contract)(created_time))
};

typedef eosio::multi_index<N(watchacnts), watch_contract> watch_table;