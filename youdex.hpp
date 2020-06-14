#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/singleton.hpp>
#include "transfer.hpp"
#include "order.hpp"

using namespace eosio;
using namespace std;
using namespace dex;
using namespace exch;

struct global_state {
    account_name    creator;
    uint32_t        created_time;
    uint8_t         status; // 0: inactive; 1: active
    uint64_t        order_count;
    uint64_t        fee;

    uint64_t        pair_count;

    // 3 levels adminstration(_self > admin > sudoer)
    account_name    oper;   // operator 
    account_name    sudoer; // advanced operator
    account_name    admin;  // administrator
    
    account_name    venus;  // in charge of RTC and bonus allocation
    account_name    pixiu;  // guard and blacklist
    account_name    fee_account; // receive fee

    uint64_t        max_match_time;

    // for future usage
    account_name    a1;
    account_name    a2;
    uint64_t        r1; 
    uint64_t        r2;
    uint64_t        r3;
    uint64_t        r4;
    uint64_t        r5;
    uint64_t        r6;
    uint64_t        r7;
    uint64_t        r8;

    auto primary_key() const { return creator; }

    EOSLIB_SERIALIZE( global_state, (creator)(created_time)(status)(order_count)(fee)(pair_count)\
                    (oper)(sudoer)(admin)(venus)(pixiu)(fee_account)(max_match_time)\
                    (a1)(a2)(r1)(r2)(r3)(r4)(r5)(r6)(r7)(r8))
};

typedef eosio::singleton<N(global), global_state> global_state_singleton;


// (_self, _self)
struct notify_entry {
    uint64_t        id; // auto increment
    checksum256     txid;
    time            created_time;

    auto primary_key()const { return id; }
    uint64_t get_time() const { return static_cast<uint64_t>(-created_time); } 

    EOSLIB_SERIALIZE(notify_entry, (id)(txid)(created_time))
};

typedef eosio::multi_index<N(notifies), notify_entry,
        indexed_by<N(bytime), const_mem_fun<notify_entry, uint64_t, &notify_entry::get_time>>>
    notify_table;

enum ROLE: uint8_t {
    SUDOER = 1,
    ADMIN  = 2,
    VENUS  = 3,
    PIXIU  = 4,
    FEE_ACCOUNT = 5,
    OPERATOR = 6,
    MAX_ROLE = 7,
};

class youdex: public eosio::contract {
public:
    const uint32_t FIVE_MINUTES = 5*60;
	using contract::contract;
	
    youdex(account_name self ) : contract(self), _global(_self, _self), 
    _exchpairs(_self, _self), _exchruntimes(_self, _self), _orders(_self, _self) {
         _gstate = _global.exists() ? _global.get() : get_default_parameters();
    }

    ~youdex() {
        _global.set( _gstate, _self );
    }

    global_state get_default_parameters() {
        global_state s;

        s.creator       = _self;
        s.created_time  = now();
        s.status        = 0; // require activate
        s.order_count   = 0;
        s.fee           = 1000;

        s.pair_count    = 0;
        s.oper          = _self;
        s.sudoer        = _self;
        s.admin         = _self;
        s.venus         = _self;
        s.pixiu         = _self;
        s.fee_account = _self;
        s.max_match_time = default_max_transaction_cpu_usage/2; // in microseconds
        // future usage
        s.a1            = 0;
        s.a2            = 0;
        s.r1 = s.r2 = s.r3 = s.r4 = s.r5 = s.r6 = s.r7 = s.r8 = 0;

        return s;
    }

    /// @abi action
    void openaccount( account_name owner, uint64_t count, account_name ram_payer );
    
    /// @abi action
    void closeaccount( account_name owner, uint64_t count );

    /// @abi action
    void cancelorder(account_name from, uint64_t id);

    /// @abi action
    void createx(account_name manager, extended_symbol quote, extended_symbol base, uint64_t scale);

    /// @abi action
    void setx(uint64_t pair_id, uint8_t status);

    /// @abi action
    void setmanager(uint64_t pair_id, account_name manager);

    /// @abi action
    void recover(uint64_t pair_id);

    /// @abi action testing only
    void cleanx(uint64_t pair_id,  uint64_t count );

    /// @abi action TODO:
    void setthreshold(uint64_t pair_id, uint64_t vote, uint64_t stake);

    /// @abi action 
    void reset(uint8_t level);

    /// @abi action
    void resetvote(account_name from, uint64_t pair_id, uint64_t amount);

    /// @abi action
    void push(account_name from, uint64_t id);

    /// @abi action
    void tick(uint64_t pair_id, string info);

    /// @abi action
    void setstatus(uint8_t status);
    
    /// @abi action
    void setaccount(uint8_t role, account_name name);

    /// @abi action
    void setparam(uint64_t key, uint64_t value);

    /// @abi action
    void setmatchtime(uint64_t max_match_time);

    /// @abi action
    void setpairinfo(uint64_t pair_id, string website, string issue, string supply, string desc_cn, string desc_en, string desc_kr);

    /// @abi action
    void addnotify(string info_cn, string info_en, string info_kr);

    /// @abi action
    void deletenotify(uint64_t id);

    /// @abi action
    void updatenotify(uint64_t id, string info_cn, string info_en, string info_kr);

    void apply( account_name contract, account_name act );

private:
    void on_transfer(const transfer_args& t, account_name code);
    bool is_blocked_account(account_name from);
    uint64_t next_order_id();
    bool check_fee(const asset& quantity);
    bool calc_fee(asset& fee, asset &quant_after_fee);
    uint64_t match(uint64_t gid);

    void update_runtime(uint64_t pair_id, const asset& latest_price, const asset& lowest_price, const asset& highest_price, const asset& quote, const asset& base);

    void close_order(order&o, uint8_t reason, account_name contract); 
    void split_fee(const exchange_pair& pair, const asset& quantity, uint64_t ask_id, uint64_t bid_id, uint64_t price, uint64_t volume);
    void do_transfer(account_name contract, account_name from, account_name to, const asset& quantity, string memo);
    bool update_order(order& o, asset take, asset get, account_name contract);

    bool process_vote(const transfer_args& t);
    void get_fee_info(string& info, uint64_t pair_id, uint64_t ask_id, uint64_t bid_id, uint64_t price, uint64_t volume);
    void ensure_status();
    checksum256 get_txid();

    asset to_base(const asset& p, uint64_t scale, const symbol_type& symbol) {
        auto temp = p / scale;
        eosio_assert(temp.amount > 0, "should be positive amount");
        return asset(temp.amount, symbol);
    }

    asset scale_base(const asset& b, uint64_t scale, const symbol_type& symbol) {
        auto temp = b * scale;
        eosio_assert(temp.amount > 0, "should be positive amount");
        return asset(temp.amount, symbol);
    }

    exchange_pair_table _exchpairs;
    exchange_runtime_table _exchruntimes;
    order_table _orders;
    
    global_state_singleton _global;
    global_state _gstate;
};