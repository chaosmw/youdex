#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.hpp>
#include "utils.hpp"
#include "venus.hpp"
#include "exchange_state.cpp"
#include "exchange_types.hpp"
#include "parser.hpp"

using namespace eosio;
using namespace vns;
using namespace exch;

namespace vns
{

venus::venus(account_name s) : contract(s), _global(_self, _self),
                               _rtcmarket(_self, _self), _exchfee(_self, _self), 
                               _stakes(_self, _self), _accounts(_self, _self), _refunds(_self, _self)
{
    _gstate = _global.exists() ? _global.get() : get_default_parameters();
    auto itr = _rtcmarket.find(S(4, RTCCORE));
    if (itr == _rtcmarket.end()) {
        auto system_token_supply = 100000000000000ll; 
        if (system_token_supply > 0) {
            itr = _rtcmarket.emplace(_self, [&](auto &m) {
                m.supply.amount = 100000000000000ll;
                m.supply.symbol = S(4, RTCCORE);
                m.base.balance.amount = int64_t(_gstate.free_rtc());
                m.base.balance.symbol = RTC_SYMBOL;
                m.quote.balance.amount = system_token_supply / 1000; 
                m.quote.balance.symbol = CORE_SYMBOL;
            });
        }
    } 
}

venus::~venus()
{
    _global.set(_gstate, _self);
}

void venus::setrtc(uint64_t max_rtc_amount)
{
    require_auth(_self);

    eosio_assert(_gstate.max_rtc_amount < max_rtc_amount, "rtc may only be increased"); /// decreasing rtc might result market maker issues
    eosio_assert(max_rtc_amount < 10000000000000000ll, "rtc size is unrealistic"); 
    eosio_assert(max_rtc_amount > _gstate.total_rtc_reserved, "attempt to set max below reserved");

    auto delta = int64_t(max_rtc_amount) - int64_t(_gstate.max_rtc_amount);
    auto itr = _rtcmarket.find(S(4, RTCCORE));

    _rtcmarket.modify(itr, 0, [&](auto &m) {
        m.base.balance.amount += delta;
    });

    _gstate.max_rtc_amount = max_rtc_amount;
    _global.set(_gstate, _self);
}

global_state venus::get_default_parameters()
{
    global_state gs;

    gs.max_rtc_amount = 10000000000000ll;
    gs.total_rtc_reserved = 0; 
    gs.total_eos_stake = 0; 

    gs.sudoer = _self;
    gs.admin = _self;
    gs.fee_account = _self;
    gs.dex_account = _self;
    gs.pixiu = _self;
    gs.ticker = _self;

    gs.fee = 10;    // 10 percent
    gs.status = 0; // TODO: 0: NOT OPEN; 1: RUN; 2:PAUSE; 3:STOP
    gs.cantransfer = 0; // disable transfer
    gs.supply = 0; // total supply is zero at beginning
    gs.total_stakers = 0;
    gs.a1 = 0;
    gs.a2 = 0;
    gs.r1 = 0; 
    gs.r2 = 0; 

    return gs;
}

void venus::do_buyrtc(account_name from, asset quant)
{
    require_auth(from);

    eosio_assert(quant.is_valid(), "invalid quantity");
    eosio_assert(quant.amount > 0, "must purchase a positive amount");
    eosio_assert(quant.symbol == CORE_SYMBOL, "invalid symbol");

    auto fee = quant;
    fee.amount = (fee.amount + _gstate.fee - 1) / _gstate.fee; 
    
    auto quant_after_fee = quant;
    quant_after_fee.amount -= fee.amount;

    eosio_assert(fee.amount > 0, "err_fee_not_enough");
    // send fee to _fee_account
    do_transfer(N(eosio.token), _self, _gstate.fee_account, fee, std::string("rtc fee"));

    asset rtc_out;

    const auto &market = _rtcmarket.get(S(4, RTCCORE), "rtc market does not exist");
    _rtcmarket.modify(market, 0, [&](auto &es) {
        rtc_out = es.convert(quant_after_fee, RTC_SYMBOL);
    });

    eosio_assert(rtc_out.amount > 0, "err_invalid_amount");

    _gstate.total_rtc_reserved += uint64_t(rtc_out.amount); // RTC
    _gstate.total_eos_stake += quant_after_fee.amount;      // EOS

    eosio_assert(_gstate.total_rtc_reserved <= _gstate.supply, "err_exceed_supply");

    auto acnts = _accounts.get_index<N(byowner)>();
    auto acnt_itr = acnts.find(from);

    eosio_assert(acnt_itr != acnts.end(), "err_require_open_account");
   
    acnts.modify(acnt_itr, from, [&](auto &res) {
        res.total += rtc_out;
        res.liquid += rtc_out;
        eosio_assert(res.total == (res.liquid + res.staked), "err_balance_mismatch");
    });
}

void venus::do_transfer(account_name contract, account_name from, account_name to, const asset &quantity, string memo)
{
    // Notice that it requres authorize eosio.code firstly
    if (quantity.amount > 0) {
        action(
            permission_level{_self, N(active)},
            contract, N(transfer),
            std::make_tuple(from, to, quantity, memo))
            .send();
    } else {
        print("try to transfer 0 token with ", memo, "\n");
    }
}

void venus::sellrtc(account_name account, asset quantity)
{
    require_auth(account);

    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.symbol == RTC_SYMBOL, "should be rtc");
    eosio_assert(quantity.amount > 0, "cannot sell negative rtc");

    auto acnts = _accounts.get_index<N(byowner)>();
    auto acnt_itr = acnts.find(account);
    eosio_assert(acnt_itr != acnts.end(), "found no account");
    eosio_assert(acnt_itr->liquid >= quantity, "err_liquid_not_enough");

    asset tokens_out;
    auto itr = _rtcmarket.find(S(4, RTCCORE));
    _rtcmarket.modify(itr, 0, [&](auto &es) {
        /// the cast to int64_t of bytes is safe because we certify bytes is <= quota which is limited by prior purchases
        tokens_out = es.convert(quantity, CORE_SYMBOL);
    });

    // in order to allow close account, we allow minimus amount == 1
    // however, the fee may be zero, thus will abort in the following fee transfer
    // this is a delimpla, should be resolved before production
    eosio_assert(tokens_out.amount > 0, "err_amount_too_small"); // token amount received from selling rtc is too low
    _gstate.total_rtc_reserved -= static_cast<decltype(_gstate.total_rtc_reserved)>(quantity.amount); // bytes > 0 is asserted above
    _gstate.total_eos_stake -= tokens_out.amount;
    // this shouldn't happen, but just in case it does we should prevent it
    eosio_assert(_gstate.total_eos_stake >= 0, "err_overdrawn_staked"); // error, attempt to unstake more tokens than previously staked

    acnts.modify(acnt_itr, account, [&](auto &res) {
        res.total -= quantity;
        res.liquid -= quantity;
        eosio_assert(res.total == (res.liquid + res.staked), "balance mismatch");
    });

    auto fee = tokens_out;
    fee.amount = (fee.amount + 199) / 200; /// .5% fee (round up)
    // fee.amount cannot be 0 since that is only possible if quant.amount is 0 which is not allowed by the assert above.
    // If quant.amount == 1, then fee.amount == 1,
    // otherwise if quant.amount > 1, then 0 < fee.amount < quant.amount.
    auto quant_after_fee = tokens_out;
    quant_after_fee.amount -= fee.amount;
    // quant_after_fee.amount should be > 0 if quant.amount > 1.
    // If quant.amount == 1, then quant_after_fee.amount == 0 and the next inline transfer will fail causing the sellrtc action to fail.

    do_transfer(N(eosio.token), _self, account, quant_after_fee, std::string("sell rtc"));
    eosio_assert(fee.amount > 0, "err_invalid_fee"); // fee is zero, should not happed

    if (fee.amount > 0) {
        do_transfer(N(eosio.token), _self, _gstate.fee_account, fee, std::string("sell rtc fee"));
    } 
}

void venus::on_transfer(const transfer_args &t, account_name code)
{
    // To avoid forged transfer
    require_auth(t.from);
    print("Received transfer from ", name{.value = code}, " with ", t, "\n");

    bool malicious = true;
    do {
        // call directly
        if (code == _self) {
            print("call transfer with our contract direclty for rtc\n");
            if (transfer_rtc(t.from, t.to, t.quantity, t.memo)) {
                if (t.from == _gstate.dex_account & t.to == _self)
                    process_xxx_fee(_self, t.from, t.to, t.quantity, t.memo);
                malicious = false;
            }
            break;
        }

        if (code == N(eosio.token)) {
            if (on_eos_received(t.from, t.to, t.quantity, t.memo)) {
                malicious = false;
            }
            break;
        }

        if (t.from == _gstate.dex_account && t.to == _self) {
            process_xxx_fee(code, t.from, t.to, t.quantity, t.memo);
        }
        // ignore other airdrops
        malicious = false;

    } while (0);

    if (malicious) {
        print("Possible attack, add sender to blacklist!!!\n");
        require_recipient(_gstate.pixiu);
        return;
    }
}

void venus::process_xxx_fee(const account_name contract,
                            account_name from,
                            account_name to,
                            asset quantity,
                            string memo)
{
    require_auth(from);
    ensure_status();
    eosio_assert(from == _gstate.dex_account, "should be from dex account");
    eosio_assert(quantity.amount > 0, "quantity should be positive");
    eosio_assert(on_fee_received(contract, quantity, memo), "should not happen");
}

bool venus::on_eos_received(account_name from,
                            account_name to,
                            asset quantity,
                            string memo)
{
    require_auth(from);

    eosio_assert(from != to, "cannot transfer to self");
    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must transfer positive quantity");
    eosio_assert(quantity.symbol == S(4, EOS), "should be eos token only");
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    if (to != _self) {
        eosio_assert(from == _self, "unexpected notification");
        return true;
    }

    eosio_assert(to == _self, "can only send to us");
    if (from == _gstate.dex_account) {
        ensure_status();
        // parse the memo to record fee for each exchange pair
        return on_fee_received(N(eosio.token), quantity, memo);
    }

    if (memo == "buyrtc") {
        do_buyrtc(from, quantity);
        return true;
    }
    // do not keep the eos token
    eosio_assert(false, "err_invalid_memo");
    // TODO: check malicious behaviour and return false to add to blacklist
    return true;
}

// *fee:pair_id:ask_id:bid_id:price:volume*
// *fee:1:101:201:100:200* EOS
// *fee:2:103:333:300:400* RTC
// *fee:3:104:444:500:600* ABC
bool venus::on_fee_received(const account_name contract, const asset &quantity, const string &memo)
{
    // TODO: check malicious behaviour and return false to add to blacklist
    eosio_assert(quantity.amount > 0, "quantity should be positive");
    string m = memo;
    trim(m);
    ensure_status();

    do {
        if (m.length() < 14) {
            print("invalid memo length");
            break;
        }

        auto first_sep = m[0];
        auto last_sep = m[m.length() - 1];

        if (first_sep != '*' || last_sep != '*') {
            print("invalid memo separator");
            break;
        }

        std::vector<std::string> parts;
        split_memo(parts, m.substr(1, m.length() - 2), ':');

        if (parts.size() < 6) {
            print("invalid memo parts");
            break;
        }

        std::string title = parts[0];
        if (title != "fee") {
            print("wrong title");
            break;
        }

        uint64_t pair_id = to_int<uint64_t>(parts[1]);

        if (quantity.amount <= 0) {
            print("malicious action");
            break;
        }
        ensure_exch_fee(pair_id, true);
        auto itr = _exchfee.find(pair_id);
        eosio_assert(itr != _exchfee.end(), "exch fee not found, should not happen");
        eosio_assert(contract == itr->base.contract, "contract mismatch, malicious behaviour!");

        bool created = ensure_exch_bonus(pair_id, true, itr->total_periods + 1, itr->base);

        _exchfee.modify(itr, 0, [&](auto &ef) {
            ef.incoming += quantity; // symbol should match
            if (created)
                ef.total_periods++;
        });

        return true;
    } while (0);

    return false;
}

bool venus::transfer_rtc(account_name from,
                         account_name to,
                         asset quantity,
                         string memo)
{
    eosio_assert(from != to, "cannot transfer to self");
    require_auth(from);
    eosio_assert(is_account(to), "to account does not exist");
    require_recipient(from);
    require_recipient(to);

    eosio_assert(_gstate.cantransfer == 1, "err_transfer_not_open");

    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount > 0, "must transfer positive quantity");
    eosio_assert(quantity.symbol == S(4, RTC), "should be rtc token");
    eosio_assert(memo.size() <= 256, "memo has more than 256 bytes");

    auto payer = has_auth(to) ? to : from; 

    sub_balance(from, quantity);
    add_balance(to, quantity, payer);

    // TODO: check malicious behaviour and return false to add to blacklist
    return true;
}

void venus::sub_balance(account_name owner, asset value)
{
    accounts from_acnts(_self, owner);
    eosio_assert(value.symbol == RTC_SYMBOL, "should be rtc");

    const auto &from = from_acnts.get(owner, "no account object found");
    eosio_assert(from.liquid.amount >= value.amount, "overdrawn liquid balance");

    from_acnts.modify(from, owner, [&](auto &a) {
        a.liquid -= value;
        a.total -= value;
        eosio_assert(a.total == (a.liquid + a.staked + a.refund), "balance mismatch");
    });
}

void venus::add_balance(account_name owner, asset value, account_name ram_payer)
{
    eosio_assert(value.symbol == RTC_SYMBOL, "should be rtc");
    accounts to_acnts(_self, owner);
    auto to = to_acnts.find(owner);
    if (to == to_acnts.end()) {
        to_acnts.emplace(ram_payer, [&](auto &a) {
            a.owner = owner;
            a.total = value;
            a.liquid = value;
            a.staked = asset(0, RTC_SYMBOL);
            a.refund = asset(0, RTC_SYMBOL);
        });
    } else {
        to_acnts.modify(to, 0, [&](auto &a) {
            a.liquid += value;
            a.total += value;
            eosio_assert(a.total == (a.liquid + a.staked + a.refund), "balance mismatch");
        });
    }
}

void venus::trigger_trading()
{
    require_recipient(_gstate.dex_account);
}

void venus::openaccount(account_name owner, account_name ram_payer)
{
    require_auth(ram_payer);
    ensure_status();
    eosio_assert(is_account(owner), "err_invalid_account"); 
    eosio_assert(is_account(ram_payer), "err_invalid_ram_payer");

    auto acnts = _accounts.get_index<N(byowner)>();
    auto it = acnts.find(owner);
    eosio_assert(it == acnts.end(), "err_already_open_account"); 

    _accounts.emplace(ram_payer, [&](auto &a) {
        a.id = _accounts.available_primary_key();
        a.owner = owner;
        a.total = asset(0, RTC_SYMBOL);
        a.liquid = asset(0, RTC_SYMBOL);
        a.staked = asset(0, RTC_SYMBOL);
        a.refund = asset(0, RTC_SYMBOL);
    });
}

void venus::closeaccount(account_name owner)
{
    require_auth(owner); 

    auto acnts = _accounts.get_index<N(byowner)>();
    auto it = acnts.find(owner);

    eosio_assert(it != acnts.end(), "err_account_not_found"); // Balance row already deleted or never existed. Action won't have any effect.
    eosio_assert(it->total.amount == 0, "err_total_not_zero"); // Cannot close because the total balance is not zero.
    eosio_assert(it->liquid.amount == 0, "err_liquid_not_zero"); // Cannot close because the liquid balance is not zero.
    eosio_assert(it->staked.amount == 0, "err_staked_not_zero"); // Cannot close because the staked balance is not zero.
    eosio_assert(it->refund.amount == 0, "err_refund_not_zero"); // Cannot close because the refund balance is not zero.

    acnts.erase(it);
}

// we need to close account by hand and call reset
void venus::reset()
{
    require_auth(_self);
    // user resource has scope for different users
    // so, it's not possible for remove them directly
    // we assume that accounts are all closed and no outgoing refund request
    eosio_assert(_gstate.total_rtc_reserved == 0, "should not have outstanding RTC");
    uint64_t count = 0;
    do {
        // clear stakes firstly
        eosio_assert(_stakes.begin() == _stakes.end(), "stake table should be empty");

        exchange_pair_table exchpairs(_gstate.dex_account, _gstate.dex_account);
        for (auto itr_exch = exchpairs.begin(); itr_exch != exchpairs.end(); ++itr_exch) {
            auto pair_id = itr_exch->id;
            // clear exchbonus
            exchbonus_table exchbonus(_self, pair_id);
            count = clear_table(exchbonus, 10);
            if (count > 0) break;
        }

        if (count > 0) break;
        // clear exchfee
        count = clear_table(_exchfee, 10);
        if (count > 0) break;
        // reset rtcmarket
        auto itr = _rtcmarket.find(S(4, RTCCORE));
        _rtcmarket.erase(itr);
        // reset global state
        _global.remove();
        eosio_exit(0); // to skip destructor
    } while (0);
}

void venus::delegate(account_name from, asset quantity, uint64_t pair_id)
{
    require_auth(from);
    ensure_status();
    // check parameters
    eosio_assert(is_account(from), "err_invalid_account"); // not an account
    eosio_assert(quantity.is_valid(), "err_invalid_quantity"); // invalid quantity
    eosio_assert(quantity.amount > 0, "err_invalid_amount"); // must be a positive amount
    eosio_assert(quantity.symbol == RTC_SYMBOL, "invalid symbol");
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "err_invalid_pair_id"); // pair_id is out of range
    eosio_assert(0 == (quantity.amount % each_rtc_stake), "err_not_multiply_of_1000"); // should be multiple of 1000.0000

    // check if exch_fee is ready
    ensure_exch_fee(pair_id, true);
    auto itr = _exchfee.find(pair_id);
    eosio_assert(itr != _exchfee.end(), "exch fee not found, should not happen");
    bool created = ensure_exch_bonus(pair_id, true, itr->total_periods + 1, itr->base);
    if (created) {
        _exchfee.modify(itr, 0, [&](auto &ef) {
            ef.total_periods++;
        });
    }
    // check if liquid asset is enough
    auto acnts = _accounts.get_index<N(byowner)>();
    auto acnt_itr = acnts.find(from);
    eosio_assert(acnt_itr != acnts.end(), "err_require_open_account"); // account not found, should call open to get account
    eosio_assert(acnt_itr->liquid >= quantity, "err_liquid_not_enough");
    // substract liquid
    acnts.modify(acnt_itr, 0, [&](auto &a) {
        a.liquid -= quantity;
        a.staked += quantity;
        eosio_assert(a.liquid.amount >= 0, "liquid underflow"); // sanity checking
    });
    // add or append stake entry
    append_stake(from, quantity, pair_id);
}

void venus::undelegate(account_name from, asset quantity, uint64_t pair_id)
{
    require_auth(from);
    ensure_status();
    // check parameters
    eosio_assert(is_account(from), "err_invalid_account");
    eosio_assert(quantity.is_valid(), "err_invalid_quantity");
    eosio_assert(quantity.amount > 0, "err_invalid_amount"); // must be a positive amount
    eosio_assert(quantity.symbol == RTC_SYMBOL, "invalid symbol");
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "err_invalid_pair_id");
    eosio_assert(0 == (quantity.amount % each_rtc_stake), "err_not_multiply_of_1000"); // should be multiple of 1000.0000
    // check if exch_fee is ready
    ensure_exch_fee(pair_id, false);
    // check if staked asset is enough
    auto acnts = _accounts.get_index<N(byowner)>();
    auto acnt_itr = acnts.find(from);
    eosio_assert(acnt_itr != acnts.end(), "err_require_open_account"); // account not found, should call open to get account
    eosio_assert(acnt_itr->staked >= quantity, "err_staked_not_enough"); // staked asset is not enough
    // revoke stake entry
    revoke_stake(from, quantity, pair_id);
    // substract staked
    acnts.modify(acnt_itr, 0, [&](auto &a) {
        // delay 3 days, thus total = liquid, staked, refund
        // a.liquid += quantity; // delay 3 days
        a.staked -= quantity;
        a.refund += quantity;
        eosio_assert(a.staked.amount >= 0, "staked underflow"); // sanity checking
    });
    // create or update refund table
    // refunds_table refunds_tbl(_self, from);
    auto refunds_tbl = _refunds.get_index<N(byowner)>();
    auto req_itr = refunds_tbl.find(from);
    if (req_itr != refunds_tbl.end()) {
        refunds_tbl.modify(req_itr, 0, [&](auto &req) {
            // update time and balance
            req.pending += quantity;
            req.request_time = now();
        });
    } else {
        _refunds.emplace(from, [&](auto &req) {
            req.id = _refunds.available_primary_key();
            req.owner = from;
            req.pending = quantity;
            req.request_time = now();
        });
    }

    eosio::transaction out;
    out.actions.emplace_back(permission_level{from, N(active)}, _self, N(refund), from);
    out.delay_sec = refund_delay;
    cancel_deferred(from); // TODO: Remove this line when replacing deferred trxs is fixed
    out.send(from, from, true);
}

void venus::claimbonus(account_name from, uint64_t pair_id)
{
    require_auth(from);
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "err_invalid_pair_id");

    ensure_status();
    // make sure exchpair is NOT pending
    exchange_pair_table exchpairs(_gstate.dex_account, _gstate.dex_account);
    auto itr_exch = exchpairs.find(pair_id);
    eosio_assert(itr_exch != exchpairs.end(), "err_pair_not_found"); // exch pair does not exist
    eosio_assert(itr_exch->status != exch::PENDING, "err_pair_not_activated"); // exch pair is not activated yet
    // we use the cpu power to move pending to effect if condition matches
    snapshot_stake(pair_id);

    auto uniquestakes = _stakes.get_index<N(byuniqueid)>();
    auto itr = uniquestakes.find(make_key_128(pair_id, from));

    if (itr != uniquestakes.end()) {
        uint32_t threshold_time = 0;
        uint64_t bonus_period = 0;

        get_latest_bonus_params(pair_id, threshold_time, bonus_period);
        if (itr->bonus_period < bonus_period) {
            exchbonus_table exchbonus(_self, pair_id);
            auto bonus_itr = exchbonus.find(itr->bonus_period);
            eosio_assert(bonus_itr != exchbonus.end(), "bonus not found, should not happed");
            eosio_assert(bonus_itr->status != vns::PENDING, "should not be pending");
            auto double_bonus = 1.0 * itr->effect.amount * bonus_itr->factor;
            eosio_assert(double_bonus >= 0, "should not be negative value");
            auto quantity = asset(int64_t(double_bonus), bonus_itr->remain.symbol);

            eosio_assert(quantity.amount >= 0, "bonus should not be negative");
            eosio_assert(bonus_itr->remain >= quantity, "remain bonus is not enough, should not happen");

            uint64_t current_period = itr->bonus_period;
            uniquestakes.modify(itr, 0, [&](auto &s) {
                // indicate that we have claim the bonus
                s.bonus_period += 1;
                s.bonus_sum += quantity; 
                eosio_assert(s.bonus_sum.amount >= 0, "should not be negative");
                eosio_assert(s.bonus_period <= bonus_period, "bonus beyond current period, should not happen");
            });

            if (quantity.amount > 0) {
                // settle
                exchbonus.modify(bonus_itr, 0, [&](auto &eb) {
                    eosio_assert(eb.remain >= quantity, "sanity checking");
                    eb.remain -= quantity;
                    if (eb.remain.amount == 0) {
                        eb.status = DONE;
                        eb.end_time = now();
                    }
                });
                auto exch_itr = _exchfee.find(pair_id);
                eosio_assert(exch_itr != _exchfee.end(), "exch fee not found, should not happen");
                auto base = exch_itr->base;
                string memo;
                get_bonus_info(memo, current_period);
                do_transfer(base.contract, _self, from, quantity, memo); 
            }
        }
    }
}

// format: *bonus:peroid_id*
void venus::get_bonus_info(string &memo, uint64_t period)
{
    char buf[32]; 
    char *q = buf;
    
    *q++ = '*';
    *q++ = 'b';
    *q++ = 'o';
    *q++ = 'n';
    *q++ = 'u';
    *q++ = 's';
    *q++ = ':';
    q += print_ll(q, period, 20);
    *q++ = '*';
    *q++ = '\0';

    memo = string(buf);
}

void venus::snapshot_stake(uint64_t pair_id)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");

    uint32_t threshold_time = 0;
    uint64_t bonus_period = 0;

    get_latest_bonus_params(pair_id, threshold_time, bonus_period);
    if ((threshold_time + min_stake_time) > now()) {
        print("cannot make snapshot before end of bonus period");
        return;
    }

    auto ps = _stakes.get_index<N(bypending)>();
    // TODO: double check low and high bounds
    // especially include or exclude threshold_time
    const auto &low = ps.lower_bound(make_pending_id(pair_id, 1, 0));
    const auto &high = ps.upper_bound(make_pending_id(pair_id, 1, threshold_time));

    bool updated = false;
    uint64_t deadline = current_time() + default_max_transaction_cpu_usage * 0.5;

    for (auto itr = low; itr != high && itr != ps.end(); ) {
        eosio_assert(itr->pair_id == pair_id, "mismatch pair id, should not happen");
        if (current_time() >= deadline) {
            print("time is up, exit asap to avoid timeout error");
            updated = true; // safe
            break;
        }
        auto current = itr++;
        if (current->pending.amount > 0) {
            ps.modify(current, 0, [&](auto &s) {
                auto quantity = s.pending;
                s.effect += quantity;
                s.pending = asset(0, RTC_SYMBOL);
                // update quantity
                update_stake(pair_id, asset(0, RTC_SYMBOL), quantity);
            });

            updated = true;
        }
    }

    // if we have processed all the pending stake, just delay switch to next round
    if (!updated) {
        // PENDING --> INPROGRESS
        exchbonus_table exchbonus(_self, pair_id);
        auto temp_itr = exchbonus.rbegin();
        eosio_assert(temp_itr != exchbonus.rend(), "pair not found");
        auto bonus_itr = exchbonus.find(temp_itr->period);
        eosio_assert(bonus_itr->status == PENDING, "should be pending");
        // calculate bonus
        auto exch_itr = _exchfee.find(pair_id);
        eosio_assert(exch_itr != _exchfee.end(), "exchfee not found");
        eosio_assert(exch_itr->effect.amount >= 0, "effect stake should be positive");

        eosio_assert(bonus_itr->period == exch_itr->total_periods, "mismatch period");

        uint64_t latest_id = exch_itr->total_periods;
        auto bonus_symbol = exch_itr->base;
        auto avail = asset(0, bonus_symbol);

        _exchfee.modify(exch_itr, 0, [&](auto &ef) {
            avail = exch_itr->incoming / 2;
            ef.incoming -= avail;
            ef.total_periods++;
        });

        // assign for releasing
        exchbonus.modify(bonus_itr, 0, [&](auto &eb) {
            eb.total = avail;
            eb.remain = avail;
            if (exch_itr->effect.amount > 0) {
                eb.factor = 1.0 * avail.amount / exch_itr->effect.amount; 
            } else {
                eb.factor = 0; 
            }
            eb.status = INPROGRESS;
        });

        exchbonus.emplace(_self, [&](auto &eb) {
            eb.period = latest_id + 1;
            eb.total = asset(0, bonus_symbol);
            eb.remain = asset(0, bonus_symbol);
            eb.factor = 0;
            eb.threshold_time = now() + effect_stake_window; // threshold time is 7 days while bonus period is 30 days
            eb.end_time = 0;
            eb.status = PENDING;
        });
    }
}

// in case of inactivity, we can release by ourself
void venus::sudorelease(account_name from, uint64_t pair_id)
{
    require_auth(from);
    ensure_status();
    eosio_assert(from == _gstate.sudoer, "should be the sudo account");
}

void venus::refund(account_name owner)
{
    require_auth(owner);
    ensure_status();

    auto acnts = _accounts.get_index<N(byowner)>();
    auto acnt_itr = acnts.find(owner);

    eosio_assert(acnt_itr != acnts.end(), "err_require_open_account"); // failed to find the account, should open account firstly
    eosio_assert(acnt_itr->refund.amount > 0, "err_invalid_quantity");

    auto refunds_tbl = _refunds.get_index<N(byowner)>();
    auto req = refunds_tbl.find(owner);
    eosio_assert(req != refunds_tbl.end(), "err_refund_not_found");
    eosio_assert(req->request_time + refund_delay <= now(), "err_refund_not_available"); // refund is not available yet
    // Until now() becomes NOW, the fact that now() is the timestamp of the previous block could in theory
    // allow people to get their tokens earlier than the 3 day delay if the unstake happened immediately after many
    // consecutive missed blocks.

    eosio_assert(req->pending == acnt_itr->refund, "refund mismatch, should not happen");

    acnts.modify(acnt_itr, 0, [&](auto &a) {
        a.liquid += a.refund;
        a.refund -= req->pending;
        eosio_assert(a.refund.amount == 0, "refund mismatch, should not happen");
        eosio_assert(a.total == (a.liquid + a.staked + a.refund), "balance mismatch");
    });

    refunds_tbl.erase(req);
}

void venus::setstatus(uint8_t status)
{
    require_auth(_gstate.admin);

    eosio_assert(status == 0 || status == 1, "invalid status");
    eosio_assert(status != _gstate.status, "same status");

    _gstate.status = status;
    _global.set(_gstate, _self);
}

void venus::ensure_status()
{
    eosio_assert(_gstate.status == 1, "err_venus_not_active");
}

void venus::setaccount(uint8_t role, account_name name)
{
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
    case FEE_ACCOUNT:
        eosio_assert(name != _gstate.fee_account, "account should be different");
        _gstate.fee_account = name;
        break;
    case DEX_ACCOUNT:
        eosio_assert(name != _gstate.dex_account, "account should be different");
        _gstate.dex_account = name;
        break;
    case PIXIU:
        eosio_assert(name != _gstate.pixiu, "account should be different");
        _gstate.pixiu = name;
        break;
    case TICKER:
        eosio_assert(name != _gstate.ticker, "account should be different");
        _gstate.ticker = name;
        break;
    default:
        eosio_assert(false, "invalid role");
        break;
    }

    _global.set(_gstate, _self);
}

void venus::settransfer(uint8_t status) {
    require_auth(_self);

    eosio_assert(status == 0 || status == 1, "invalid status");
    eosio_assert(_gstate.cantransfer != status, "status should be different");
    _gstate.cantransfer = status;
    _global.set(_gstate, _self); 
}

void venus::setsupply(uint64_t supply) {
    require_auth(_self);

    eosio_assert(supply > _gstate.supply, "should be greater than current supply");
    eosio_assert(supply > _gstate.total_rtc_reserved, "should be greater than current reserved value");
    eosio_assert(supply <= _gstate.max_rtc_amount, "should be less than max supply");
    eosio_assert(supply < 10000000000000000ll, "rtc supply is unrealistic");

    _gstate.supply = supply;
    _global.set(_gstate, _self); 
}

void venus::tick(string info)
{
    require_auth(_gstate.ticker);
    ensure_status();
    eosio_assert( info.size() <= 256, "info has more than 256 bytes" );
}

void venus::append_stake(account_name owner, asset quantity, uint64_t pair_id)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    auto uniquestakes = _stakes.get_index<N(byuniqueid)>();
    
    uint32_t threshold_time = 0;
    uint64_t bonus_period = 0;

    get_latest_bonus_params(pair_id, threshold_time, bonus_period);
    auto current = now();

    auto itr = uniquestakes.find(make_key_128(pair_id, owner));

    if (itr == uniquestakes.end()) {
        _stakes.emplace(owner, [&](auto &s) {
            s.id = _stakes.available_primary_key();
            s.pair_id = pair_id;
            s.owner = owner;

            if (current <= threshold_time) {
                s.effect = quantity;
                s.pending = asset(0, RTC_SYMBOL);
            } else {
                s.effect = asset(0, RTC_SYMBOL);
                s.pending = quantity;
            }
            // update quantity
            update_stake(pair_id, quantity, s.effect);
            auto exch_itr = _exchfee.find(pair_id);
            eosio_assert(exch_itr != _exchfee.end(), "exch fee not found, should not happen");

            s.bonus_period = bonus_period;
            s.bonus_sum = asset(0, exch_itr->base);
            s.status = 0; 
            s.last_stake_time = current;
        });
        // update total_stakes
        ++_gstate.total_stakers;
        _global.set(_gstate, _self); 
    } else {
        // force user to claim bonus here, so the factor will not be affected
        eosio_assert(itr->bonus_period == bonus_period, "err_require_claim_bonus"); // you have pending bonus, please claim bonus and try again
        uniquestakes.modify(itr, 0, [&](auto &s) {
            auto new_effect = asset(0, RTC_SYMBOL);
            auto total = quantity;

            if (s.last_stake_time <= threshold_time) {
                new_effect = s.pending;
                s.pending = asset(0, RTC_SYMBOL);
            }

            if (current < threshold_time) {
                new_effect += quantity;
            } else {
                s.pending += quantity;
            }

            if (new_effect.amount > 0) {
                s.effect += new_effect;
            }

            // update quantity
            update_stake(pair_id, quantity, new_effect);

            s.last_stake_time = current;
        });
    }
}

void venus::revoke_stake(account_name owner, asset quantity, uint64_t pair_id)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");

    uint32_t threshold_time = 0;
    uint64_t bonus_period = 0;

    get_latest_bonus_params(pair_id, threshold_time, bonus_period);

    auto uniquestakes = _stakes.get_index<N(byuniqueid)>();
    auto itr = uniquestakes.find(make_key_128(pair_id, owner));

    eosio_assert(itr != uniquestakes.end(), "stake not found");

    // force user to claim bonus here, so the factor will not be affected
    bool erase = false;
    eosio_assert(itr->bonus_period == bonus_period, "err_require_claim_bonus"); // you have pending bonus, please claim bonus and try again
    uniquestakes.modify(itr, 0, [&](auto &s) {
        if (s.pending >= quantity) {
            s.pending -= quantity;
            update_stake(pair_id, -quantity, -asset(0, RTC_SYMBOL));
        } else {
            auto total = s.pending;
            auto remain = quantity - s.pending;
            s.pending = asset(0, RTC_SYMBOL);
            eosio_assert(remain.amount > 0, "remain should be positive");
            s.effect -= remain;
            eosio_assert(s.effect.amount >= 0, "effect amount should not be negative");
            // update  quantity
            update_stake(pair_id, -quantity, -remain);
        }
        if (s.effect.amount == 0 && s.pending.amount == 0) {
            erase = true;
        }
    });

    if (erase) {
        uniquestakes.erase(itr);
    }
}

void venus::update_stake(uint64_t pair_id, asset total, asset effect)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    auto itr = _exchfee.find(pair_id);
    eosio_assert(itr != _exchfee.end(), "exchfee not found");

    _exchfee.modify(itr, 0, [&](auto &ef) {
        ef.total += total; // quantity should be negative for unstake
        ef.effect += effect;
        eosio_assert(ef.total.amount >= 0, "total staked asset should be not negative");
        eosio_assert(ef.effect.amount >= 0, "total effect asset should be not negative");
        // only allow limited stakes
        if (total.amount > 0) {
            // read table from youdex contract
            exchange_pair_table exchpairs(_gstate.dex_account, _gstate.dex_account);
            auto itr_exch = exchpairs.find(pair_id);
            eosio_assert(itr_exch != exchpairs.end(), "exch pair does not exist");
            eosio_assert(ef.total.amount <= itr_exch->stake_threshold, "err_exceed_stake_threshold"); // reached the staking limit
        }
    });
}

void venus::get_latest_bonus_params(uint64_t pair_id, uint32_t &threshold_time, uint64_t &period)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    exchbonus_table exchbonus(_self, pair_id);
    auto itr = exchbonus.rbegin();
    eosio_assert(itr != exchbonus.rend(), "pair not found");
    eosio_assert(itr->status == PENDING, "should be pending state");

    threshold_time = itr->threshold_time;

    period = itr->period;
}

void venus::ensure_exch_fee(uint64_t pair_id, bool auto_create)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    auto itr = _exchfee.find(pair_id);
    if (itr == _exchfee.end()) {
        eosio_assert(auto_create, "exch fee not found");
        // TODO: check youdex to make sure exchange pair does exists
        extended_symbol symbol;
        should_exist_exch_pair(pair_id, symbol);
        // if we bill the user, the ram will not be released even the user close the account
        // so we pay the ram, thus we must add enough ram for storing this table
        _exchfee.emplace(_self, [&](auto &ef) {
            ef.pair_id = pair_id;
            ef.incoming = asset(0, symbol);   // EOS/RTC or other base token
            ef.total = asset(0, RTC_SYMBOL);  // RTC
            ef.effect = asset(0, RTC_SYMBOL); // RTC
            ef.total_periods = 0;
            ef.base = symbol;
        });
    }
}

bool venus::ensure_exch_bonus(uint64_t pair_id, bool auto_create, uint64_t period, const extended_symbol &bonus_symbol)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    eosio_assert(period > 0, "period should be positive");

    exchbonus_table exchbonus(_self, pair_id);
    auto itr = exchbonus.begin();
    if (itr == exchbonus.end()) {
        eosio_assert(auto_create, "exch bonus not found");
        // create the first one
        exchbonus.emplace(_self, [&](auto &eb) {
            eb.period = period;
            eb.total = asset(0, bonus_symbol);
            eb.remain = asset(0, bonus_symbol);
            eb.factor = 0;
            eb.threshold_time = now() + effect_stake_window; // threshold time is 7 days while bonus period is 30 days
            eb.end_time = 0;
            eb.status = PENDING;
        });

        return true;
    }

    return false;
}

void venus::should_exist_exch_pair(uint64_t pair_id, extended_symbol &symbol)
{
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "pair_id is out of range");
    // read table from youdex contract
    exchange_pair_table exchpairs(_gstate.dex_account, _gstate.dex_account);
    auto itr = exchpairs.find(pair_id);
    eosio_assert(itr != exchpairs.end(), "exch pair does not exist");
    symbol = itr->base;
}
// Entrance of contract
void venus::apply(account_name contract, account_name act)
{
    if (act == N(transfer)) {
        on_transfer(unpack_action_data<transfer_args>(), contract);
        return;
    }

    if( contract != _self ) {
        // reject all the annoying advertisements
        eosio_assert(false, "unexpected contract");
        return;
    }

    auto &thiscontract = *this;
    switch (act) {
        EOSIO_API(venus, (sellrtc)(setrtc)(openaccount)(closeaccount)(reset)
        (delegate)(undelegate)(claimbonus)(sudorelease)(refund)(setstatus)(setaccount)
        (settransfer)(setsupply)(tick))
        default:
            eosio_assert(false, "unexpected action");
            break;
    };

    // currently, global state relies on destructor to save intermediate results
    // so do not call eosio_exit here
}

extern "C"
{
    void apply(uint64_t receiver, uint64_t code, uint64_t action) {
        venus vs(receiver);
        vs.apply(code, action);
        /* does not allow destructor of thiscontract to run: eosio_exit(0); */
    }
}

} // namespace vns