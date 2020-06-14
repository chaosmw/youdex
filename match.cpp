// match engine implementation

bool youdex::check_fee(const asset& quantity) {
    eosio_assert(quantity.amount >= 0, "err_invalid_quantity"); // check quantity should be positive
    auto fee = quantity;
    auto after_fee = quantity;

    return calc_fee(fee, after_fee);
}

bool youdex::calc_fee(asset& fee, asset &quant_after_fee) {
      fee.amount = ( fee.amount + 999 ) / 1000; /// 0.0001 % fee (round up)
      // fee.amount cannot be 0 since that is only possible if quant.amount is 0 which is not allowed by the assert above.
      // If quant.amount == 1, then fee.amount == 1,
      // otherwise if quant.amount > 1, then 0 < fee.amount < quant.amount.
      quant_after_fee.amount -= fee.amount;
      // quant_after_fee.amount should be > 0 if quant.amount > 1.
      // If quant.amount == 1, then quant_after_fee.amount == 0 and the next inline transfer will fail causing the buyram action to fail.
      if (quant_after_fee.amount == 0) {
          print("not enough for trading fee, close it\n");
          return false;
      }

      return true;
}

// This match function matches orders
uint64_t youdex::match(uint64_t gid) {
    uint64_t deadline = current_time() + _gstate.max_match_time;

    auto current = _orders.find(gid);
    eosio_assert(current != _orders.end(), "found no order, should not happen");
    auto& o = *current; // should use reference, otherwise it's a copy of current order

    print_f("matching engine starts with % price %\n", name{.value=o.owner}, o.price);
    auto itr_exch = _exchpairs.find(o.pair_id);
    eosio_assert(itr_exch != _exchpairs.end(), "invalid exchange pair id");
    auto pair = *itr_exch;
    eosio_assert(pair.status == ACTIVE, "err_pair_not_activated");

    bool ask = is_ask_order(o.type);
    auto aos = _orders.get_index<N(byprice)>();

    auto latest_price = asset(0, get_price_symbol(pair.base, pair.scale));
    auto lowest_price = latest_price;
    auto highest_price = latest_price;
    auto total_quote = asset(0, pair.quote);
    auto total_base = asset(0, pair.base);

    uint64_t updated = 0;
    switch (o.type) {
        case ASK_LIMITED: {
            bool match_market = false;
            // firstly, check if we're the lowest ask order
            auto lowest = aos.lower_bound(make_match_128(o.pair_id, o.type, asset(0, o.price.symbol)));
            eosio_assert(lowest != aos.end(), "lowest not found, should not happed");
            if (lowest->gid == gid) {
                match_market = true;
            }
            order_type low_type, high_type;
            get_range(o.type, match_market, low_type, high_type);

            // rule: use ask's price, from market to limited bid orders to trade, as for bid orders, high biders 
            // will have higher priority than low biders

            auto low = aos.lower_bound(make_match_128(o.pair_id, low_type, asset(0, o.price.symbol)));
            auto high = aos.upper_bound(make_match_128(o.pair_id, high_type+1, asset(0, o.price.symbol)));

            for (auto curitr = low; curitr != high && curitr->pair_id == o.pair_id && curitr->type >= low_type && curitr->type <= high_type; ) {
                auto itr = curitr++;
                if (itr->type == BID_LIMITED) {
                    if (itr->price < o.price) {
                        print("across lower border, matching is over");
                        break;
                    } else {
                        print("matched bid limited order price: ", itr->price, "\n");
                    }
                } else {
                    eosio_assert(itr->type == BID_MARKET, "should be market order");
                    print_f("matched bid market order %\n", itr->gid);
                }
            
                if (current_time() >= deadline) {
                    print("time is up, exit asap to avoid timeout error");
                    break;
                }
                if (o.remain.amount == 0) {
                    print("order ", o.id, " is settled completely, close it\n");
                    _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NORMAL, pair.quote.contract); }); 
                    updated++;
                    break;
                }

                eosio_assert(o.remain.amount > 0, "negative ask balance, should not happen");
                eosio_assert(itr->remain.amount > 0, "negative bid balance, should not happen");

                // settle order on behalf of the placer
                // use ask's price
                asset total_ask = o.remain.amount * o.price;
                total_ask /= pair.quote_precision; 
                total_ask = to_base(total_ask, pair.scale, pair.base);

                eosio_assert(total_ask.amount > 0, "total remain overflow?");
            
                auto bid_fee = itr->remain;
                auto bid_after_fee = itr->remain;
                if (!calc_fee(bid_fee, bid_after_fee)) {
                    // close the bid order
                    aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); }); 
                    updated++;
                    continue;
                }

                if (total_ask <= bid_after_fee) {
                
                    auto ask_fee = total_ask;
                    auto ask_after_fee = total_ask;

                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.quote.contract); });
                        updated++;
                        break;
                    }
                    // cannot use bid_fee since trade amount is based on total_ask
                    auto trade = o.remain; 
                    auto target = total_ask;
                    bid_fee = ask_fee; 
                    auto price = o.price;

                    // step1: transfer fee to system and partner accounts
                    split_fee(pair, bid_fee+ask_fee, o.id, itr->id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract,  _self, o.owner, target-ask_fee, "bingo1");
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, itr->owner, trade, "bingo2");
                    // step4: update current order
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    // step5: update itr order
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    
                    updated++;
                    break;
                } else {
                    // calc bid_fee again
                    auto ask_fee = bid_after_fee;
                    auto ask_after_fee = bid_after_fee;
                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        // close the bid order since ask > bid
                        aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                        updated++;
                        continue;
                    }
                    // split base token
                    auto price = o.price;
                    auto temp = bid_after_fee * pair.scale * pair.quote_precision;
                    eosio_assert(temp.amount > 0, "amount overflow");

                    auto volume = temp / price.amount;
                    eosio_assert(volume.amount > 0, "invalid volume, overflow?");
                    auto trade = asset(volume.amount, o.initial.symbol);
                     
                    auto target = trade.amount * price / pair.quote_precision;
                    target = to_base(target, pair.scale, pair.base);
                    eosio_assert(target.amount > 0, "invalid target, overflow?");

                    auto remainder = temp.amount % price.amount;
                    if (remainder == 0) {
                        eosio_assert(target == bid_after_fee, "no remainder, should match");
                    } else {
                        // print("recalculate fees"); 
                        ask_fee = target;
                        ask_after_fee = target;
                        if (!calc_fee(ask_fee, ask_after_fee)) {
                            // close the bid order since ask > bid
                            aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                            updated++;
                            continue;
                        }
                        bid_fee = ask_fee; 
                    }
                    // step1: transfer fee to system and partner account
                    split_fee(pair, bid_fee+ask_fee, o.id, itr->id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract, _self, o.owner, target-ask_fee, "bingo3");
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, itr->owner, trade, "bingo4");
                    // step4: update itr order 
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    // step5: update current order
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                    continue;
                }
            }
        }
        break;
        case ASK_MARKET: { // similar to the above routines
            // only match limited bid orders, from higher price to lower price            
            auto low = aos.lower_bound(make_match_128(o.pair_id, BID_LIMITED, asset(0, o.price.symbol))); // limited price should be positive
            auto high = aos.upper_bound(make_match_128(o.pair_id, BID_LIMITED+1, asset(0, o.price.symbol)));
            for (auto curitr = low; curitr != high && curitr->pair_id == o.pair_id && curitr->type == BID_LIMITED; ) {
                auto itr = curitr++;
                print_f("matched bid limited order price: %\n", itr->price);
                if (current_time() >= deadline) {
                    print("time is up, exit asap to avoid timeout error");
                    break;
                }

                if (o.remain.amount == 0) {
                    print("order ", o.id, " is settled completely, close it\n");
                    _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NORMAL, pair.quote.contract); }); 
                    updated++;
                    break;
                }
                eosio_assert(o.remain.amount > 0, "negative ask balance, should not happen");
                eosio_assert(itr->remain.amount > 0, "negative bid balance, should not happen");

                auto price = itr->price; // use bider's price
                asset total_ask = o.remain.amount * price;
                total_ask /= pair.quote_precision;
                total_ask = to_base(total_ask, pair.scale, pair.base);
                eosio_assert(total_ask.amount > 0, "total remain overflow?");
            
                auto bid_fee = itr->remain;
                auto bid_after_fee = itr->remain;
                if (!calc_fee(bid_fee, bid_after_fee)) {
                    // close the bid order
                    aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); }); 
                    updated++;
                    continue;
                }

                if (total_ask <= bid_after_fee) {
                    auto ask_fee = total_ask;
                    auto ask_after_fee = total_ask;
                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.quote.contract); });
                        updated++;
                        break;
                    }
                    // cannot use bid_fee since trade amount is based on total_ask
                    // split base token
                    auto trade = o.remain; 
                    auto target = total_ask;
                    bid_fee = ask_fee;

                    // step1: transfer (2*ask_fee) to system and partner accounts
                    split_fee(pair, bid_fee+ask_fee, o.id, itr->id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract,  _self, o.owner, target-ask_fee, "bingo1");
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, itr->owner, trade, "bingo2");
                    // step4: update current order 
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    // step5: update itr order
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                    break;
                } else {
                    // calc bid_fee again
                    auto ask_fee = bid_after_fee;
                    auto ask_after_fee = bid_after_fee;
                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        // close the bid order since ask > bid
                        aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                        updated++;
                        continue;
                    }
                    // split base token
                    auto temp = bid_after_fee * pair.scale * pair.quote_precision;
                    eosio_assert(temp.amount > 0, "amount overflow");

                    auto volume = temp / price.amount;
                    eosio_assert(volume.amount > 0, "invalid volume, overflow?");
                    auto trade = asset(volume.amount, o.initial.symbol);
                     
                    auto target = trade.amount * price / pair.quote_precision;
                    target = to_base(target, pair.scale, pair.base);
                    eosio_assert(target.amount > 0, "invalid target, overflow?");

                    auto remainder = temp.amount % price.amount;
                    if (remainder == 0) {
                        eosio_assert(target == bid_after_fee, "no remainder, should match");
                    } else {
                        ask_fee = target;
                        ask_after_fee = target;
                        if (!calc_fee(ask_fee, ask_after_fee)) {
                            // close the bid order since ask > bid
                            aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                            updated++;
                            continue;
                        }
                        bid_fee = ask_fee; 
                    }
                    // step1: transfer (bid_fee+ask_fee) to system account
                    split_fee(pair, bid_fee+ask_fee, o.id, itr->id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract, _self, o.owner, target-ask_fee, "bingo3");
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, itr->owner, trade, "bingo4");
                    // step4: update itr order
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    // step5: update current order
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                    continue;
                }
            }
            
        }
        break;
        case BID_LIMITED: {
            bool match_market = false;
            // firstly, check if we're the highest bid order
            const auto& highest = aos.lower_bound(make_match_128(o.pair_id, o.type, asset(0, o.price.symbol))); 
            eosio_assert(highest != aos.end(), "highest not found, should not happed");
            if (highest->gid == gid) {
                match_market = true;
            }
        
            order_type low_type, high_type;
            get_range(o.type, match_market, low_type, high_type);
            // for the benefit of buyer
            // firstly, match market order, then match match limited orders from lower price to higher price
            const auto &low = aos.lower_bound(make_match_128(o.pair_id, low_type, asset(0, o.price.symbol))); // including market orders if any
            const auto &high = aos.upper_bound(make_match_128(o.pair_id, high_type, o.price));

            for (auto curitr = low; curitr != high && curitr->pair_id == o.pair_id && curitr->type >= low_type && curitr->type <= high_type; ) {
                auto itr = curitr++;
                if (itr->type == ASK_LIMITED) {
                    if (itr->price > o.price) {
                        print("across lower border, matching is over\n");
                        break;
                    } else {
                        print_f("matched ask limited order price % \n", itr->price);
                    }
                } else {
                    eosio_assert(itr->type == ASK_MARKET, "should be market order");
                    print_f("matched ask market order %\n", itr->gid);
                }

                if (current_time() >= deadline) {
                    print("time is up, exit asap to avoid timeout error\n");
                    break;
                }

                if (o.remain.amount == 0) {
                    print("order ", o.id, " is settled completely, close it\n");
                    _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NORMAL, pair.base.contract); });
                    updated++;
                    break;
                }

                eosio_assert(o.remain.amount > 0, "negative ask balance, should not happen");
                eosio_assert(itr->remain.amount > 0, "negative bid balance, should not happen");

                // settle order on behalf of the placer
                asset total_bid = o.remain;
                auto price = (itr->type == ASK_MARKET) ? o.price : itr->price;

                asset total_ask = itr->remain.amount * price;
                total_ask /= pair.quote_precision;
                total_ask = to_base(total_ask, pair.scale, pair.base);

                eosio_assert(total_ask.amount > 0, "total remain overflow?");

                auto bid_fee = total_bid;
                auto bid_after_fee = total_bid;

                if (!calc_fee(bid_fee, bid_after_fee)) {
                    // close the bid order
                    _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                    updated++;
                    break;
                }

                if (total_ask <= bid_after_fee) {
                    auto ask_fee = total_ask;
                    auto ask_after_fee = total_ask;

                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.quote.contract); });
                        updated++;
                        continue; 
                    }

                    auto trade = itr->remain; 
                    auto target = total_ask;
                    bid_fee = ask_fee;

                    // step1: transfer (bid_fee+ask_fee) to system account
                    split_fee(pair, bid_fee+ask_fee, itr->id, o.id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract, _self, itr->owner, target-ask_fee, "bingo5"); 
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, o.owner, trade, "bingo6");
                    // step4: update itr order 
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    // step5: update current order
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                    continue;
                } else { // total_ask > bid_after_fee
                    // calc ask_fee again
                    auto ask_fee = bid_after_fee;
                    auto ask_after_fee = bid_after_fee;

                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                        updated++;
                        break;
                    }

                    auto temp = bid_after_fee * pair.scale * pair.quote_precision;
                    eosio_assert(temp.amount > 0, "amount overflow");

                    auto volume = temp / price.amount;
                    eosio_assert(volume.amount > 0, "invalid volume, overflow?");
                    auto trade = asset(volume.amount, itr->initial.symbol);
                     
                    auto target = trade.amount * price / pair.quote_precision;
                    target = to_base(target, pair.scale, pair.base);
                    eosio_assert(target.amount > 0, "invalid target, overflow?");

                    auto remainder = temp.amount % price.amount;
                    if (remainder == 0) {
                        eosio_assert(target == bid_after_fee, "no remainder, should match");
                    } else {
                        print("recalculate fees"); 
                        ask_fee = target;
                        ask_after_fee = target;
                        if (!calc_fee(ask_fee, ask_after_fee)) {
                            // close the bid order since ask > bid
                            _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                            updated++;
                            break;
                        }
                        bid_fee = ask_fee; 
                    }
                    // step1: transfer (2*bid_fee) to system and partner accounts
                    split_fee(pair, ask_fee+bid_fee, itr->id, o.id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract,  _self, itr->owner, target-ask_fee, "bingo7");
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, o.owner, trade, "bingo8");
                    // step4: update current order 
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    // step5: update itr order
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                
                    break;
                }
            }
        }

        break;
        case BID_MARKET: {
            // only match limited ask orders, from lower price to higher price            
            const auto &low = aos.lower_bound(make_match_128(o.pair_id, ASK_LIMITED, asset(0, o.price.symbol))); // limited price should be positive
            const auto &high = aos.upper_bound(make_match_128(o.pair_id, ASK_LIMITED, asset(asset::max_amount, o.price.symbol)));
            for (auto curitr = low; curitr != high && curitr->pair_id == o.pair_id && curitr->type == ASK_LIMITED; ) {
                auto itr = curitr++;
                print_f("matched ask limited order price: %\n", itr->price);
                   
                if (current_time() >= deadline) {
                    print("time is up, exit asap to avoid timeout error");
                    break;
                }
                if (o.remain.amount == 0) {
                    print("order ", o.id, " is settled completely, close it\n");
                    _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NORMAL, pair.base.contract); });
                    updated++;
                    break;
                }

                eosio_assert(o.remain.amount > 0, "negative ask balance, should not happen");
                eosio_assert(itr->remain.amount > 0, "negative bid balance, should not happen");

                // settle order on behalf of the placer
                asset total_bid = o.remain;
                auto price = itr->price;

                asset total_ask = itr->remain.amount * price;
                total_ask /= pair.quote_precision;
                total_ask = to_base(total_ask, pair.scale, pair.base);
                eosio_assert(total_ask.amount > 0, "total remain overflow?");

                auto bid_fee = total_bid;
                auto bid_after_fee = total_bid;

                if (!calc_fee(bid_fee, bid_after_fee)) {
                    // close the bid order
                    _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                    updated++;
                    break;
                }

                if (total_ask <= bid_after_fee) {
                    auto ask_fee = total_ask;
                    auto ask_after_fee = total_ask;

                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        if (itr->type == ASK_LIMITED) {
                            // close the ask order
                            aos.modify(itr, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.quote.contract); });
                            updated++;
                        }
                        // do not close ASK_MARKET
                        continue; 
                    }

                    auto trade = itr->remain; 
                    auto target = total_ask;
                    bid_fee = ask_fee;

                    // step1: transfer fee to system and partner accounts
                    split_fee(pair, bid_fee+ask_fee, itr->id, o.id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract, _self, itr->owner, target-ask_fee, "bingo5"); 
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, o.owner, trade, "bingo6");
                    // step4: update itr order
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    // step5: update current order
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                    continue;
                } else { // total_ask > bid_after_fee
                    // calc ask_fee again
                    auto ask_fee = bid_after_fee;
                    auto ask_after_fee = bid_after_fee;

                    if (!calc_fee(ask_fee, ask_after_fee)) {
                        _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                        updated++;
                        break;
                    }

                    auto temp = bid_after_fee * pair.scale * pair.quote_precision;
                    eosio_assert(temp.amount > 0, "amount overflow");

                    auto volume = temp / price.amount;
                    eosio_assert(volume.amount > 0, "invalid volume, overflow?");
                    auto trade = asset(volume.amount, itr->initial.symbol);
                     
                    auto target = trade.amount * price / pair.quote_precision;
                    target = to_base(target, pair.scale, pair.base);
                    eosio_assert(target.amount > 0, "invalid target, overflow?");

                    auto remainder = temp.amount % price.amount;
                    if (remainder == 0) {
                        eosio_assert(target == bid_after_fee, "no remainder, should match");
                    } else {
                        print("recalculate fees"); 
                        ask_fee = target;
                        ask_after_fee = target;
                        if (!calc_fee(ask_fee, ask_after_fee)) {
                            // close the bid order since ask > bid
                            _orders.modify(current, 0, [&](auto& ord) { close_order(ord, NOT_ENOUGH_FEE, pair.base.contract); });
                            updated++;
                            break;
                        }
                        bid_fee = ask_fee; 
                    }

                    // step1: transfer fee to system and partner accounts
                    split_fee(pair, ask_fee+bid_fee, itr->id, o.id, price.amount, trade.amount);
                    // step2: transfer base token (such as EOS) from dex to asker
                    do_transfer(pair.base.contract,  _self, itr->owner, target-ask_fee, "bingo7");
                    // step3: transfer quote token (such as ABC) from dex to bider
                    do_transfer(pair.quote.contract, _self, o.owner, trade, "bingo8");
                    // step4: update current order
                    _orders.modify(current, 0, [&](auto& ord) { update_order(ord, target+bid_fee, trade, pair.base.contract); });
                    // step5: update itr order
                    aos.modify(itr, 0, [&](auto& ord) { update_order(ord, trade, target-ask_fee, pair.quote.contract); });
                    
                    if (latest_price.amount == 0) {
                        lowest_price = highest_price = latest_price = price;
                    } else {
                        latest_price = price;
                        lowest_price = (price < lowest_price) ? price : lowest_price;
                        highest_price = (price > highest_price) ? price : highest_price;
                    }
                    total_quote += trade;
                    total_base += (target+bid_fee);
                    updated++;
                
                    break;
                }
            } 
        }
        break;
        default:
        printf("invalid order type, should not happen");
        break;
    }

    if (latest_price.amount > 0) {
        update_runtime(pair.id, latest_price, lowest_price, highest_price, total_quote, total_base);
    }
    
    return updated;
}

void youdex::update_runtime(uint64_t pair_id, const asset& latest_price, const asset& lowest_price, const asset& highest_price, const asset& quote, const asset& base) {
    auto itr = _exchruntimes.find(pair_id);
    eosio_assert(itr != _exchruntimes.end(), "empty runtime record, should not happen");
    _exchruntimes.modify(itr, 0, [&](auto& er) {
        auto current = now();
        auto seconds_of_day = 86400;
        if ((current/seconds_of_day) != (er.lastmod_time/seconds_of_day)) {
            er.lowest_price = latest_price;
            er.highest_price = latest_price;
            er.close_price = er.latest_price;
            er.open_price = latest_price;
            
            er.total_quote = quote;
            er.total_base = base;
        } else {
            er.lowest_price = (lowest_price < er.lowest_price) ? lowest_price : er.lowest_price;
            er.highest_price = (highest_price > er.highest_price) ? highest_price : er.highest_price;
            er.total_quote += quote;
            er.total_base += base;
        }
        er.latest_price = latest_price;
        er.lastmod_time = current;
        er.current_quote -= quote;

        eosio_assert(er.current_quote.amount >= 0, "err_current_quote");
    });
}

void youdex::close_order(order& o, uint8_t reason, account_name contract) {
    eosio_assert(o.remain <= o.initial, "should not exceed initial amount");
    string memo = "smallchange";
    switch (reason) {
        case CANCELLED: memo = "cancelled"; break;
        case EXPIRED: memo = "expired"; break;
    }

    if (is_ask_order(o.type)) {
        auto itr = _exchruntimes.find(o.pair_id);
        eosio_assert(itr != _exchruntimes.end(), "empty runtime record, should not happen");
        _exchruntimes.modify(itr, 0, [&](auto& er) {
            er.current_quote -= o.remain;
            eosio_assert(er.current_quote.amount >= 0, "err_current_quote");
        });
    }

    do_transfer(contract, _self, o.owner, o.remain, memo); 
    o.reset();
}


void youdex::push(account_name from, uint64_t id)
{
    require_auth(from);
    auto aos = _orders.get_index<N(byid)>();
    auto itr = aos.find(id); 
    eosio_assert(itr != aos.end(), "err_order_not_found");
    eosio_assert(itr->type != RESERVED, "err_order_not_active");
    // only owner and system can cancel order!
    if (from != itr->owner) {
        eosio_assert(itr->owner == from, "err_auth_push"); // cannot push other user's order
    }
    
    if (0 == match(itr->gid)) {
        eosio_assert(false, "err_match_failed"); 
    }
}

// due to limitation of system resource, such as cpu, we need to send tick perodically
// to help matching orders. info can be used for advertising
// TODO: allow other people to tick and incentive them with RTC?
// TODO: add direction flag to avoid starving bid orders
void youdex::tick(uint64_t pair_id, string info)
{
    require_auth(_gstate.oper);
    
    // checking params
    eosio_assert( info.size() <= 256, "info has more than 256 bytes" ); 
    eosio_assert(pair_id > 0 && pair_id < max_exch_pair_id, "err_invalid_pair_id");
    auto itr_exch = _exchpairs.find(pair_id);
    eosio_assert(itr_exch != _exchpairs.end(), "err_pair_not_found");
    eosio_assert(itr_exch->status == ACTIVE, "err_pair_not_activated");

    auto pair = *itr_exch;

    // if tick does nothing, should assert error to avoid being banned by BPers
    uint64_t updated = 0;
    do {
        auto aos = _orders.get_index<N(byprice)>();
        auto itr = aos.lower_bound(make_match_128(pair_id, ASK_MARKET, asset(0, pair.base)));
        if (itr == aos.end()) {
            print("no available pending orders");
            break;
        }
        // ASK_MARKET will be matched by BID_LIMITED
        // market orders always come before limited orders
        if (itr->type == ASK_MARKET || itr->type == ASK_LIMITED) {
            // TODO: sanity checking with remaining
            eosio_assert(itr->remain.amount > 0, "remain shoule be positive, should not happen");
            print_f("found pending order % with type % price %\n", itr->id, (uint64_t)itr->type, itr->price);
            updated += match(itr->gid);
            // maybe we have no time to remove expired orders
            if (updated > 0) break;
        }
        // Bid orders are reversly saved
        auto biditr = aos.lower_bound(make_match_128(pair_id, BID_MARKET, asset(0, pair.base)));
        if (biditr == aos.end()) {
            print("no available pending orders");
            break;
        }
        // BID_MARKET will be matched by ASK_LIMITED
        // market orders always come before limited orders
        if (biditr->type == BID_MARKET || biditr->type == BID_LIMITED) {
            eosio_assert(biditr->remain.amount > 0, "remain shoule be positive, should not happen");
            print_f("found pending order % with type % price %\n", biditr->id, (uint64_t)biditr->type, biditr->price);
            updated += match(biditr->gid);
            // maybe we have no time to remove expired orders
            if (updated > 0) break;
        }

    } while (0);

    if (0 == updated) {
        eosio_assert(false, "no biding orders? the world is too quiet, let's make some noise");
    }
}

// only allow owner and system to cancel order
void youdex::cancelorder(account_name from, uint64_t id) {
    require_auth(from);

    auto aos = _orders.get_index<N(byid)>();
    auto itr = aos.find(id); 
    eosio_assert(itr != aos.end(), "err_order_not_found");
    eosio_assert(itr->type != RESERVED, "err_order_not_active");
    // only owner and system can cancel order!
    if (from != _self && from != _gstate.sudoer) {
        eosio_assert(itr->owner == from, "err_auth_cancel"); // cannot cancel other user's order
    }

    // get contract of order
    auto itr_exch = _exchpairs.find(itr->pair_id);
    eosio_assert(itr_exch != _exchpairs.end(), "err_pair_not_found");
    auto pair = *itr_exch;
    bool ask = is_ask_order(itr->type);
    auto contract = ask ? pair.quote.contract : pair.base.contract;

    aos.modify(itr, 0, [&](auto& ord) { close_order(ord, CANCELLED, contract); });

    print("GID: ", itr->gid, " id: ", itr->id, " is cancelled\n");
}

bool youdex::update_order(order& o, asset take, asset get, account_name contract) {
    eosio_assert(take.amount > 0, "take amount should be positive");
    eosio_assert(get.amount > 0, "get amount should be positive");

    bool closed = false;
    o.remain -= take;
    o.deal += get;
    o.updated_time = now();

    eosio_assert(o.remain.amount >= 0, "remain should be positive");
    if (!check_fee(o.remain)) {
        eosio_assert(o.remain < o.initial, "should not exceed initial amount");
        do_transfer(contract, _self, o.owner, o.remain, "smallchange2");
        o.reset();
        closed = true;
    }

    return closed;
}

void youdex::split_fee(const exchange_pair& pair, const asset& quantity, uint64_t ask_id, uint64_t bid_id, uint64_t price, uint64_t volume) {
    eosio_assert(quantity.amount > 0, "fee should be positive");
    auto partner_fee = quantity/2;

    string memo;
    get_fee_info(memo, pair.id, ask_id, bid_id, price, volume);

    if (partner_fee.amount > 0) {
        do_transfer(pair.base.contract, _self, pair.manager, partner_fee, memo);
    }
    // TODO: we can keep the fee and send to fee_account in batch mode
    // benefit: can save cpu time
    // drawback: hard to check ledge and possible loss of token in case of attack
    auto system_fee = quantity - partner_fee;
    eosio_assert(system_fee.amount > 0, "system fee should be positive");
    do_transfer(pair.base.contract, _self, _gstate.fee_account, system_fee, memo);
}

// Get fee info string, outputs like '*fee:65536:ask_id:bid_id:price:volume*'
void youdex::get_fee_info(string& info, uint64_t pair_id, uint64_t ask_id, uint64_t bid_id, uint64_t price, uint64_t volume) {
    char buf[100]; 
    char* q = buf;

    *q++ = '*'; *q++ = 'f'; *q++ = 'e'; *q++ = 'e';
    *q++ = ':';
    q += print_ll(q, pair_id, 20);
    *q++ = ':';
    q += print_ll(q, ask_id, 20);
    *q++ = ':';
    q += print_ll(q, bid_id, 20);
    *q++ = ':';
    q += print_ll(q, price, 20);
    *q++ = ':';
    q += print_ll(q, volume, 20);
    *q++ = '*'; *q++ = '\0';

    info = string(buf);
}

void youdex::do_transfer(account_name contract, account_name from, account_name to, const asset& quantity, string memo) {
    // Notice that it requres authorize eosio.code firstly
    if (quantity.amount > 0) {
        action(
            permission_level{ _self, N(active) },
            contract, N(transfer),
            std::make_tuple(from, to, quantity, memo)
        ).send();
    } else {
        print("try to transfer 0 token with ", memo, "\n");
    }
}