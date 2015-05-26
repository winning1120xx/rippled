//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_FIELDREADER_H_INCLUDED
#define RIPPLE_RPC_FIELDREADER_H_INCLUDED

#include <ripple/rpc/impl/AccountFromString.h>

namespace ripple {
namespace RPC {

class FieldReader
{
public:
    explicit FieldReader (Json::Value const& params)
            : params_ (params)
    {
    }

    template <class T>
    bool readOptional (T& value, Json::StaticString field)
    {
        auto& f = params_[field];
        return f.isNull() || readImpl <T> (value, field, f);
    }

    template <class T>
    bool read (T& value, Json::StaticString field)
    {
        auto& f = params_[field];
        if (!f.isNull())
            return readImpl <T> (value, field, f);
        error_ = missing_field_error (field);
        return false;
    }

    Json::Value error() const { return error_; }

    bool read (Ledger::pointer& result, NetworkOPs& netOps)
    {
        error_ = RPC::lookupLedger (params_, result, netOps);
        return ! error_;
    }

    /** Read an account from its public hash or account ID. */
    bool readAccount (Account& result, std::string const val)
    {
        RippleAddress ra;
        if (ra.setAccountPublic (val) || ra.setAccountID (val))
        {
            result = ra.getAccountID();
            return true;
        }

        error_ = rpcError (rpcACT_MALFORMED);
        return false;
    }

    /** Read an account from its public hash, account ID or regular seed. */
    bool readAccountAddress (RippleAddress& result,
                             Ledger::ref ledger,
                             NetworkOPs& netOps)
    {
        bool bIndex;
        bool strict = false;
        std::string name;
        if (! (readOptional (strict, jss::strict) &&
               read (name, jss::account)))
            return false;
        error_ = RPC::accountFromString (
            ledger, result, bIndex, name, 0, strict, netOps);
        return !error_;
    }

private:
    Json::Value const& params_;
    Json::Value error_;

    template <class T>
    bool readImpl (T& result, Json::StaticString field, Json::Value const& val);
};

template <>
bool FieldReader::readImpl <bool> (
    bool& result, Json::StaticString field, Json::Value const& value)
{
    if (!value.isBool())
        error_ = expected_field_error (field, "bool");
    result = value.asBool();
    return ! error_;
}

template <>
bool FieldReader::readImpl <std::string> (
    std::string& result, Json::StaticString field, Json::Value const& value)
{
    if (!value.isString())
        error_ = expected_field_error (field, "string");
    result = value.asString();
    return ! error_;
}

template <>
bool FieldReader::readImpl <Account> (
    Account& result, Json::StaticString field, Json::Value const& value)
{
    std::string account;
    return readImpl (account, field, value) && readAccount (result, account);
}

template <>
bool FieldReader::readImpl <std::vector <std::string>> (
    std::vector <std::string>& result, Json::StaticString field,
    Json::Value const& value)
{
    if (value.isString())
    {
        result.push_back (value.asString());
        return true;
    }

    if (!value.isArray())
    {
        error_ = expected_field_error (field, "list of strings");
        return false;
    }

    for (auto& v: value)
    {
        if (! v.isString()) {
            error_ = expected_field_error (field, "list of strings");
            return false;
        }
        result.push_back (v.asString());
    }
    return true;
}

using AccountSet = std::set <Account>;

template <>
bool FieldReader::readImpl <AccountSet> (
    AccountSet& result, Json::StaticString field,
    Json::Value const& value)
{
    std::vector<std::string> accounts;

    if (!readImpl (accounts, field, value))
        return false;

    for (auto a : accounts)
    {
        Account account;
        if (readAccount (account, a))
            result.insert (std::move (account));
        else
            return false;
    }
    return true;
}

} // RPC
} // ripple

#endif
