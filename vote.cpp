// Vote Logic
// every account has one vote for one exchange pair
// memo format: "vote:pair_id:refer"
// for example: "vote:1" or "vote:1:hsiung"
bool youdex::process_vote(const transfer_args& t) {
    account_name from = t.from;
    account_name to = t.to;
    string memo = t.memo;

    eosio_assert(to == _self, "should be the contract");
    require_auth(from);
    // we only accept 0.0001 EOS
    if(t.quantity != asset(1)) {
        print("should be 0.0001 EOS");
        return false;
    }

    string m = memo;
    trim(m);
    do {
        if (m.length() < 6) {
            print("invalid vote memo length");
            break;
        }
        
        std::vector<std::string> parts;
        split_memo(parts, m, ':');
        if (parts.size() < 2) {
            print("invalid vote memo parts");
            break;
        }

        if (parts[0] != "vote") {
            print("should begin with vote");
            break;
        }

        uint64_t pair_id = to_int<uint64_t>(parts[1]);
        string referee_str;
        account_name referee = 0;
        if (parts.size() > 2) {
            referee_str = parts[2];
            referee = eosio::string_to_name(referee_str.c_str());

            eosio_assert(is_account(referee), "err_invalid_referee"); // referee should be an account
            eosio_assert(from != referee, "err_cannot_refer_self"); 
        }

        print_f("got vote  %, %, %\n", name{.value=from}, pair_id, name{.value=referee});
        // check if exch pair exists
        auto exch_itr = _exchpairs.find(pair_id);
        eosio_assert(exch_itr != _exchpairs.end(), "err_pair_not_found");
        // avoid ram usage
        eosio_assert(exch_itr->status == PENDING, "err_voting_is_over");
        // do not allow voting if manager is not venus
        eosio_assert(exch_itr->manager == _gstate.venus, "err_wrong_manager");
        // check if duplicate vote
        vote_table votes(_self, _self);
        auto pairvotes = votes.get_index<N(bypairfrom)>();
        auto itr = pairvotes.find(make_key_128(pair_id, from));
        eosio_assert(itr == pairvotes.end(), "err_duplicate_vote"); // cannot vote the pair more than 1 time
        // update vote count 
        uint64_t vote_count = 0;
        auto runtime_itr = _exchruntimes.find(pair_id);
        eosio_assert(runtime_itr != _exchruntimes.end(), "err_runtime_not_found");
        _exchruntimes.modify(runtime_itr, 0, [&](auto& er) {
            vote_count = ++er.vote_count;
        });
        ensure_status();
        // use our ram to emplace the vote
        votes.emplace(_self, [&](auto& v) {
            v.id = votes.available_primary_key();
            v.pair_id = pair_id;
            v.from = from;
            v.referee = referee;
            v.timestamp = now();
            
            eosio_assert(v.id < max_vote_allowed, "err_exceed_max_votes");
            eosio_assert(v.from != v.referee, "err_cannot_refer_self"); // sanity checking
        });

        do {
            if (vote_count < exch_itr->vote_threshold) break;
            // get staked RTC from contract venus
            vns::exchfee_table exchfees(_gstate.venus, _gstate.venus);
            auto exchfee_itr = exchfees.find(pair_id);
            if (exchfee_itr == exchfees.end()) break;

            if (exchfee_itr->total.amount < exch_itr->stake_threshold) break;
            // activate it now
            _exchpairs.modify(exch_itr, 0, [&](auto& ep) {
                ep.status = ACTIVE;
                ep.lastmod_time = now();
                print("info_congratulations, exch pair % is actived by %\n", pair_id, name{.value=from});
            });

        } while (0);
        return true;
    } while (0);
    return false;
}