#include <eosiolib/eosio.hpp>
#include "utils.hpp"
#include "pixiu.hpp"
#include "accounts.hpp"

using namespace eosio;

namespace pix {
pixiu::pixiu(account_name s) : contract(s), _global(_self, _self) {
    _gstate = _global.exists() ? _global.get() : get_default_parameters();
}

pixiu::~pixiu()
{
    // make sure results are saved properly after exit
    _global.set(_gstate, _self);
}

global_state pixiu::get_default_parameters()
{
    global_state gs;

    gs.sudoer = _self;
    gs.admin = _self;
    
    gs.status = 0; // TODO: 0: NOT OPEN; 1: RUN; 2:PAUSE; 3:STOP
    gs.a1 = 0;
    gs.a2 = 0;
    gs.r1 = 0;
    gs.r2 = 0;

    return gs;
}

void pixiu::on_transfer(const transfer_args& t, account_name code) {
    // To avoid forged transfer
    require_auth(t.from);

    bool malicious = true;
    do {
        // call directly
        if (code == _self) {
            print("Attack: call transfer with our contract direclty\n");
            break;
        }
        // on notify
        if (t.to == _self) {
            malicious = false;
            break;
        }
        // notificatin by other contracts, we only filter transfer with current watch list
        // since this might be abused by evil account to report normal account deliberately
        if (t.to == code && on_watch_list(code)) {
            print("Warning: ", name{.value=code}, " is under attack by ", name{.value=t.from}, "\n");
            break;
        }

        malicious = false;
    } while (0);

    if (malicious) {
        print("Possible attack, add sender to blacklist!!!\n");
        do_add(t.from, name{.value=N(blacklist)}, true);
        return;
    }

    print("Received transfer from ", name{.value=code}, " with ", t, "\n");
}

bool pixiu::on_watch_list(account_name contract) {
    watch_table watchlist(_self, _self);
    auto itr = watchlist.find(contract);
    if (itr != watchlist.end()) {
        print("Found contract ", name{.value=contract}, "\n");
        return true;
    }

    return false;
}

void pixiu::addwatch(account_name contract) {
    require_auth(_gstate.admin);
    eosio_assert(_self != contract, "cannot add myself to the watch list");
    eosio_assert(is_account(contract), "should be valid account");
    
    watch_table watchlist(_self, _self);
    auto itr = watchlist.find(contract); 
    eosio_assert(itr == watchlist.end(), "already on the watch list");

    watchlist.emplace(_self, [&](auto& w) {
        w.contract = contract;
        w.created_time = now();
    });

    print("Added ", name{.value=contract}, " to the watch list");
}

void pixiu::removewatch(account_name contract) {
    require_auth(_gstate.admin);

    watch_table watchlist(_self, _self);
    auto itr = watchlist.find(contract); 
    eosio_assert(itr != watchlist.end(), "contract not on the watch list");

    watchlist.erase(itr);

    print("Removed ", name{.value=contract}, " from the watch list"); 
}

bool pixiu::is_blocked_account(account_name from) {
    // Check whitelist account
    specialacnt_table whitelist( N(pixiu), N(whitelist));
    auto itr = whitelist.find(from);
    if (itr != whitelist.end()) {
        print("Allow whitelist account ", name{.value=from});
        return false;
    }
    // Check blacklist
    specialacnt_table blacklist( N(pixiu), N(blacklist));
    itr = blacklist.find(from);
    if (itr != blacklist.end()) {
        print("Allow whitelist account ", name{.value=from});
        return true;
    }

    return false;
}

// Add an account to the special account list
void pixiu::add( account_name from, eosio::name listid ) {
    require_auth(_gstate.sudoer);
    eosio_assert(_self != from, "cannot add myself");
    eosio_assert(is_account(from), "should be a valid account");
    eosio_assert(is_valid_list(listid), "invalid list");

    do_add(from, listid, false); 
}

// Do the actual adding operation
void pixiu::do_add( account_name from, eosio::name listid, bool on_notify ) {
    specialacnt_table list( _self, listid);
    auto itr = list.find(from);
    eosio_assert(_self != from, "cannot add myself to list");

    if (itr != list.end()) {
        eosio_assert(on_notify, "account is already in the list");
        print(name{.value=from}, " is already in ", listid);
        return;
    }
    // Use my ram sinc billing on notify is forbidded now
    list.emplace(_self, [&](auto& a) {
        a.name = from;
        a.created_time = now();
    });
    
    print("Added account ", name{.value=from}, " to ", listid);
}

// Remove an account from the special account list
void pixiu::remove( account_name from, eosio::name listid ) {
    require_auth(_gstate.sudoer);
    eosio_assert(is_valid_list(listid), "invalid list");

    specialacnt_table list( _self, listid);
    auto itr = list.find(from);
    
    eosio_assert(itr != list.end(), "account does not exist");

    list.erase(itr);
    print("Removed account ", name{.value=from}, " from ", listid);
}

// Check if the list name is valid or not
bool pixiu::is_valid_list(eosio::name listid) {
    return (listid == N(whitelist) || listid == N(blacklist));
}

void pixiu::setstatus(uint8_t status)
{
    require_auth(_gstate.admin);

    eosio_assert(status == 0 || status == 1, "invalid status");
    eosio_assert(status != _gstate.status, "same status");

    _gstate.status = status;
    _global.set(_gstate, _self);
}

void pixiu::ensure_status()
{
    eosio_assert(_gstate.status == 1, "should be active status");
}

void pixiu::setaccount(uint8_t role, account_name name)
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
    default:
        eosio_assert(false, "invalid role");
        break;
    }

    _global.set(_gstate, _self);
}

// Reset lists
void pixiu::reset() {
    require_auth(_self);

    uint64_t count = 0;
    do {
        specialacnt_table whitelist( _self, N(whitelist));
        count = clear_table(whitelist, 10);
        if (count > 0) break;

        specialacnt_table blacklist( _self, N(blacklist));
        count = clear_table(blacklist, 10);
        if (count > 0) break;

        watch_table watchlist(_self, _self); 
        count = clear_table(watchlist, 10);
        if (count > 0) break;

        // reset global state
        _global.remove();
        eosio_exit(0); // to skip destructor

    } while (0);
}

// Entrance of contract
void pixiu::apply( account_name contract, account_name act ) {

    if( act == N(transfer) ) {
        on_transfer( unpack_action_data<transfer_args>(), contract );
        return;
    }

    if( contract != _self ) {
        // reject all the annoying advertisements
        eosio_assert(false, "unexpected contract");
        return;
    }

    switch (act) {
        case N(add):
        case N(addwatch):
        ensure_status();
        break;
        default:
        break;
    }

    auto& thiscontract = *this;
    switch( act ) {
        EOSIO_API( pixiu, (setstatus)(setaccount)(add)(remove)(addwatch)(removewatch)(reset) )
        default:
            eosio_assert(false, "unexpected action");
            break;
    };

}

extern "C" {
    void apply( uint64_t receiver, uint64_t code, uint64_t action ) {
        // TODO: check how receiver and code are assigned
        pixiu px( receiver );
        px.apply( code, action );
        /* does not allow destructor of thiscontract to run: eosio_exit(0); */
   }
}

} /// namespace pix