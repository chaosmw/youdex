#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>

using namespace std;

// borrowed from config.hpp
const uint32_t   default_max_block_cpu_usage                 = 200'000; /// max block cpu usage in microseconds
const uint32_t   default_max_transaction_cpu_usage           = 3*default_max_block_cpu_usage/4; /// max trx cpu usage in microseconds
const uint32_t   default_min_transaction_cpu_usage           = 100; /// min trx cpu usage in microseconds (10000 TPS equiv)

namespace eosio {

    struct account {
        asset    balance;
        uint64_t primary_key()const { return balance.symbol.name(); }
    };
    typedef eosio::multi_index<N(accounts), account> accounts;

    asset get_current_balance(account_name account, const extended_symbol& quote) {
        accounts test_acnts(quote.contract, account);
        auto myquote = test_acnts.find(quote.name());
        auto current_quote = asset(0, quote);
        
        if (myquote != test_acnts.end()) {
            current_quote = myquote->balance; 
        }

        return current_quote;
    }

    // Remove limited records from table
    template <uint64_t TableName, typename T, typename ... Indices>
    uint64_t clear_table(multi_index<TableName, T, Indices...> &table, uint64_t limit) {
        auto it = table.begin();

        uint64_t count = 0;
        while (it != table.end() && count < limit) {
            it = table.erase(it);
            count++;
        }

        name temp{.value = TableName};

        print_f(" Erased % records from table %\n", count, temp);

        return count;
    } 

    uint128_t make_key_128(uint64_t a, uint64_t b) {
        uint128_t temp = a;
        return temp << 64 | b;
    }

    void split_memo(std::vector<std::string>& results, const std::string& memo,
               char separator) {
        auto start = memo.cbegin();
        auto end = memo.cend();

        for (auto it = start; it != end; ++it) {
            if (*it == separator) {
                results.emplace_back(start, it);
                start = it + 1;
            }
        }
        if (start != end) results.emplace_back(start, end);
    }

    // recursively print digit 
    int print_ll(char* q, int64_t num, uint8_t max) {
        eosio_assert(max > 0, "at least 1 digit");
        eosio_assert(num >= 0, "negative value is not allowed");

        if (num < 10) {
            *q = num + '0';
            return 1;
        }

        int digit = num % 10;
        int n = print_ll(q, num / 10, max);
        eosio_assert(n < max, "out of range of buffer");

        *(q+n) = digit + '0';
        n++;

        return n;
    }

} /// namespace eosio