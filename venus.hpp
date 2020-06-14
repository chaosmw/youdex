#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/singleton.hpp>

#include "transfer.hpp"
#include "exchange_state.hpp"
#include "venus_types.hpp"

using namespace eosio;
using namespace exchange;
using namespace std;

// #define ENV_PRODUCT 1
namespace vns
{

// (_self, _self)
struct global_state
{
    uint64_t free_rtc() const { return max_rtc_amount - total_rtc_reserved; }

    uint64_t max_rtc_amount = 10000000000000ll;
    uint64_t total_rtc_reserved = 0; // RTC
    int64_t total_eos_stake = 0;     // EOS

    // 3 levels adminstration(_self > admin > sudoer)
    account_name sudoer; // operator
    account_name admin;  // administrator

    account_name fee_account; // buy/sell rtc fee
    account_name dex_account; // such as youdex
    account_name pixiu;
    account_name ticker; // ticker

    uint64_t fee;
    uint8_t status; // switch on/off
    uint8_t cantransfer; // transfer flag
    uint64_t supply; // RTC supply
    uint64_t total_stakers; // RTC stakers

    // for future usage
    account_name a1; 
    account_name a2;
    uint64_t r1; 
    uint64_t r2; 

    EOSLIB_SERIALIZE(global_state, (max_rtc_amount)(total_rtc_reserved)(total_eos_stake)
        (sudoer)(admin)(fee_account)(dex_account)(pixiu)(ticker)
        (fee)(status)(cantransfer)(supply)(total_stakers)
        (a1)(a2)(r1)(r2))
};

typedef eosio::singleton<N(global), global_state> global_state_singleton;

#ifdef ENV_PRODUCT
static constexpr uint32_t seconds_per_day = 24 * 3600;
#else
static constexpr uint32_t seconds_per_day = 10;
#endif
static constexpr time refund_delay = 3 * seconds_per_day;
static constexpr time effect_stake_window = 7 * seconds_per_day;
static constexpr time min_stake_time = 23 * seconds_per_day;

static constexpr uint64_t system_token_symbol = CORE_SYMBOL;

// (_self, _self)
struct account
{
    uint64_t id; // auto increment
    account_name owner;
    asset total;
    asset liquid;
    asset staked;
    asset refund;

    uint64_t primary_key() const { return id; }
    uint64_t get_owner() const { return owner; }
    uint64_t get_total() const { return static_cast<uint64_t>(-total.amount);} // the order of 0 is not changed, so need to exclude zero 
    uint64_t get_liquid() const { return static_cast<uint64_t>(-liquid.amount);} 
    uint64_t get_staked() const { return static_cast<uint64_t>(-staked.amount);} 
    // explicit serialization macro is not necessary, used here only to improve compilation time
    EOSLIB_SERIALIZE(account, (id)(owner)(total)(liquid)(staked)(refund))
};

typedef eosio::multi_index<N(accounts), account,
                            indexed_by<N(byowner), const_mem_fun<account, uint64_t, &account::get_owner>>, 
                            indexed_by<N(bytotal), const_mem_fun<account, uint64_t, &account::get_total>>,
                            indexed_by<N(byliquid), const_mem_fun<account, uint64_t, &account::get_liquid>>,
                            indexed_by<N(bystaked), const_mem_fun<account, uint64_t, &account::get_staked>>
                            > accounts;

uint64_t make_pending_id(uint64_t pair_id, uint64_t prefix, uint32_t last_stake_time)
{
    return (pair_id << 40) | (prefix << 32) | last_stake_time;
}

// (_self, _self)
struct stake_entry
{
    uint64_t id;
    uint64_t pair_id;
    account_name owner;
    asset effect;
    asset pending;

    uint32_t last_stake_time;
    uint64_t bonus_period;
    asset bonus_sum;
    uint64_t status; 

    uint64_t primary_key() const { return id; }

    uint64_t get_owner() const { return owner; }
    uint128_t get_unique_id() const { return make_key_128(pair_id, owner); }
    uint64_t get_last_stake_time() const { return static_cast<uint64_t>(-last_stake_time); } // with reverse order
    uint128_t get_last_stake_pair_time() const { return static_cast<uint128_t>(-make_key_128(pair_id, last_stake_time));} 
    uint64_t get_pending() const { return make_pending_id(pair_id, pending.amount > 0 ? 1 : 0, last_stake_time); }
    uint128_t get_top() const { return static_cast<uint128_t>(-make_key_128(pair_id, (effect+pending).amount)); }
    EOSLIB_SERIALIZE(stake_entry, (id)(pair_id)(owner)(effect)(pending)(last_stake_time)(bonus_period)(bonus_sum)(status))
};

typedef eosio::multi_index<N(stakes), stake_entry,
                           indexed_by<N(byowner), const_mem_fun<stake_entry, uint64_t, &stake_entry::get_owner>>,
                           indexed_by<N(byuniqueid), const_mem_fun<stake_entry, uint128_t, &stake_entry::get_unique_id>>,
                           indexed_by<N(bylstime), const_mem_fun<stake_entry, uint64_t, &stake_entry::get_last_stake_time>>,
                           indexed_by<N(bylsptime), const_mem_fun<stake_entry, uint128_t, &stake_entry::get_last_stake_pair_time>>,
                           indexed_by<N(bypending), const_mem_fun<stake_entry, uint64_t, &stake_entry::get_pending>>,
                           indexed_by<N(bytop), const_mem_fun<stake_entry, uint128_t, &stake_entry::get_top>>>
    stake_table;

enum exch_status : uint8_t
{
    PENDING = 0,
    INPROGRESS = 1,
    DONE = 2,
};

// (_self, pair_id) as table
struct exch_bonus
{
    uint64_t period;
    asset total;
    asset remain;
    double factor; // should use double, pay attention to precision of RTC
    uint32_t threshold_time;
    uint32_t end_time;
    uint8_t status; // 0: pending; 1: inprogress; 2: done

    uint64_t primary_key() const { return period; }

    EOSLIB_SERIALIZE(exch_bonus, (period)(total)(remain)(factor)(threshold_time)(end_time)(status))
};

typedef eosio::multi_index<N(exchbonus), exch_bonus> exchbonus_table;

// (_self, _self)
struct refund_request
{
    uint64_t id; // auto increment
    account_name owner;
    time request_time;
    asset pending; // RTC

    uint64_t primary_key() const { return id; }
    uint64_t get_owner() const { return owner; }
    uint64_t get_request_time() const { return static_cast<uint64_t>(request_time);}
    uint64_t get_pending() const { return static_cast<uint64_t>(-pending.amount);} 
    // explicit serialization macro is not necessary, used here only to improve compilation time
    EOSLIB_SERIALIZE(refund_request, (id)(owner)(request_time)(pending))
};

typedef eosio::multi_index<N(refunds), refund_request,
                        indexed_by<N(byowner), const_mem_fun<refund_request, uint64_t, &refund_request::get_owner>>,
                        indexed_by<N(byrequest), const_mem_fun<refund_request, uint64_t, &refund_request::get_request_time>>,
                        indexed_by<N(bypending), const_mem_fun<refund_request, uint64_t, &refund_request::get_pending>>
                        > refunds_table;

enum ROLE : uint8_t
{
    SUDOER = 1,
    ADMIN = 2,
    FEE_ACCOUNT = 3,
    DEX_ACCOUNT = 4,
    PIXIU = 5,
    TICKER = 6,
    MAX_ROLE = 7,
};

class venus : public eosio::contract
{
  public:
    using contract::contract;
    venus(account_name s);
    ~venus();
    
    /// @abi action
    void sellrtc(account_name account, asset quantity);

    /// @abi action
    void setrtc(uint64_t max_rtc_amount);

    /// @abi action
    void openaccount(account_name owner, account_name ram_payer);

    /// @abi action
    void closeaccount(account_name owner);

    /// @abi action
    void refund(account_name owner);

    /// @abi action
    void reset();

    /// @abi action
    void delegate(account_name from, asset quantity, uint64_t pair_id);

    /// @abi action
    void undelegate(account_name from, asset quantity, uint64_t pair_id);

    /// @abi action
    void claimbonus(account_name from, uint64_t pair_id);

    /// @abi action
    void sudorelease(account_name from, uint64_t pair_id);

    /// @abi action
    void tick(string info);

    /// @abi action
    void setstatus(uint8_t status);

    /// @abi action
    void setaccount(uint8_t role, account_name name);

    /// @abi action
    void settransfer(uint8_t status);

    /// @abi action
    void setsupply(uint64_t supply);

    void apply(account_name contract, account_name act);

  private:
    global_state get_default_parameters();

    void on_transfer(const transfer_args &t, account_name code);
    void do_buyrtc(account_name from, asset quant);
    void do_transfer(account_name contract, account_name from, account_name to, const asset &quantity, string memo);
    bool on_eos_received(account_name from,
                         account_name to,
                         asset quantity,
                         string memo);
    void process_xxx_fee(const account_name contract,
                         account_name from,
                         account_name to,
                         asset quantity,
                         string memo);
    bool on_fee_received(const account_name contract, const asset &quantity, const string &memo);
    bool transfer_rtc(account_name from,
                      account_name to,
                      asset quantity,
                      string memo);
    void sub_balance(account_name owner, asset value);
    void add_balance(account_name owner, asset value, account_name ram_payer);
    void trigger_trading();
    void snapshot_stake(uint64_t pair_id);
    void update_bonus(account_name owner, asset quantity);
    void append_stake(account_name owner, asset quantity, uint64_t pair_id);
    void revoke_stake(account_name owner, asset quantity, uint64_t pair_id);
    void update_stake(uint64_t pair_id, asset total, asset effect);
    void get_latest_bonus_params(uint64_t pair_id, uint32_t &threshold_time, uint64_t &period);
    void ensure_exch_fee(uint64_t pair_id, bool auto_create);
    bool ensure_exch_bonus(uint64_t pair_id, bool auto_create, uint64_t period, const extended_symbol &bonus_symbol);
    void should_exist_exch_pair(uint64_t pair_id, extended_symbol &symbol);
    void get_bonus_info(string &memo, uint64_t period);
    void ensure_status();

  private:
    rtcmarket _rtcmarket;
    global_state_singleton _global;
    global_state _gstate;

    exchfee_table _exchfee;
    stake_table _stakes;
    accounts _accounts;
    refunds_table _refunds;
};

} // namespace vns