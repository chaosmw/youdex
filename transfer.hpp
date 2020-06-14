#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>

using namespace eosio;

struct transfer_args
{
    account_name from;
    account_name to;
    asset quantity;
    std::string memo;

    void print() const {
        eosio::print(name{.value=from}, " | ", name{.value=to}, " | ", quantity, " | ", memo);
    }

    EOSLIB_SERIALIZE(transfer_args, (from)(to)(quantity)(memo));
};
