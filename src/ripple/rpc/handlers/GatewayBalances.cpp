//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/rpc/impl/AccountFromString.h>
#include <ripple/rpc/impl/LookupLedger.h>
#include <ripple/app/paths/RippleState.h>

namespace ripple {

// Query:
// 1) Specify ledger to query.
// 2) Specify issuer account (cold wallet) in "account" field.
// 3) Specify accounts that hold gateway assets (such as hot wallets)
//    using "hotwallet" field which should be either a string (if just
//    one wallet) or an array of strings (if more than one).

// Response:
// 1) Array, "obligations", indicating the total obligations of the
//    gateway in each currency. Obligations to specified hot wallets
//    are not counted here.
// 2) Object, "balances", indicating balances in each account
//    that holds gateway assets. (Those specified in the "hotwallet"
//    field.)
// 3) Object of "assets" indicating accounts that owe the gateway.
//    (Gateways typically do not hold positive balances. This is unusual.)

// gateway_balances [<ledger>] <account> [<howallet> [<hotwallet [...

Json::Value doGatewayBalances (RPC::Context& context)
{
    auto& params = context.params;

    // Get the current ledger
    Ledger::pointer ledger;
    Json::Value result (RPC::lookupLedger (params, ledger, context.netOps));

    if (!ledger)
        return result;

    if (!(params.isMember (jss::account) || params.isMember (jss::ident)))
        return RPC::missing_field_error (jss::account);

    std::string const strIdent (params.isMember (jss::account)
        ? params[jss::account].asString ()
        : params[jss::ident].asString ());

    int iIndex = 0;

    if (params.isMember (jss::account_index))
    {
        auto const& accountIndex = params[jss::account_index];
        if (!accountIndex.isUInt() && !accountIndex.isInt ())
            return RPC::invalid_field_message (jss::account_index);
        iIndex = accountIndex.asUInt ();
    }

    bool const bStrict = params.isMember (jss::strict) &&
            params[jss::strict].asBool ();

    // Get info on account.
    bool bIndex; // out param
    RippleAddress naAccount; // out param
    Json::Value jvAccepted (RPC::accountFromString (
        ledger, naAccount, bIndex, strIdent, iIndex, bStrict, context.netOps));

    if (!jvAccepted.empty ())
        return jvAccepted;

    context.loadType = Resource::feeHighBurdenRPC;

    result[jss::account] = naAccount.humanAccountID();
    auto accountID = naAccount.getAccountID();

    // Parse the specified hotwallet(s), if any
    std::set <Account> hotWallets;

    if (params.isMember ("hotwallet"))
    {
        Json::Value const& hw = params["hotwallet"];
        bool valid = true;

        auto addHotWallet = [&valid, &hotWallets](Json::Value const& j)
        {
            if (j.isString())
            {
                RippleAddress ra;
                if (! ra.setAccountPublic (j.asString ()) &&
                    ! ra.setAccountID (j.asString()))
                {
                    valid = false;
                }
                else
                    hotWallets.insert (ra.getAccountID ());
            }
            else
            {
                valid = false;
            }
        };

        if (hw.isArray())
        {
            for (unsigned i = 0; i < hw.size(); ++i)
                addHotWallet (hw[i]);
        }
        else if (hw.isString())
        {
            addHotWallet (hw);
        }
        else
        {
            valid = false;
        }

        if (! valid)
        {
            result[jss::error]   = "invalidHotWallet";
            return result;
        }

    }

    std::map <Currency, STAmount> sums;
    std::map <Account, std::vector <STAmount>> hotBalances;
    std::map <Account, std::vector <STAmount>> assets;

    // Traverse the cold wallet's trust lines
    ledger->visitAccountItems (accountID, [&](SLE::ref sle)
    {
        if (sle->getType() == ltRIPPLE_STATE)
        {
            RippleState rs (sle, accountID);

            int balSign = rs.getBalance().signum();
            if (balSign == 0)
                return;

            auto const& peer = rs.getAccountIDPeer();

            // Here, a negative balance means the cold wallet owes (normal)
            // A positive balance means the cold wallet has an asset (unusual)

            if (hotWallets.count (peer) > 0)
            {
                // This is a specified hot wallt
                hotBalances[peer].push_back (-rs.getBalance ());
            }
            else if (balSign > 0)
            {
                // This is a gateway asset
                assets[peer].push_back (rs.getBalance ());
            }
            else
            {
                // normal negative balance, obligation to customer
                auto& bal = sums[rs.getBalance().getCurrency()];
                if (bal == zero)
                {
                    // This is needed to set the currency code correctly
                    bal = -rs.getBalance();
                }
                else
                    bal -= rs.getBalance();
            }
        }
    });

    if (! sums.empty())
    {
        Json::Value& j = (result [jss::obligations] = Json::objectValue);
        for (auto const& e : sums)
        {
            j[to_string (e.first)] = e.second.getText ();
        }
    }

    if (! hotBalances.empty())
    {
        Json::Value& j = (result [jss::balances] = Json::objectValue);
        for (auto const& account : hotBalances)
        {
            Json::Value& balanceArray = (j[to_string (account.first)] = Json::arrayValue);
            for (auto const& balance : account.second)
            {
                Json::Value& entry = balanceArray.append (Json::objectValue);
                entry[jss::currency] = balance.getHumanCurrency ();
                entry[jss::value] = balance.getText();
            }
        }
    }

    if (! assets.empty())
    {
        Json::Value& j = (result [jss::assets] = Json::objectValue);

        for (auto const& account : assets)
        {
            Json::Value& balanceArray = (j[to_string (account.first)] = Json::arrayValue);
            for (auto const& balance : account.second)
            {
                Json::Value& entry = balanceArray.append (Json::objectValue);
                entry[jss::currency] = balance.getHumanCurrency ();
                entry[jss::value] = balance.getText();
            }
        }
    }

    return result;
}

} // ripple
