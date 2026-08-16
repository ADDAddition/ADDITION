#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace addition {

class TokenEngine {
public:
    bool create_token(const std::string& symbol,
                      const std::string& owner,
                      std::uint64_t max_supply,
                      std::uint64_t initial_mint,
                      std::string& error);
    bool mint(const std::string& symbol,
              const std::string& caller,
              const std::string& to,
              std::uint64_t amount,
              std::string& error);
    bool create_token_ex(const std::string& symbol,
                         const std::string& name,
                         const std::string& owner,
                         std::uint64_t max_supply,
                         std::uint64_t initial_mint,
                         std::uint32_t decimals,
                         bool burnable,
                         const std::string& dev_wallet,
                         std::uint64_t dev_allocation,
                         std::string& error);
    bool transfer(const std::string& symbol,
                  const std::string& from,
                  const std::string& to,
                  std::uint64_t amount,
                  std::string& error);
    bool burn(const std::string& symbol,
              const std::string& from,
              std::uint64_t amount,
              std::string& error);
    bool set_policy(const std::string& symbol,
                    const std::string& caller,
                    const std::string& treasury_wallet,
                    std::uint64_t transfer_fee_bps,
                    std::uint64_t burn_fee_bps,
                    bool paused,
                    std::string& error);
    bool set_blacklist(const std::string& symbol,
                       const std::string& caller,
                       const std::string& wallet,
                       bool blocked,
                       std::string& error);
    bool set_fee_exempt(const std::string& symbol,
                        const std::string& caller,
                        const std::string& wallet,
                        bool exempt,
                        std::string& error);
    bool set_limits(const std::string& symbol,
                    const std::string& caller,
                    std::uint64_t max_tx_amount,
                    std::uint64_t max_wallet_amount,
                    std::string& error);
    bool token_info(const std::string& symbol, std::string& out, std::string& error) const;
    std::uint64_t balance_of(const std::string& symbol, const std::string& owner) const;

    bool mint_nft(const std::string& collection,
                  const std::string& token_id,
                  const std::string& owner,
                  const std::string& metadata,
                  std::string& error);
    bool transfer_nft(const std::string& collection,
                      const std::string& token_id,
                      const std::string& from,
                      const std::string& to,
                      std::string& error);
    std::string nft_owner_of(const std::string& collection, const std::string& token_id) const;
    bool nft_info(const std::string& collection,
                  const std::string& token_id,
                  std::string& out,
                  std::string& error) const;

    bool create_pool(const std::string& token_a,
                     const std::string& token_b,
                     std::uint64_t fee_bps,
                     std::string& error);
    bool add_liquidity(const std::string& token_a,
                       const std::string& token_b,
                       const std::string& provider,
                       std::uint64_t amount_a,
                       std::uint64_t amount_b,
                       std::string& error);
    bool remove_liquidity(const std::string& token_a,
                          const std::string& token_b,
                          const std::string& provider,
                          std::uint64_t lp_amount,
                          std::uint64_t& out_a,
                          std::uint64_t& out_b,
                          std::string& error);
    bool swap_exact_in(const std::string& token_in,
                       const std::string& token_out,
                       const std::string& trader,
                       std::uint64_t amount_in,
                       std::uint64_t min_out,
                       std::uint64_t& amount_out,
                       std::string& error);
    bool quote_exact_in(const std::string& token_in,
                        const std::string& token_out,
                        std::uint64_t amount_in,
                        std::uint64_t& amount_out,
                        std::string& error) const;
    bool pool_info(const std::string& token_a,
                   const std::string& token_b,
                   std::uint64_t& reserve_a,
                   std::uint64_t& reserve_b,
                   std::uint64_t& fee_bps,
                   std::uint64_t& lp_total_supply,
                   std::string& error) const;
    bool quote_route_exact_in(const std::vector<std::string>& route,
                              std::uint64_t amount_in,
                              std::uint64_t& amount_out,
                              std::string& error) const;
    bool swap_route_exact_in(const std::vector<std::string>& route,
                             const std::string& trader,
                             std::uint64_t amount_in,
                             std::uint64_t min_out,
                             std::uint64_t& amount_out,
                             std::string& error);
    bool best_route_exact_in(const std::string& token_in,
                             const std::string& token_out,
                             std::uint64_t amount_in,
                             std::size_t max_hops,
                             std::vector<std::string>& route,
                             std::uint64_t& amount_out,
                             std::string& error) const;

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    struct Token {
        std::string name;
        std::string owner;
        std::uint32_t decimals{18};
        bool burnable{false};
        std::string dev_wallet;
        std::uint64_t dev_allocation{0};
        std::string treasury_wallet;
        std::uint64_t transfer_fee_bps{0};
        std::uint64_t burn_fee_bps{0};
        bool paused{false};
        std::uint64_t max_tx_amount{0};
        std::uint64_t max_wallet_amount{0};
        std::uint64_t max_supply{0};
        std::uint64_t total_supply{0};
        std::unordered_map<std::string, std::uint64_t> balances;
        std::unordered_set<std::string> blacklist;
        std::unordered_set<std::string> fee_exempt;
    };

    struct NftAsset {
        std::string owner;
        std::string metadata;
    };

    struct Pool {
        std::string token0;
        std::string token1;
        std::uint64_t reserve0{0};
        std::uint64_t reserve1{0};
        std::uint64_t fee_bps{30};
        std::uint64_t lp_total_supply{0};
        std::unordered_map<std::string, std::uint64_t> lp_balances;
    };

    std::string pool_key(const std::string& token_a, const std::string& token_b) const;
    static bool ordered_pair(const std::string& a, const std::string& b, std::string& t0, std::string& t1);
    static std::uint64_t isqrt_u64(std::uint64_t x);

    std::unordered_map<std::string, Token> tokens_;
    std::unordered_map<std::string, std::unordered_map<std::string, NftAsset>> nfts_;
    std::unordered_map<std::string, Pool> pools_;
};

} // namespace addition
