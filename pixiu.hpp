#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/singleton.hpp>

#include "transfer.hpp"


using namespace eosio;
using namespace std;

namespace pix {
// (_self, _self)
struct global_state
{
    // 3 levels adminstration(_self > admin > sudoer)
    account_name sudoer; // operator
    account_name admin;  // administrator

    uint8_t status; // switch on/off

    // for future usage
    account_name a1; 
    account_name a2;
    uint64_t r1;
    uint64_t r2;

    EOSLIB_SERIALIZE(global_state, (sudoer)(admin)(status)(a1)(a2)(r1)(r2))
};
typedef eosio::singleton<N(global), global_state> global_state_singleton;

enum ROLE : uint8_t
{
    SUDOER = 1,
    ADMIN = 2,
    MAX_ROLE = 3,
};
class pixiu: public eosio::contract {
public:
	using contract::contract;
    pixiu(account_name s);
    ~pixiu();
	
    /// @abi action
    void add (account_name from, eosio::name listid );
    
    /// @abi action
    void remove( account_name from, eosio::name listid );

    /// @abi action
    void addwatch(account_name contract);

    /// @abi action
    void removewatch(account_name contract);

    /// @abi action
    void setstatus(uint8_t status);

    /// @abi action
    void setaccount(uint8_t role, account_name name);

    /// @abi action
    void reset();

    void apply( account_name contract, account_name act );

private:
    global_state get_default_parameters();
    void ensure_status();
    void on_transfer(const transfer_args& t, account_name code);
    bool is_blocked_account(account_name from);
    bool is_valid_list(eosio::name listid);
    bool on_watch_list(account_name from);
    void do_add(account_name from, eosio::name listid, bool on_notify);

private:
    global_state_singleton _global;
    global_state _gstate;
};
} /// namespace pix