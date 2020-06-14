#include "order.hpp"
#include "parser.hpp"
#include "utils.hpp"

namespace dex {
    
// normal order with the following format
// pair_id:type:price:match_now:agent
// #1:1:0.4567 EOS:match:meetone#
bool parse_order_from_string(const string &memo, order& o, bool &match_now) {
    string m = memo;
    trim(m);

    do {
        if (m.length() < 12) {
            print("invalid memo length");
            break;
        }

        if (m.find('-') != string::npos) {
            print("invalid char");
            break;
        }

        auto first_sep = m[0];
        auto last_sep = m[m.length()-1];

        if (first_sep != '#' || last_sep != '#') {
            print("invalid memo separator");
            break;
        }

        std::vector<std::string> parts;
        split_memo(parts, m.substr(1, m.length()-2), ':');
        auto parts_size = parts.size();

        if (parts_size < 3) {
            print("invalid memo parts");
            break;
        }

        // parse required parameters
        uint16_t pair_id = to_int<uint16_t>(parts[0]);
        uint8_t type = to_int<uint8_t>(parts[1]);
        myasset price = parse_asset_from_string(parts[2]);

        // parse match flag, default to true, if user has no enough cpu
        // he can set this flag to zero, thus we can tick later to match
        match_now = true; // by default
        if (parts_size >= 4) {
            eosio_assert(parts[3] == "0" || parts[3] == "1", "use 0 or 1 to set match flag");
            uint8_t match_int = to_int<uint8_t>(parts[3]);
            if (match_int == 0) {
                match_now = false;
            }
        }
        // parse agent
        if (parts_size >= 5) {
            std::string agent = parts[4];
        }

        // check price
        if (is_limited_order(type)) {
            if (price.amount <= 0) {
                print("price should be positive, malicious action");
                break;
            }
            if (price.amount >= asset::max_amount) {
                print("price exceeds max amount, malicious action");
                break;
            } 
        } else {
            // market orders
            if (price.amount != 0) {
                print("market price should be zero, malicious action");
                break;
            }
        }  
        
        o.pair_id = pair_id;
        o.type = (order_type)type;
        o.price = price;

        return true;
    } while (0);
    
    return false;
}

} /// namespace dex