#pragma once
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include "safe.hpp"

using namespace std;
namespace eosio {
    static constexpr uint8_t max_precision = 18;

    struct myasset: asset {
        myasset(int64_t a = 0, symbol_type s = CORE_SYMBOL):asset(a, s){}
        
        void print()const {
            if (amount < 0) {
                prints("-");
                asset(std::abs(amount), symbol).print();
            } else {
                asset::print();
            }
        }

        // print to string
        void print_s(string& s) const {
            static const int max_buf_len = 64; 
            int64_t p = (int64_t)symbol.precision();
            int64_t p10 = 1;
            while( p > 0  ) {
                p10 *= 10; --p;
            }
            p = (int64_t)symbol.precision();
            char buf[max_buf_len];
            memset(buf, 0, max_buf_len);
            char* q = buf;

            int64_t amount2 = amount;
            if (amount < 0) {
                *q++ = '-';
                amount2 = std::abs(amount);
            }

            int64_t int_part = amount2 / p10;

            q += print_ll(q, amount2 / p10, max_buf_len-1-(1+p+1+7));

            *q++ = '.';
            auto change = amount2 % p10;

            for( int64_t i = p -1; i >= 0; --i ) {
                q[i] = (change % 10) + '0'; 
                change /= 10;
            }

            q += p;

            *q++ = ' ';

            auto sym = symbol.value;
            sym >>= 8;
            for( int i = 0; i < 7; ++i ) {
                char c = (char)(sym & 0xff);
                if( !c ) break;
                *q++ = c;
                sym >>= 8;
            }

            eosio_assert((q-buf)<max_buf_len, "exceed max_buf_len");

            s = string(buf);
        }
    };

    template<typename T> 
    T data_as(bytes data) {
        return unpack<T>( &data[0], data.size() );
    }

    template<typename T>
    T data_as(std::string str) {
        bytes data(str.begin(), str.end());
        return unpack<T>( &data[0], data.size() );
    }

    void trim(std::string& str) {
        str.erase(str.begin(), find_if(str.begin(), str.end(), [](int ch) {
            return !isspace(ch);
        }));

        str.erase(find_if(str.rbegin(), str.rend(), [](int ch) {
            return !isspace(ch);
        }).base(), str.end());
    }

    template <typename T>
    T to_int( const string&i ) {
        const char* s = i.c_str();
	    int neg=0;

	    while (isspace(*s)) s++;
	    switch (*s) {
	        case '-': neg=1;
	        case '+': s++;
	    }

        safe<T> n(0);
        safe<T> ten(10);

        while (isdigit(*s)) {
            n = ten*n + safe<T>(*s++ - '0');
        }

        return neg ? (-n).value : n.value;
    }

    symbol_type parse_symbol_from_string(const string &str) {
        static constexpr uint8_t max_precision = 18;
        string s(str);

        trim(s);
        eosio_assert(!s.empty(), "creating symbol from empty string");

        auto comma_pos = s.find(',');
        eosio_assert(comma_pos != string::npos, "missing comma in symbol");

        auto prec_part = s.substr(0, comma_pos);
        uint8_t p = to_int<int64_t>(prec_part);
        string name_part = s.substr(comma_pos + 1);
        eosio_assert( p <= max_precision, "precision should be <= 18");

        return symbol_type(string_to_symbol(p, name_part.c_str()));
    }

    // parse with precision and symbol
    symbol_type parse_symbol2(uint8_t p, const string &str) {
        
        string s(str);

        trim(s);
        eosio_assert(!s.empty(), "creating symbol from empty string");
        eosio_assert( (p >= 0 && p <= max_precision), "precision should be <= 18");

        return symbol_type(string_to_symbol(p, s.c_str()));
    }

    uint64_t get_precision(uint8_t p) {
        eosio_assert( (p >= 0 && p <= max_precision), "precision should be <= 18");
        safe<uint64_t> p10(1);
        safe<uint64_t> ten(10);
        while ( p > 0 ) {
            p10 *= ten; --p;
        }

        return p10.value;
    }

    // takes a string of the form "10.0000 CUR" and constructs an asset 
    // with amount = 10 and symbol(4,"CUR")
    myasset parse_asset_from_string(const string &str) {
        string from(str);
        trim(from);
        // Find space in order to split amount and symbol
        auto space_pos = from.find(' ');
        eosio_assert(space_pos != string::npos, "Asset's amount and symbol should be separated with space");

        auto symbol_str = from.substr(space_pos + 1);
        trim(symbol_str);
        auto amount_str = from.substr(0, space_pos);

        // Ensure that if decimal point is used (.), decimal fraction is specified
        auto dot_pos = amount_str.find('.');
        eosio_assert(dot_pos != string::npos, "Missing decimal fraction after decimal point");
        // Parse symbol
        uint8_t precision_digit = 0;
        if (dot_pos != string::npos) {
            precision_digit = amount_str.size() - dot_pos - 1;
        }

        symbol_type sym = parse_symbol2(precision_digit, symbol_str);

        // Parse amount
        safe<int64_t> int_part, fract_part;
        if (dot_pos != string::npos) {
            int_part = to_int<int64_t>(amount_str.substr(0, dot_pos));
            fract_part = to_int<int64_t>(amount_str.substr(dot_pos + 1));
            if (amount_str[0] == '-') fract_part *= -1;
        } else {
            int_part = to_int<int64_t>(amount_str);
        }

        safe<int64_t> amount = int_part;
        amount *= safe<int64_t>(get_precision(sym.precision()));
        amount += fract_part;

        return myasset(amount.value, sym);
    }

} /// namespace eosio