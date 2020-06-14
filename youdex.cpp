#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/crypto.h>

#include "youdex.hpp"
#include "parser.hpp"
#include "utils.hpp"
#include "accounts.hpp"
#include "vote.hpp"
#include "venus_types.hpp"
#include "exchange_accounts.cpp"
#include "exchange_state.cpp"
#include "order.cpp"
#include "match.cpp"
#include "settle.cpp"
#include "vote.cpp"

void youdex::on_transfer(const transfer_args& t, account_name code) {
    require_auth(t.from);
    bool malicious = true;
    do {
        // called direcly
        if (code == _self) {
            break;
        }
        // fake EOS
        if (t.quantity.symbol == S(4, EOS) && code != N(eosio.token)) {
            break;
        }

        malicious = false;
    } while (0);

    if (malicious) {
        print("Possible attack, add sender to blacklist!!!\n");
        require_recipient(_gstate.pixiu);
        return;
    }
    // Why are there so many attacking? it's time for us to do something
    if (is_blocked_account(t.from)) {
        // In order to support auto disable pair, we reject all such transfers
        eosio_assert(false, "You are blacklisted due to previous malicious attacks, stop that silly behaviour!!!");
        return;
    }

    if ( t.to != _self ) {
        eosio_assert(t.from == _self, "unexpected notification");
        return;
    }

    eosio_assert( t.from != _self, "cannot transfer from self contract");
	eosio_assert( t.to == _self, "receipt should be the contract");
	eosio_assert( t.quantity.is_valid(), "invalid quantity" );
    eosio_assert( t.quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( t.memo.size() <= 256, "memo has more than 256 bytes" ); 
    print("Received transfer from ", name{.value=code}, " with ", t, "\n");

    if (code == N(eosio.token)) {
        if (process_vote(t)) {
            return;
        }
    }

    auto aos = _orders.get_index<N(byslot)>();
    auto itr = aos.find(make_key_128(t.from, RESERVED));
    eosio_assert(itr != aos.end(), "err_no_idle_orders"); 
    
    uint64_t order_gid = 0;
    bool match_now = false;
    bool ask = false;
    bool abnormal_contract = false;

    aos.modify(itr, 0, [&](auto&o) {
        if (!parse_order_from_string(t.memo, o, match_now)) {
            eosio_assert(false, "err_invalid_memo"); 
        }
        
        ensure_status();
        // get target pair
        uint64_t pair_id = o.pair_id;
        auto itr_exch = _exchpairs.find(o.pair_id);
        eosio_assert(itr_exch != _exchpairs.end(), "err_pair_not_found");
        eosio_assert(itr_exch->status == ACTIVE, "err_pair_not_activated");
        auto pair = *itr_exch;
        // TODO: add to gray list if error
        // Check the token is from the exact contract 
        // Fill other fields
        ask = is_ask_order(o.type);
        // check if account from has open account for himself
        // we don't want to spend RAM to create account for users
        // however, this would be hard to accept
        if (0 == _gstate.r1) {
            accounts test_acnts(ask ? pair.base.contract : pair.quote.contract, t.from);
            auto to = test_acnts.find( ask ? pair.base.name() : pair.quote.name());
            // currently we only support two possible base tokens, EOS and RTC.
            // as for RTC, user cannot trade EOS for RTC without calling openaccount on 
            // RTC contract. 
            // In order to avoid much RAM usage, we increase the trading amount for more fee
            // to compensate RAM usage 
            if (to == test_acnts.end()) {
                if (ask) {
                    eosio_assert(false, "err_target_account_not_found"); // require account for target token
                } else {
                    eosio_assert(t.quantity >= asset(pair.base_precision, pair.base), "err_trade_too_small_for_new_account");
                }
            }
        }

        if (o.type == ASK_LIMITED || o.type == BID_LIMITED) {
            // do not allow unreasonable price
            auto max_price = asset(pair.base_precision*10, pair.base); 
            max_price = scale_base(max_price, pair.scale, o.price.symbol);
            eosio_assert(o.price <= max_price, "err_unreasonable_price");
        }
        
        // filter out minimu amount
        switch (o.type) {
            case ASK_LIMITED: {
                auto total_ask = t.quantity.amount * o.price;
                total_ask /= pair.quote_precision; 
                 
                auto min_ask = asset(pair.base_precision/10, pair.base);
                // use price precision to avoid zero amount 
                min_ask = scale_base(min_ask, pair.scale, o.price.symbol);
                eosio_assert(total_ask >= min_ask, "err_trade_too_small"); // trade should be larger than 0.1

                total_ask = to_base(total_ask, pair.scale, pair.base);
                eosio_assert(total_ask.amount > 0, "err_invalid_amount"); // total ask should be positive
                eosio_assert(check_fee(total_ask), "err_amount_too_small"); // total ask is not enough for fee
            }
            break;
            case ASK_MARKET: {
                auto runtime_itr = _exchruntimes.find(pair_id);
                eosio_assert(runtime_itr != _exchruntimes.end(), "err_runtime_not_found, should not happen");
                if (runtime_itr->lowest_price.amount > 0) {
                    // use 24H lowest price
                    auto total_ask = t.quantity.amount * runtime_itr->lowest_price;
                    total_ask /= pair.quote_precision; 
                 
                    auto min_ask = asset(pair.base_precision/10, pair.base);
                    // use price precision to avoid zero amount 
                    min_ask = scale_base(min_ask, pair.scale, o.price.symbol);
                    eosio_assert(total_ask >= min_ask, "err_trade_too_small"); // trade should be larger than 0.1

                    total_ask = to_base(total_ask, pair.scale, pair.base);
                    eosio_assert(total_ask.amount > 0, "err_invalid_amount"); // total ask should be positive
                    eosio_assert(check_fee(total_ask), "err_amount_too_small"); // total ask is not enough for fee
                } else {
                    // Possible small orders
                }
            }
            break;
            case BID_LIMITED: {
                // check if we can buy a minimum unit to avoid malicious highest price
                asset total_ask = 1 * o.price;
                total_ask /= pair.quote_precision;
                // use price precision to avoid zero amount 
                auto total_bid = scale_base(t.quantity, pair.scale, o.price.symbol);
                eosio_assert(total_bid >= total_ask, "err_unreasonable_price"); // should be reasonable price
            }
            // break;
            case BID_MARKET: {
                eosio_assert(t.quantity >= asset(pair.base_precision/10, pair.base), "err_trade_too_small");
                eosio_assert(check_fee(t.quantity), "err_amount_too_small"); // total bid is not enough for fee
            }
            break;
            default:
            eosio_assert(false, "err_invalid_order_type"); // invalid type, should not happed
            break;
        }

        asset quantity = t.quantity;
        auto symbol = append_order(_self, o.pair_id, ask, code, o.price.symbol, quantity);
        if (quantity != t.quantity) {
            abnormal_contract = true;
            eosio_assert(quantity.amount > 0, "should be postive quantity");
        }
        o.id = next_order_id();
        o.placed_time = now();
        o.expiration_time = (now() + FIVE_MINUTES); 
        o.initial = quantity;
        o.remain = quantity;

        o.deal = asset(0, symbol); 
        // save for outside usage
        order_gid = o.gid;
    });
     
    if (match_now) {
        do {
            if (ask) {
                // check status again to ensure still active
                // since if contract changes its rule, current quote
                // may be mismatch
                if (abnormal_contract) {
                    break;
                }
            }
            match(order_gid);
        } while (0);
    }
}

bool youdex::is_blocked_account(account_name from) {
    // Check whitelist account
    specialacnt_table whitelist( _gstate.pixiu, N(whitelist));
    auto itr = whitelist.find(from);
    if (itr != whitelist.end()) {
        print("Allow whitelist account ", name{.value=from});
        return false;
    }
    // Check blacklist
    specialacnt_table blacklist( _gstate.pixiu, N(blacklist));
    itr = blacklist.find(from);
    if (itr != blacklist.end()) {
        print("Allow whitelist account ", name{.value=from});
        return true;
    }

    return false;
}

// Open account before trading
void youdex::openaccount( account_name owner, uint64_t increment, account_name ram_payer ) {
    require_auth( ram_payer );
    eosio_assert( (increment > 0) && (increment <= 10), "err_invalid_increment");

    eosio_assert(is_account(owner), "err_invalid_account");
    eosio_assert(is_account(ram_payer), "err_invalid_ram_payer");
    // Calculate the orders belonging to this account
    const auto &aos = _orders.get_index<N(byowner)>();
    const auto &begin = aos.lower_bound(owner);
    const auto &end = aos.upper_bound(owner+1); 

    uint64_t count = 0;
    for (auto itr = begin; itr != end && itr != aos.end(); ++itr) {
        eosio_assert(itr->owner == owner, "err_unexpected_order_owner");
        ++count;
        print(" GID = ", itr->gid, ", expiration=", itr->expiration_time, ", owner=", name{.value=itr->owner}, "\n");
    }

    eosio_assert( (count+increment) <= 100, "err_exceed_max_reserved"); // for this account exceeds the max value (100)
    // Add empty orders
    for (auto i = 0; i < increment; i++) {
        _orders.emplace(ram_payer, [&](auto &o) {
            o.reset();
            o.gid = next_order_id();
            o.owner = owner;
        });
    }
}

uint64_t youdex::next_order_id() {
    eosio_assert(_gstate.order_count < max_order_id, "err_exceed_max_order");
    _gstate.order_count++;
    uint64_t next_id = _gstate.order_count; 
    _global.set( _gstate, _self );

    eosio_assert(next_id > 0 && next_id < max_order_id, "err_invalid_order_id");
    return next_id;
}

void youdex::createx(account_name manager, extended_symbol quote, extended_symbol base, uint64_t scale) {
    require_auth(_gstate.admin);

    eosio_assert(is_account(manager), "manager is not an valid account");
    eosio_assert(manager != quote.contract, "manager should not be the quote contract");
    eosio_assert(manager != base.contract, "manager should not be the base contract");
    eosio_assert(manager != _self, "manager should not be self");
    
    eosio_assert(quote.is_valid(), "quote symbol is invalid");
    eosio_assert(base.is_valid(), "base symbol is invalid");
    eosio_assert(is_account(quote.contract), "quote contract is invalid");
    eosio_assert(is_account(base.contract), "base contract is invalid");
    eosio_assert(quote != base, "quote should be different from base token");
    eosio_assert(scale > 0 && scale <= 100000000ll && (1 == scale || 0 == (scale % 10)), "invalid scale");

    // Deduplicate
    uint128_t key = make_key_128(quote.value, base.value);
    auto pairs = _exchpairs.get_index<N(bypair)>();

    const auto &begin = pairs.lower_bound(key);
    const auto &end = pairs.upper_bound(key+1); 

    for (auto itr = begin; itr != end && itr != pairs.end(); ++itr) {
        eosio_assert(!(itr->quote == quote && itr->base == base), "already has this pair");
    } 
    
    // Create the exchange pair
    _exchpairs.emplace(_self, [&](auto& ep) {
        eosio_assert(_gstate.pair_count < max_exch_pair_id, "exceed max_exch_pair_id");
        _gstate.pair_count++;
        _global.set(_gstate, _self);

        ep.id = _gstate.pair_count;
        ep.manager = manager;
        ep.quote = quote;
        ep.base = base;
        ep.quote_precision = get_precision(quote.precision());
        ep.base_precision = get_precision(base.precision());
        ep.scale = scale;
        ep.created_time = now();
        ep.lastmod_time = 0;
        ep.status = PENDING;
        // use default thresholds
        if (manager == _gstate.venus) {
            ep.vote_threshold = 1000; 
            ep.stake_threshold = 1000000ll*10000; // pay attention to the precision
            eosio_assert(0 == (ep.stake_threshold % vns::each_rtc_stake), "invalid stake threshold");
        } else {
            ep.vote_threshold = 0;
            ep.stake_threshold = 0;
        }
        ep.r1 = ep.r2 = ep.r3 = ep.r4 = 0;
    });

    // Take snapshot of quote balance
    auto current_quote = get_current_balance(_self, quote);
    // Create runtime record
    _exchruntimes.emplace(_self, [&](auto&er) {
        er.pair_id = _gstate.pair_count;
        er.ask_orders = 0;
        er.bid_orders = 0;
        er.vote_count = 0;

        er.latest_price = asset(0, get_price_symbol(base, scale));
        er.open_price = er.latest_price;
        er.close_price = er.latest_price;
        er.highest_price = er.latest_price;
        er.lowest_price = er.latest_price;

        er.total_quote = asset(0, quote);
        er.total_base = asset(0, base);
        er.lastmod_time = 0;

        er.current_quote = current_quote;
        er.r1 = er.r2 = 0;
    });

    print("Created exchange pair ", quote, "/", base, " with manager ", manager);
}

void youdex::setx(uint64_t pair_id, uint8_t status) {
    require_auth(_gstate.sudoer);

    auto itr = _exchpairs.find(pair_id);

    eosio_assert(itr != _exchpairs.end(), "exchange pair does not exist");
    eosio_assert(itr->status != status, "status should not be the same with current value");
    eosio_assert(is_valid_exchange_status(status), "invalid status");

    _exchpairs.modify(itr, 0, [&](auto& ep) {
        ep.status = status;
        ep.lastmod_time = now();
    });
    
    print_f("Set status of % to %", pair_id, (uint64_t)status);
}

void youdex::setmanager(uint64_t pair_id, account_name manager) {
    require_auth(_gstate.admin);

    eosio_assert(is_account(manager), "should be valid account");
    eosio_assert(manager != _self, "should not be inner account");
    eosio_assert(manager != _gstate.pixiu, "should not be inner account");
    eosio_assert(manager != _gstate.fee_account, "should not be inner account");
    eosio_assert(manager != _gstate.sudoer, "should not be inner account");
    eosio_assert(manager != _gstate.oper, "should not be inner account");
    eosio_assert(manager != _gstate.admin, "should not be inner account");

    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    auto itr = _exchpairs.find(pair_id);

    eosio_assert(itr != _exchpairs.end(), "exchange pair does not exist");
    eosio_assert(itr->manager != manager, "status should not be the same with current value");
    print_f("Set manager of pair % from % to %", pair_id, itr->manager, manager);

    _exchpairs.modify(itr, 0, [&](auto& ep) {
        ep.manager = manager;
        ep.lastmod_time = now();
    });
}

void youdex::recover(uint64_t pair_id) {
    require_auth(_gstate.sudoer);
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");

    auto itr = _exchpairs.find(pair_id);
    eosio_assert(itr != _exchpairs.end(), "exchange pair does not exist");

    auto runtime_itr = _exchruntimes.find(pair_id);
    eosio_assert(runtime_itr != _exchruntimes.end(), "err_runtime_not_found");

    _exchruntimes.modify(runtime_itr, 0, [&](auto& er) {
        auto current_quote = get_current_balance(_self, itr->quote); 
        print_f("update quote balance % ==> %\n", er.current_quote, current_quote);
        er.current_quote = current_quote;
    });

    print_f("updated current quote balance for pair %\n", pair_id);
}

void youdex::setthreshold(uint64_t pair_id, uint64_t vote, uint64_t stake) {
    require_auth(_self);

    auto itr = _exchpairs.find(pair_id);

    eosio_assert(itr != _exchpairs.end(), "exchange pair does not exist");
    eosio_assert(itr->status == PENDING, "status should be pending");
    eosio_assert(vote >= 0, "vote threshold should be positive");
    eosio_assert(stake >= 0, "stake threshold should be positive");
    eosio_assert(itr->manager == _gstate.venus, "manager should be venus");

    _exchpairs.modify(itr, 0, [&](auto& ep) {
        ep.stake_threshold = stake;
        ep.vote_threshold = vote;
    });
}

// Close account to exit trading, this operation will remove all the prepaied
// ram to the previous user
void youdex::closeaccount( account_name owner, uint64_t count ) {
    require_auth(owner);
    eosio_assert(count > 0 && count <= 10, "err_invalid_decrement"); // at most 10 orders for each operation
    
    auto aos = _orders.get_index<N(byslot)>();

    const auto &begin = aos.lower_bound(make_key_128(owner, RESERVED));
    const auto &end = aos.upper_bound(make_key_128(owner+1, RESERVED)); 

    uint64_t i = 0;
    // Close idle orders firstly, then active orders
    for (auto curitr = begin; curitr != end && curitr != aos.end() && i < count; ++i) {
        auto itr = curitr++;
        eosio_assert(itr->owner == owner, "err_auth_close"); 
        print(" GID = ", itr->gid, ", expiration=", itr->expiration_time, ", owner=", name{.value=itr->owner}, "\n");
        if (itr->type != RESERVED) {
            cancelorder(owner, itr->id);
        }
        
        aos.erase(itr);
    }

    print_f("removed % orders\n", i);
    eosio_assert(i > 0, "err_end_of_table");   
}

// Clean removes at most count elements from orders
void youdex::cleanx(uint64_t pair_id,  uint64_t count ) {
    auto from = _gstate.sudoer;
    require_auth(from);

    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    eosio_assert(count > 0 && count <= 10, "at most 10 orders for each operation");

    auto itr_exch = _exchpairs.find(pair_id);
    eosio_assert(itr_exch != _exchpairs.end(), "invalid exchange pair id");
    eosio_assert(itr_exch->status != ACTIVE, "exch pair should not be active");
    auto pair = *itr_exch;

    auto aos = _orders.get_index<N(byprice)>();
    auto low = aos.lower_bound(make_match_128(pair_id, ASK_MARKET, asset(0, pair.base)));
    auto high = aos.upper_bound(make_match_128(pair_id, BID_LIMITED+1, asset(0, pair.base)));
    
    uint64_t i = 0;
    for (auto curitr = low; curitr != high && i < count; ++i) {
        // Notice that orderitr will be changed in cancelorder
        auto itr = curitr++;
        eosio_assert(itr->type != RESERVED, "should not be reserved order");
        eosio_assert(itr->pair_id == pair_id, "pair id mismatch, should not happen");

        cancelorder(from, itr->id);
    }

    print_f("Erased % records from table\n", i);
    eosio_assert(i > 0, "end of table");
}

// clear all data and shutdown
void youdex::reset(uint8_t level) {
    require_auth(_self);

    do {
        // check system status
        eosio_assert(_gstate.status == 0, "should not be active status");
        // make sure all exchpairs are not active
        auto aep = _exchpairs.get_index<N(bystatus)>();
        uint64_t i = 0;
        for (auto curitr = aep.find(ACTIVE); curitr != aep.end() && i < 10; ++i) {
            auto itr = curitr++;
            aep.modify(itr, 0, [&](auto& ep) {
                ep.status = DEACTIVATED;
                ep.lastmod_time = now();
            });
        }

        if ((i > 0) || (level == 1)) break;
        // clean exchpairs one by one
        auto itr_exch = _exchpairs.begin();
        vote_table votes(_self, _self);
        if (clear_table(votes, 10) > 0) break;
        if (level == 2) break;
        // clean orders including reserved ones
        i = 0;
        for (auto itr = _orders.begin(); itr != _orders.end() && i < 10; ++i) {
            print(" GID = ", itr->gid, ", expiration=", itr->expiration_time, ", owner=", name{.value=itr->owner}, "\n");
            if (itr->type != RESERVED) {
                cancelorder(_self, itr->id);
            }
            
            itr = _orders.erase(itr); 
        }

        if ((i > 0) || (level == 3)) break;
        // remove notifications
        notify_table notifies(_self, _self);
        auto count = clear_table(notifies, 10);
        if ((count > 0) || (level == 4)) break;

        count = clear_table(_exchruntimes, 10);
        if ((count > 0) || (level == 5)) break;
        
        count = clear_table(_exchpairs, 10);
        if ((count > 0) || (level == 6)) break;
        
        _global.remove();
        eosio_exit(0); // to avoid create one in destructor!
    } while (0);
}

// reset vote to release ram
void youdex::resetvote(account_name from, uint64_t pair_id, uint64_t amount) {
    require_auth(from);
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    eosio_assert(from == _gstate.sudoer, "not authorized");
    eosio_assert(amount > 0 && amount <= 100, "cannot remove too many records at one time");
    
    vote_table votetable(_self, _self);
    auto votes = votetable.get_index<N(bypair)>();

    const auto &begin = votes.lower_bound(pair_id);
    const auto &end = votes.upper_bound(pair_id+1); 

    uint64_t i = 0;
    for (auto itr = begin; itr != end && itr != votes.end() && i < amount; ++i) {
        eosio_assert(itr->pair_id == pair_id, "cannot reset votes of other pair");
        
        itr = votes.erase(itr);
    }

    eosio_assert(i > 0, "end of table"); 
}

void youdex::setstatus(uint8_t status) {
    require_auth(_gstate.admin);

    eosio_assert(status == 0 || status == 1, "invalid status");
    eosio_assert(status != _gstate.status, "same status");

    _gstate.status = status;
    _global.set(_gstate, _self);
}

void youdex::ensure_status() {
    eosio_assert(_gstate.status == 1, "err_dex_not_active"); 
}

void youdex::setaccount(uint8_t role, account_name name) {
    require_auth(_self);

    eosio_assert(is_account(name), "not a invalid account");
    eosio_assert(role >= SUDOER && role < MAX_ROLE, "invalid role");

    switch (role) {
        case SUDOER: 
        eosio_assert(name != _gstate.sudoer, "account should be different");
        _gstate.sudoer = name;
        break;
        case ADMIN: 
        eosio_assert(name != _gstate.admin, "account should be different");
        _gstate.admin = name;
        break;
        case VENUS: 
        eosio_assert(name != _gstate.venus, "account should be different");
        _gstate.venus = name;
        break;
        case PIXIU: 
        eosio_assert(name != _gstate.pixiu, "account should be different");
        _gstate.pixiu = name;
        break;
        case FEE_ACCOUNT: 
        eosio_assert(name != _gstate.fee_account, "account should be different");
        _gstate.fee_account = name;
        break;
        case OPERATOR: 
        eosio_assert(name != _gstate.oper, "account should be different");
        _gstate.oper = name;
        break;
        default:
        eosio_assert(false, "invalid role");
        break;
    }

    _global.set(_gstate, _self);
}

void youdex::setparam(uint64_t key, uint64_t value) {
    require_auth(_gstate.admin);
    switch (key) {
        case 1:
        eosio_assert(value == 0 || value == 1, "invalid input");
        eosio_assert(_gstate.r1 != value, "should be different value");
        _gstate.r1 = value;
        break;
        default:
        eosio_assert(false, "invalid key");
        break;
    }

    _global.set(_gstate, _self);
}

void youdex::setmatchtime(uint64_t max_match_time) {
    require_auth(_gstate.admin);

    eosio_assert(max_match_time != _gstate.max_match_time, "should be different");
    eosio_assert(max_match_time >= default_min_transaction_cpu_usage, "should be higher than default_min_transaction_cpu_usage");
    eosio_assert(max_match_time <= default_max_transaction_cpu_usage, "should be lower than default_max_transaction_cpu_usage");

    _gstate.max_match_time = max_match_time;
    _global.set(_gstate, _self);
}

void youdex::setpairinfo(uint64_t pair_id, string website, string issue, string supply, string desc_cn, string desc_en, string desc_kr)
{
    require_auth(_gstate.sudoer);
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");

    auto itr_exch = _exchpairs.find(pair_id);
    eosio_assert(itr_exch != _exchpairs.end(), "invalid exchange pair id");
    _exchpairs.modify(itr_exch, 0, [&](auto& ep) {
        ep.txid = get_txid();
        ep.lastmod_time = now();
    });
}

void youdex::addnotify(string info_cn, string info_en, string info_kr)
{
    require_auth(_gstate.admin);

    notify_table notifies(_self, _self);

    uint64_t count = 0;
    for (auto itr = notifies.begin(); itr != notifies.end(); ++itr) {
        ++count;
    }
    eosio_assert(count < 10, "at most 10 notify entries");
    
    notifies.emplace(_self, [&](auto& n) {
        n.id = notifies.available_primary_key();
        n.txid = get_txid();
        n.created_time = now();
    });
}

void youdex::deletenotify(uint64_t id) 
{
    require_auth(_gstate.admin);

    notify_table notifies(_self, _self);
    auto itr = notifies.find(id);
    eosio_assert(itr != notifies.end(), "notify entry not found"); 

    notifies.erase(itr);
}

void youdex::updatenotify(uint64_t id, string info_cn, string info_en, string info_kr)
{
    require_auth(_gstate.admin);

    notify_table notifies(_self, _self);
    auto itr = notifies.find(id);
    eosio_assert(itr != notifies.end(), "notify entry not found"); 

    notifies.modify(itr, 0, [&](auto& n) {
        n.txid = get_txid();
    });
}

checksum256 youdex::get_txid()
{
    checksum256 h;
    auto size = transaction_size();
    char buf[size];
    uint32_t read = read_transaction( buf, size );
    eosio_assert( size == read, "read_transaction failed");
    sha256(buf, read, &h);

    printhex( &h, sizeof(h) );

    return h; 
}

void youdex::apply( account_name contract, account_name act ) {

    if( act == N(transfer) ) {
        on_transfer( unpack_action_data<transfer_args>(), contract );
        return;
    }

    if( contract != _self ) {
        // reject all the annoying advertisements
        eosio_assert(false, "unexpected contract");
        return;
    }
    // disable actions when system is not ready
    switch (act) {
        case N(openaccount):
        case N(createx):
        case N(setx):
        case N(setmanager):
        case N(push):
        case N(tick):
        case N(setpairinfo):
        case N(addnotify):
        case N(deletenotify):
        case N(updatenotify):
        ensure_status();
        break;
        default: break;
    }

    auto& thiscontract = *this;
    switch( act ) {
        EOSIO_API( youdex, (openaccount)(closeaccount)(cancelorder)\
        (cleanx)(createx)(setx)(setmanager)(recover)(reset)(resetvote)(setthreshold)(push)(tick)\
        (setstatus)(setaccount)(setparam)(setmatchtime)(setpairinfo)\
        (addnotify)(deletenotify)(updatenotify) )
        default:
            eosio_assert(false, "unexpected action");
            break;
    };
}

extern "C" {
    void apply( uint64_t receiver, uint64_t code, uint64_t action ) {
        youdex ex( receiver );
        ex.apply( code, action );
        /* does not allow destructor of thiscontract to run: eosio_exit(0); */
   }
}