#include "addition/token_engine.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <sstream>

namespace addition {

namespace {

constexpr const char* kPoolPrefix = "pool:";

} // namespace

bool TokenEngine::ordered_pair(const std::string& a, const std::string& b, std::string& t0, std::string& t1) {
    if (a.empty() || b.empty() || a == b) {
        return false;
    }
    if (a < b) {
        t0 = a;
        t1 = b;
    } else {
        t0 = b;
        t1 = a;
    }
    return true;
}

std::uint64_t TokenEngine::isqrt_u64(std::uint64_t x) {
    if (x == 0) {
        return 0;
    }
    std::uint64_t r = x;
    std::uint64_t prev = 0;
    while (r != prev) {
        prev = r;
        r = (r + x / r) / 2;
    }
    while ((r + 1) <= (std::numeric_limits<std::uint64_t>::max() / (r + 1)) && (r + 1) * (r + 1) <= x) {
        ++r;
    }
    while (r * r > x) {
        --r;
    }
    return r;
}

std::string TokenEngine::pool_key(const std::string& token_a, const std::string& token_b) const {
    std::string t0;
    std::string t1;
    if (!ordered_pair(token_a, token_b, t0, t1)) {
        return {};
    }
    return t0 + "|" + t1;
}

bool TokenEngine::create_token(const std::string& symbol,
                               const std::string& owner,
                               std::uint64_t max_supply,
                               std::uint64_t initial_mint,
                               std::string& error) {
    return create_token_ex(symbol,
                           symbol,
                           owner,
                           max_supply,
                           initial_mint,
                           18,
                           false,
                           "",
                           0,
                           error);
}

bool TokenEngine::create_token_ex(const std::string& symbol,
                                  const std::string& name,
                                  const std::string& owner,
                                  std::uint64_t max_supply,
                                  std::uint64_t initial_mint,
                                  std::uint32_t decimals,
                                  bool burnable,
                                  const std::string& dev_wallet,
                                  std::uint64_t dev_allocation,
                                  std::string& error) {
    if (symbol.empty() || owner.empty()) {
        error = "symbol/owner empty";
        return false;
    }
    if (name.empty()) {
        error = "name empty";
        return false;
    }
    if (decimals > 30) {
        error = "decimals too high";
        return false;
    }
    if (max_supply == 0) {
        error = "max supply must be > 0";
        return false;
    }
    if (dev_allocation > max_supply) {
        error = "dev allocation exceeds max supply";
        return false;
    }
    if (dev_allocation > 0 && dev_wallet.empty()) {
        error = "dev wallet required when dev allocation > 0";
        return false;
    }
    if (initial_mint > (std::numeric_limits<std::uint64_t>::max() - dev_allocation)) {
        error = "initial + dev allocation overflow";
        return false;
    }
    if (initial_mint + dev_allocation > max_supply) {
        error = "initial + dev allocation exceeds max supply";
        return false;
    }
    if (initial_mint > max_supply) {
        error = "initial mint exceeds max supply";
        return false;
    }
    if (tokens_.find(symbol) != tokens_.end()) {
        error = "token already exists";
        return false;
    }

    Token t{};
    t.name = name;
    t.owner = owner;
    t.decimals = decimals;
    t.burnable = burnable;
    t.dev_wallet = dev_wallet;
    t.dev_allocation = dev_allocation;
    t.max_supply = max_supply;
    t.total_supply = initial_mint + dev_allocation;
    if (initial_mint > 0) {
        t.balances[owner] = initial_mint;
    }
    if (dev_allocation > 0) {
        t.balances[dev_wallet] += dev_allocation;
    }

    tokens_[symbol] = std::move(t);
    return true;
}

bool TokenEngine::mint(const std::string& symbol,
                       const std::string& caller,
                       const std::string& to,
                       std::uint64_t amount,
                       std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (caller != it->second.owner) {
        error = "only owner can mint";
        return false;
    }
    if (to.empty() || amount == 0) {
        error = "invalid mint params";
        return false;
    }
    if (it->second.total_supply > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        error = "token supply overflow";
        return false;
    }
    if (it->second.total_supply + amount > it->second.max_supply) {
        error = "token max supply exceeded";
        return false;
    }
    if (it->second.balances[to] > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        error = "recipient balance overflow";
        return false;
    }

    it->second.total_supply += amount;
    it->second.balances[to] += amount;
    return true;
}

bool TokenEngine::transfer(const std::string& symbol,
                           const std::string& from,
                           const std::string& to,
                           std::uint64_t amount,
                           std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (from.empty() || to.empty() || amount == 0) {
        error = "invalid transfer params";
        return false;
    }
    if (it->second.paused) {
        error = "token transfers paused";
        return false;
    }
    if (it->second.blacklist.count(from) || it->second.blacklist.count(to)) {
        error = "wallet blacklisted";
        return false;
    }

    if (it->second.max_tx_amount > 0 && amount > it->second.max_tx_amount) {
        error = "amount exceeds max_tx_amount";
        return false;
    }

    const bool fee_exempt = it->second.fee_exempt.count(from) || it->second.fee_exempt.count(to);

    const std::uint64_t fee_denom = 10000;
    const std::uint64_t transfer_fee = fee_exempt ? 0 : (amount * it->second.transfer_fee_bps) / fee_denom;
    const std::uint64_t burn_fee = fee_exempt ? 0 : (amount * it->second.burn_fee_bps) / fee_denom;
    if (transfer_fee + burn_fee > amount) {
        error = "invalid fee policy";
        return false;
    }
    const std::uint64_t receive_amount = amount - transfer_fee - burn_fee;

    auto& from_bal = it->second.balances[from];
    if (from_bal < amount) {
        error = "insufficient token balance";
        return false;
    }

    from_bal -= amount;
    if (it->second.balances[to] > (std::numeric_limits<std::uint64_t>::max() - receive_amount)) {
        from_bal += amount;
        error = "recipient balance overflow";
        return false;
    }
    it->second.balances[to] += receive_amount;

    if (it->second.max_wallet_amount > 0 && it->second.balances[to] > it->second.max_wallet_amount) {
        from_bal += amount;
        it->second.balances[to] -= receive_amount;
        error = "recipient exceeds max_wallet_amount";
        return false;
    }

    if (transfer_fee > 0 && !it->second.treasury_wallet.empty()) {
        if (it->second.balances[it->second.treasury_wallet] > (std::numeric_limits<std::uint64_t>::max() - transfer_fee)) {
            from_bal += amount;
            it->second.balances[to] -= receive_amount;
            error = "treasury balance overflow";
            return false;
        }
        it->second.balances[it->second.treasury_wallet] += transfer_fee;
    }
    if (burn_fee > 0) {
        if (it->second.total_supply < burn_fee) {
            error = "burn fee exceeds supply";
            return false;
        }
        it->second.total_supply -= burn_fee;
    }
    return true;
}

bool TokenEngine::burn(const std::string& symbol,
                       const std::string& from,
                       std::uint64_t amount,
                       std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (!it->second.burnable) {
        error = "token is not burnable";
        return false;
    }
    if (from.empty() || amount == 0) {
        error = "invalid burn params";
        return false;
    }
    auto& bal = it->second.balances[from];
    if (bal < amount) {
        error = "insufficient token balance";
        return false;
    }
    bal -= amount;
    it->second.total_supply -= amount;
    return true;
}

bool TokenEngine::set_policy(const std::string& symbol,
                             const std::string& caller,
                             const std::string& treasury_wallet,
                             std::uint64_t transfer_fee_bps,
                             std::uint64_t burn_fee_bps,
                             bool paused,
                             std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (caller != it->second.owner) {
        error = "only owner can set policy";
        return false;
    }
    if (transfer_fee_bps + burn_fee_bps > 2000) {
        error = "total fee bps too high (>2000)";
        return false;
    }

    it->second.treasury_wallet = treasury_wallet;
    it->second.transfer_fee_bps = transfer_fee_bps;
    it->second.burn_fee_bps = burn_fee_bps;
    it->second.paused = paused;
    return true;
}

bool TokenEngine::set_blacklist(const std::string& symbol,
                                const std::string& caller,
                                const std::string& wallet,
                                bool blocked,
                                std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (caller != it->second.owner) {
        error = "only owner can set blacklist";
        return false;
    }
    if (wallet.empty()) {
        error = "wallet empty";
        return false;
    }

    if (blocked) {
        it->second.blacklist.insert(wallet);
    } else {
        it->second.blacklist.erase(wallet);
    }
    return true;
}

bool TokenEngine::set_fee_exempt(const std::string& symbol,
                                 const std::string& caller,
                                 const std::string& wallet,
                                 bool exempt,
                                 std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (caller != it->second.owner) {
        error = "only owner can set fee exemption";
        return false;
    }
    if (wallet.empty()) {
        error = "wallet empty";
        return false;
    }

    if (exempt) {
        it->second.fee_exempt.insert(wallet);
    } else {
        it->second.fee_exempt.erase(wallet);
    }
    return true;
}

bool TokenEngine::set_limits(const std::string& symbol,
                             const std::string& caller,
                             std::uint64_t max_tx_amount,
                             std::uint64_t max_wallet_amount,
                             std::string& error) {
    auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    if (caller != it->second.owner) {
        error = "only owner can set limits";
        return false;
    }
    it->second.max_tx_amount = max_tx_amount;
    it->second.max_wallet_amount = max_wallet_amount;
    return true;
}

bool TokenEngine::token_info(const std::string& symbol, std::string& out, std::string& error) const {
    const auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        error = "token not found";
        return false;
    }
    std::ostringstream oss;
    oss << "symbol=" << symbol
        << " name=" << it->second.name
        << " owner=" << it->second.owner
        << " decimals=" << it->second.decimals
        << " burnable=" << (it->second.burnable ? "true" : "false")
        << " paused=" << (it->second.paused ? "true" : "false")
        << " transfer_fee_bps=" << it->second.transfer_fee_bps
        << " burn_fee_bps=" << it->second.burn_fee_bps
        << " treasury_wallet=" << it->second.treasury_wallet
        << " max_tx_amount=" << it->second.max_tx_amount
        << " max_wallet_amount=" << it->second.max_wallet_amount
        << " max_supply=" << it->second.max_supply
        << " total_supply=" << it->second.total_supply
        << " dev_wallet=" << it->second.dev_wallet
        << " dev_allocation=" << it->second.dev_allocation
        << " blacklist_size=" << it->second.blacklist.size()
        << " fee_exempt_size=" << it->second.fee_exempt.size();
    out = oss.str();
    return true;
}

std::uint64_t TokenEngine::balance_of(const std::string& symbol, const std::string& owner) const {
    const auto it = tokens_.find(symbol);
    if (it == tokens_.end()) {
        return 0;
    }
    const auto jt = it->second.balances.find(owner);
    return jt == it->second.balances.end() ? 0ULL : jt->second;
}

bool TokenEngine::mint_nft(const std::string& collection,
                           const std::string& token_id,
                           const std::string& owner,
                           const std::string& metadata,
                           std::string& error) {
    if (collection.empty() || token_id.empty() || owner.empty()) {
        error = "invalid nft mint params";
        return false;
    }

    auto& col = nfts_[collection];
    if (col.find(token_id) != col.end()) {
        error = "nft already exists";
        return false;
    }

    col[token_id] = NftAsset{owner, metadata};
    return true;
}

bool TokenEngine::transfer_nft(const std::string& collection,
                               const std::string& token_id,
                               const std::string& from,
                               const std::string& to,
                               std::string& error) {
    auto cit = nfts_.find(collection);
    if (cit == nfts_.end()) {
        error = "collection not found";
        return false;
    }

    auto ait = cit->second.find(token_id);
    if (ait == cit->second.end()) {
        error = "nft not found";
        return false;
    }

    if (from.empty() || to.empty()) {
        error = "invalid nft transfer params";
        return false;
    }

    if (ait->second.owner != from) {
        error = "not nft owner";
        return false;
    }

    ait->second.owner = to;
    return true;
}

std::string TokenEngine::nft_owner_of(const std::string& collection, const std::string& token_id) const {
    const auto cit = nfts_.find(collection);
    if (cit == nfts_.end()) {
        return {};
    }
    const auto ait = cit->second.find(token_id);
    if (ait == cit->second.end()) {
        return {};
    }
    return ait->second.owner;
}

bool TokenEngine::create_pool(const std::string& token_a,
                              const std::string& token_b,
                              std::uint64_t fee_bps,
                              std::string& error) {
    std::string t0;
    std::string t1;
    if (!ordered_pair(token_a, token_b, t0, t1)) {
        error = "invalid token pair";
        return false;
    }
    if (tokens_.find(t0) == tokens_.end() || tokens_.find(t1) == tokens_.end()) {
        error = "pool tokens must exist";
        return false;
    }
    if (fee_bps == 0 || fee_bps >= 10000) {
        error = "fee_bps must be in [1,9999]";
        return false;
    }

    const auto key = t0 + "|" + t1;
    if (pools_.find(key) != pools_.end()) {
        error = "pool already exists";
        return false;
    }

    pools_[key] = Pool{t0, t1, 0, 0, fee_bps, 0, {}};
    return true;
}

bool TokenEngine::add_liquidity(const std::string& token_a,
                                const std::string& token_b,
                                const std::string& provider,
                                std::uint64_t amount_a,
                                std::uint64_t amount_b,
                                std::string& error) {
    if (provider.empty() || amount_a == 0 || amount_b == 0) {
        error = "invalid liquidity params";
        return false;
    }

    const auto key = pool_key(token_a, token_b);
    auto pit = pools_.find(key);
    if (pit == pools_.end()) {
        error = "pool not found";
        return false;
    }
    auto& p = pit->second;

    const bool a_is_0 = (token_a == p.token0);
    const std::uint64_t add0 = a_is_0 ? amount_a : amount_b;
    const std::uint64_t add1 = a_is_0 ? amount_b : amount_a;

    auto& b0 = tokens_[p.token0].balances[provider];
    auto& b1 = tokens_[p.token1].balances[provider];
    if (b0 < add0 || b1 < add1) {
        error = "insufficient token balance for liquidity";
        return false;
    }

    std::uint64_t minted_lp = 0;
    if (p.lp_total_supply == 0) {
        if (add0 > (std::numeric_limits<std::uint64_t>::max() / add1)) {
            error = "liquidity product overflow";
            return false;
        }
        minted_lp = isqrt_u64(add0 * add1);
        if (minted_lp == 0) {
            error = "liquidity too small";
            return false;
        }
    } else {
        if (p.reserve0 == 0 || p.reserve1 == 0) {
            error = "invalid pool reserves";
            return false;
        }
        if (add0 > (std::numeric_limits<std::uint64_t>::max() / p.reserve1) ||
            p.reserve0 > (std::numeric_limits<std::uint64_t>::max() / add1)) {
            error = "liquidity ratio overflow";
            return false;
        }
        if (add0 * p.reserve1 != add1 * p.reserve0) {
            error = "liquidity amounts must match pool ratio";
            return false;
        }
        const auto lp0 = (add0 * p.lp_total_supply) / p.reserve0;
        const auto lp1 = (add1 * p.lp_total_supply) / p.reserve1;
        minted_lp = std::min(lp0, lp1);
        if (minted_lp == 0) {
            error = "liquidity too small";
            return false;
        }
    }

    b0 -= add0;
    b1 -= add1;
    if (p.reserve0 > (std::numeric_limits<std::uint64_t>::max() - add0) ||
        p.reserve1 > (std::numeric_limits<std::uint64_t>::max() - add1) ||
        p.lp_total_supply > (std::numeric_limits<std::uint64_t>::max() - minted_lp) ||
        p.lp_balances[provider] > (std::numeric_limits<std::uint64_t>::max() - minted_lp)) {
        b0 += add0;
        b1 += add1;
        error = "pool state overflow";
        return false;
    }
    p.reserve0 += add0;
    p.reserve1 += add1;
    p.lp_total_supply += minted_lp;
    p.lp_balances[provider] += minted_lp;
    return true;
}

bool TokenEngine::remove_liquidity(const std::string& token_a,
                                   const std::string& token_b,
                                   const std::string& provider,
                                   std::uint64_t lp_amount,
                                   std::uint64_t& out_a,
                                   std::uint64_t& out_b,
                                   std::string& error) {
    out_a = 0;
    out_b = 0;
    if (provider.empty() || lp_amount == 0) {
        error = "invalid remove params";
        return false;
    }

    const auto key = pool_key(token_a, token_b);
    auto pit = pools_.find(key);
    if (pit == pools_.end()) {
        error = "pool not found";
        return false;
    }
    auto& p = pit->second;

    auto& lp_bal = p.lp_balances[provider];
    if (lp_bal < lp_amount || p.lp_total_supply == 0) {
        error = "insufficient lp balance";
        return false;
    }

    const auto take0 = (p.reserve0 * lp_amount) / p.lp_total_supply;
    const auto take1 = (p.reserve1 * lp_amount) / p.lp_total_supply;
    if (take0 == 0 || take1 == 0) {
        error = "lp amount too small";
        return false;
    }

    lp_bal -= lp_amount;
    p.lp_total_supply -= lp_amount;
    p.reserve0 -= take0;
    p.reserve1 -= take1;

    if (tokens_[p.token0].balances[provider] > (std::numeric_limits<std::uint64_t>::max() - take0) ||
        tokens_[p.token1].balances[provider] > (std::numeric_limits<std::uint64_t>::max() - take1)) {
        lp_bal += lp_amount;
        p.lp_total_supply += lp_amount;
        p.reserve0 += take0;
        p.reserve1 += take1;
        error = "provider balance overflow";
        return false;
    }
    tokens_[p.token0].balances[provider] += take0;
    tokens_[p.token1].balances[provider] += take1;

    if (token_a == p.token0) {
        out_a = take0;
        out_b = take1;
    } else {
        out_a = take1;
        out_b = take0;
    }
    return true;
}

bool TokenEngine::quote_exact_in(const std::string& token_in,
                                 const std::string& token_out,
                                 std::uint64_t amount_in,
                                 std::uint64_t& amount_out,
                                 std::string& error) const {
    amount_out = 0;
    if (amount_in == 0 || token_in.empty() || token_out.empty() || token_in == token_out) {
        error = "invalid quote params";
        return false;
    }
    const auto key = pool_key(token_in, token_out);
    const auto pit = pools_.find(key);
    if (pit == pools_.end()) {
        error = "pool not found";
        return false;
    }
    const auto& p = pit->second;

    const bool in_is_0 = (token_in == p.token0);
    const std::uint64_t reserve_in = in_is_0 ? p.reserve0 : p.reserve1;
    const std::uint64_t reserve_out = in_is_0 ? p.reserve1 : p.reserve0;
    if (reserve_in == 0 || reserve_out == 0) {
        error = "pool has no liquidity";
        return false;
    }

    const std::uint64_t fee_denom = 10000;
    const std::uint64_t fee_multiplier = (fee_denom - p.fee_bps);
    if (amount_in > (std::numeric_limits<std::uint64_t>::max() / fee_multiplier)) {
        error = "quote overflow";
        return false;
    }
    const std::uint64_t amount_in_with_fee = amount_in * fee_multiplier;
    if (amount_in_with_fee == 0) {
        error = "amount too small after fee";
        return false;
    }

    if (amount_in_with_fee > (std::numeric_limits<std::uint64_t>::max() / reserve_out)) {
        error = "quote overflow";
        return false;
    }
    const std::uint64_t num = amount_in_with_fee * reserve_out;
    if (reserve_in > (std::numeric_limits<std::uint64_t>::max() / fee_denom)) {
        error = "quote overflow";
        return false;
    }
    const std::uint64_t base_den = reserve_in * fee_denom;
    if (base_den > (std::numeric_limits<std::uint64_t>::max() - amount_in_with_fee)) {
        error = "quote overflow";
        return false;
    }
    const std::uint64_t den = base_den + amount_in_with_fee;
    if (den == 0) {
        error = "quote denominator zero";
        return false;
    }
    amount_out = num / den;
    if (amount_out == 0 || amount_out >= reserve_out) {
        error = "insufficient output amount";
        return false;
    }
    return true;
}

bool TokenEngine::swap_exact_in(const std::string& token_in,
                                const std::string& token_out,
                                const std::string& trader,
                                std::uint64_t amount_in,
                                std::uint64_t min_out,
                                std::uint64_t& amount_out,
                                std::string& error) {
    amount_out = 0;
    if (trader.empty()) {
        error = "trader empty";
        return false;
    }

    if (!quote_exact_in(token_in, token_out, amount_in, amount_out, error)) {
        return false;
    }
    if (amount_out < min_out) {
        error = "slippage exceeded";
        return false;
    }

    const auto key = pool_key(token_in, token_out);
    auto pit = pools_.find(key);
    auto& p = pit->second;

    auto& in_bal = tokens_[token_in].balances[trader];
    if (in_bal < amount_in) {
        error = "insufficient input token balance";
        return false;
    }

    const bool in_is_0 = (token_in == p.token0);
    if (in_is_0) {
        if (p.reserve1 <= amount_out) {
            error = "insufficient pool reserve";
            return false;
        }
        in_bal -= amount_in;
        if (tokens_[token_out].balances[trader] > (std::numeric_limits<std::uint64_t>::max() - amount_out) ||
            p.reserve0 > (std::numeric_limits<std::uint64_t>::max() - amount_in)) {
            in_bal += amount_in;
            error = "swap state overflow";
            return false;
        }
        tokens_[token_out].balances[trader] += amount_out;
        p.reserve0 += amount_in;
        p.reserve1 -= amount_out;
    } else {
        if (p.reserve0 <= amount_out) {
            error = "insufficient pool reserve";
            return false;
        }
        in_bal -= amount_in;
        if (tokens_[token_out].balances[trader] > (std::numeric_limits<std::uint64_t>::max() - amount_out) ||
            p.reserve1 > (std::numeric_limits<std::uint64_t>::max() - amount_in)) {
            in_bal += amount_in;
            error = "swap state overflow";
            return false;
        }
        tokens_[token_out].balances[trader] += amount_out;
        p.reserve1 += amount_in;
        p.reserve0 -= amount_out;
    }

    return true;
}

bool TokenEngine::pool_info(const std::string& token_a,
                            const std::string& token_b,
                            std::uint64_t& reserve_a,
                            std::uint64_t& reserve_b,
                            std::uint64_t& fee_bps,
                            std::uint64_t& lp_total_supply,
                            std::string& error) const {
    reserve_a = 0;
    reserve_b = 0;
    fee_bps = 0;
    lp_total_supply = 0;

    const auto key = pool_key(token_a, token_b);
    const auto pit = pools_.find(key);
    if (pit == pools_.end()) {
        error = "pool not found";
        return false;
    }
    const auto& p = pit->second;
    fee_bps = p.fee_bps;
    lp_total_supply = p.lp_total_supply;

    if (token_a == p.token0) {
        reserve_a = p.reserve0;
        reserve_b = p.reserve1;
    } else {
        reserve_a = p.reserve1;
        reserve_b = p.reserve0;
    }
    return true;
}

bool TokenEngine::quote_route_exact_in(const std::vector<std::string>& route,
                                       std::uint64_t amount_in,
                                       std::uint64_t& amount_out,
                                       std::string& error) const {
    amount_out = 0;
    if (route.size() < 2) {
        error = "route must contain at least 2 tokens";
        return false;
    }
    if (amount_in == 0) {
        error = "amount_in must be > 0";
        return false;
    }

    std::uint64_t running = amount_in;
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        std::uint64_t hop_out = 0;
        if (!quote_exact_in(route[i], route[i + 1], running, hop_out, error)) {
            error = "route hop " + std::to_string(i) + " failed: " + error;
            return false;
        }
        running = hop_out;
    }

    amount_out = running;
    return true;
}

bool TokenEngine::swap_route_exact_in(const std::vector<std::string>& route,
                                      const std::string& trader,
                                      std::uint64_t amount_in,
                                      std::uint64_t min_out,
                                      std::uint64_t& amount_out,
                                      std::string& error) {
    amount_out = 0;
    if (trader.empty()) {
        error = "trader empty";
        return false;
    }

    std::uint64_t quoted = 0;
    if (!quote_route_exact_in(route, amount_in, quoted, error)) {
        return false;
    }
    if (quoted < min_out) {
        error = "slippage exceeded on route";
        return false;
    }

    if (tokens_[route.front()].balances[trader] < amount_in) {
        error = "insufficient input token balance";
        return false;
    }

    const auto pools_snapshot = pools_;
    const auto tokens_snapshot = tokens_;

    std::uint64_t running_in = amount_in;
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        const auto& in_tok = route[i];
        const auto& out_tok = route[i + 1];
        std::uint64_t hop_out = 0;
        const std::uint64_t hop_min = (i + 2 == route.size()) ? min_out : 1;
        if (!swap_exact_in(in_tok, out_tok, trader, running_in, hop_min, hop_out, error)) {
            pools_ = pools_snapshot;
            tokens_ = tokens_snapshot;
            error = "route swap hop " + std::to_string(i) + " failed: " + error;
            return false;
        }
        running_in = hop_out;
    }

    amount_out = running_in;
    return true;
}

bool TokenEngine::best_route_exact_in(const std::string& token_in,
                                      const std::string& token_out,
                                      std::uint64_t amount_in,
                                      std::size_t max_hops,
                                      std::vector<std::string>& route,
                                      std::uint64_t& amount_out,
                                      std::string& error) const {
    route.clear();
    amount_out = 0;
    if (token_in.empty() || token_out.empty() || token_in == token_out) {
        error = "invalid token pair";
        return false;
    }
    if (amount_in == 0) {
        error = "amount_in must be > 0";
        return false;
    }
    if (max_hops < 1) {
        max_hops = 1;
    }
    if (max_hops > 4) {
        max_hops = 4;
    }

    std::vector<std::string> all_tokens;
    all_tokens.reserve(tokens_.size());
    for (const auto& [sym, _] : tokens_) {
        all_tokens.push_back(sym);
    }

    std::uint64_t best_out = 0;
    std::uint64_t best_score = 0;
    std::uint64_t best_bottleneck_liquidity = 0;
    std::vector<std::string> best_route;

    std::vector<std::string> current{token_in};
    std::unordered_map<std::string, bool> used;
    used[token_in] = true;

    std::function<void(std::size_t, std::uint64_t)> dfs = [&](std::size_t depth, std::uint64_t running_amount) {
        const auto& cur = current.back();
        if (cur == token_out && current.size() >= 2) {
            const std::uint64_t q = running_amount;
            constexpr std::uint64_t kHopPenaltyBps = 7;
            const std::size_t hop_count = current.size() - 1;
            const std::uint64_t total_penalty = std::min<std::uint64_t>(9999, kHopPenaltyBps * (hop_count - 1));
            const std::uint64_t route_score = (q * (10000 - total_penalty)) / 10000;

            std::uint64_t bottleneck_liquidity = std::numeric_limits<std::uint64_t>::max();
            for (std::size_t i = 0; i + 1 < current.size(); ++i) {
                const auto key = pool_key(current[i], current[i + 1]);
                const auto pit = pools_.find(key);
                if (pit == pools_.end()) {
                    bottleneck_liquidity = 0;
                    break;
                }
                const auto& p = pit->second;
                const bool in_is_0 = (current[i] == p.token0);
                const std::uint64_t reserve_in = in_is_0 ? p.reserve0 : p.reserve1;
                bottleneck_liquidity = std::min(bottleneck_liquidity, reserve_in);
            }

            const bool better = (route_score > best_score) ||
                                (route_score == best_score && q > best_out) ||
                                (route_score == best_score && q == best_out &&
                                 bottleneck_liquidity > best_bottleneck_liquidity);
            if (better) {
                best_out = q;
                best_score = route_score;
                best_bottleneck_liquidity = bottleneck_liquidity;
                best_route = current;
            }
            return;
        }

        if (depth >= max_hops) {
            return;
        }

        for (const auto& nxt : all_tokens) {
            if (used[nxt]) {
                continue;
            }
            std::uint64_t hop_out = 0;
            std::string qerr;
            if (!quote_exact_in(cur, nxt, running_amount, hop_out, qerr)) {
                continue;
            }

            used[nxt] = true;
            current.push_back(nxt);
            dfs(depth + 1, hop_out);
            current.pop_back();
            used[nxt] = false;
        }
    };

    dfs(0, amount_in);

    if (best_route.empty() || best_out == 0) {
        error = "no viable route";
        return false;
    }

    route = std::move(best_route);
    amount_out = best_out;
    return true;
}

std::string TokenEngine::dump_state() const {
    std::ostringstream oss;

    for (const auto& [sym, t] : tokens_) {
        oss << "T|" << sym << '|' << t.name << '|' << t.owner << '|' << t.decimals << '|'
            << (t.burnable ? 1 : 0) << '|' << t.dev_wallet << '|' << t.dev_allocation << '|'
            << t.treasury_wallet << '|' << t.transfer_fee_bps << '|' << t.burn_fee_bps << '|'
            << (t.paused ? 1 : 0) << '|'
            << t.max_tx_amount << '|' << t.max_wallet_amount << '|'
            << t.max_supply << '|' << t.total_supply << '\n';
        for (const auto& [addr, bal] : t.balances) {
            oss << "B|" << sym << '|' << addr << '|' << bal << '\n';
        }
        for (const auto& w : t.blacklist) {
            oss << "X|" << sym << '|' << w << '\n';
        }
        for (const auto& w : t.fee_exempt) {
            oss << "E|" << sym << '|' << w << '\n';
        }
    }

    for (const auto& [col, assets] : nfts_) {
        for (const auto& [id, nft] : assets) {
            oss << "N|" << col << '|' << id << '|' << nft.owner << '|' << nft.metadata << '\n';
        }
    }

    for (const auto& [k, p] : pools_) {
        oss << "P|" << k << '|' << p.token0 << '|' << p.token1 << '|' << p.reserve0 << '|' << p.reserve1 << '|'
            << p.fee_bps << '|' << p.lp_total_supply << '\n';
        for (const auto& [owner, bal] : p.lp_balances) {
            oss << "L|" << k << '|' << owner << '|' << bal << '\n';
        }
    }

    return oss.str();
}

bool TokenEngine::load_state(const std::string& state, std::string& error) {
    tokens_.clear();
    nfts_.clear();
    pools_.clear();

    std::istringstream iss(state);
    for (std::string line; std::getline(iss, line);) {
        if (line.empty()) {
            continue;
        }

        std::istringstream ls(line);
        std::string tag;
        std::getline(ls, tag, '|');

        if (tag == "T") {
            std::vector<std::string> parts;
            for (std::string p; std::getline(ls, p, '|');) {
                parts.push_back(p);
            }

            if (parts.size() < 4) {
                error = "invalid token state line";
                return false;
            }

            Token t{};
            std::string sym;

            if (parts.size() >= 15) {
                // New format:
                // sym|name|owner|decimals|burnable|dev_wallet|dev_alloc|treasury|tfb|bfb|paused|max_tx|max_wallet|max|total
                sym = parts[0];
                t.name = parts[1].empty() ? parts[0] : parts[1];
                t.owner = parts[2];
                t.decimals = parts[3].empty() ? 18U : static_cast<std::uint32_t>(std::stoul(parts[3]));
                t.burnable = (parts[4] == "1");
                t.dev_wallet = parts[5];
                t.dev_allocation = parts[6].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[6]));
                t.treasury_wallet = parts[7];
                t.transfer_fee_bps = parts[8].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[8]));
                t.burn_fee_bps = parts[9].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[9]));
                t.paused = (parts[10] == "1");
                t.max_tx_amount = parts[11].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[11]));
                t.max_wallet_amount = parts[12].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[12]));
                t.max_supply = static_cast<std::uint64_t>(std::stoull(parts[13]));
                t.total_supply = static_cast<std::uint64_t>(std::stoull(parts[14]));
            } else if (parts.size() >= 13) {
                // Previous new format without limits:
                // sym|name|owner|decimals|burnable|dev_wallet|dev_alloc|treasury|tfb|bfb|paused|max|total
                sym = parts[0];
                t.name = parts[1].empty() ? parts[0] : parts[1];
                t.owner = parts[2];
                t.decimals = parts[3].empty() ? 18U : static_cast<std::uint32_t>(std::stoul(parts[3]));
                t.burnable = (parts[4] == "1");
                t.dev_wallet = parts[5];
                t.dev_allocation = parts[6].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[6]));
                t.treasury_wallet = parts[7];
                t.transfer_fee_bps = parts[8].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[8]));
                t.burn_fee_bps = parts[9].empty() ? 0ULL : static_cast<std::uint64_t>(std::stoull(parts[9]));
                t.paused = (parts[10] == "1");
                t.max_supply = static_cast<std::uint64_t>(std::stoull(parts[11]));
                t.total_supply = static_cast<std::uint64_t>(std::stoull(parts[12]));
            } else {
                // Legacy format:
                // sym|owner|max|total
                sym = parts[0];
                t.name = parts[0];
                t.owner = parts[1];
                t.max_supply = static_cast<std::uint64_t>(std::stoull(parts[2]));
                t.total_supply = static_cast<std::uint64_t>(std::stoull(parts[3]));
            }

            if (sym.empty() || t.owner.empty()) {
                error = "invalid token state line";
                return false;
            }
            tokens_[sym] = std::move(t);
        } else if (tag == "B") {
            std::string sym, addr, bal;
            std::getline(ls, sym, '|');
            std::getline(ls, addr, '|');
            std::getline(ls, bal);
            if (sym.empty() || addr.empty()) {
                error = "invalid token balance line";
                return false;
            }
            tokens_[sym].balances[addr] = static_cast<std::uint64_t>(std::stoull(bal));
        } else if (tag == "N") {
            std::string col, id, owner, meta;
            std::getline(ls, col, '|');
            std::getline(ls, id, '|');
            std::getline(ls, owner, '|');
            std::getline(ls, meta);
            if (col.empty() || id.empty() || owner.empty()) {
                error = "invalid nft line";
                return false;
            }
            nfts_[col][id] = NftAsset{owner, meta};
        } else if (tag == "P") {
            std::string key, t0, t1, r0, r1, fee, lps;
            std::getline(ls, key, '|');
            std::getline(ls, t0, '|');
            std::getline(ls, t1, '|');
            std::getline(ls, r0, '|');
            std::getline(ls, r1, '|');
            std::getline(ls, fee, '|');
            std::getline(ls, lps);
            if (key.empty() || t0.empty() || t1.empty()) {
                error = "invalid pool line";
                return false;
            }
            Pool p{};
            p.token0 = t0;
            p.token1 = t1;
            p.reserve0 = static_cast<std::uint64_t>(std::stoull(r0));
            p.reserve1 = static_cast<std::uint64_t>(std::stoull(r1));
            p.fee_bps = static_cast<std::uint64_t>(std::stoull(fee));
            p.lp_total_supply = static_cast<std::uint64_t>(std::stoull(lps));
            pools_[key] = std::move(p);
        } else if (tag == "L") {
            std::string key, owner, bal;
            std::getline(ls, key, '|');
            std::getline(ls, owner, '|');
            std::getline(ls, bal);
            if (key.empty() || owner.empty()) {
                error = "invalid lp line";
                return false;
            }
            pools_[key].lp_balances[owner] = static_cast<std::uint64_t>(std::stoull(bal));
        } else if (tag == "X") {
            std::string sym, wallet;
            std::getline(ls, sym, '|');
            std::getline(ls, wallet);
            if (sym.empty() || wallet.empty()) {
                error = "invalid blacklist line";
                return false;
            }
            tokens_[sym].blacklist.insert(wallet);
        } else if (tag == "E") {
            std::string sym, wallet;
            std::getline(ls, sym, '|');
            std::getline(ls, wallet);
            if (sym.empty() || wallet.empty()) {
                error = "invalid fee exempt line";
                return false;
            }
            tokens_[sym].fee_exempt.insert(wallet);
        }
    }

    return true;
}

} // namespace addition
