#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace addition {

// REAL arithmetic-circuit building block: R1CS constraint satisfaction over a
// prime field. This is NOT zero-knowledge and NOT a SNARK. Evaluating that a
// witness satisfies constraints reveals the witness to the evaluator.
// Label: constraint_check_not_zk. See docs/ZK_CIRCUIT_V1.md.

inline constexpr const char* kZkR1csHonestyLabel = "constraint_check_not_zk";
inline constexpr const char* kZkR1csBackendId = "r1cs_field_evaluator_v1";

// 31-bit Mersenne prime: intermediates fit in uint64 for mul-then-reduce.
inline constexpr std::uint64_t kZkR1csFieldPrime = 2147483647ULL; // 2^31 - 1

class ZkFieldElem {
public:
    ZkFieldElem() : v_(0) {}
    explicit ZkFieldElem(std::uint64_t raw);

    static ZkFieldElem from_u64(std::uint64_t raw);
    static ZkFieldElem zero();
    static ZkFieldElem one();

    std::uint64_t value() const { return v_; }

    ZkFieldElem operator+(const ZkFieldElem& o) const;
    ZkFieldElem operator-(const ZkFieldElem& o) const;
    ZkFieldElem operator*(const ZkFieldElem& o) const;
    bool operator==(const ZkFieldElem& o) const { return v_ == o.v_; }
    bool operator!=(const ZkFieldElem& o) const { return v_ != o.v_; }

private:
    std::uint64_t v_;
};

// Sparse linear combination: sum coeff_i * z[index_i]
struct ZkR1csLinear {
    std::vector<std::pair<std::size_t, ZkFieldElem>> terms;
};

struct ZkR1csConstraint {
    ZkR1csLinear A;
    ZkR1csLinear B;
    ZkR1csLinear C;
};

struct ZkR1csInstance {
    std::size_t num_vars{0}; // includes z[0]=1 constant wire
    std::vector<ZkR1csConstraint> constraints;
};

// Assignment z[0..num_vars). z[0] must be 1 for a well-formed witness.
using ZkR1csAssignment = std::vector<ZkFieldElem>;

ZkFieldElem zk_r1cs_eval_linear(const ZkR1csLinear& lin, const ZkR1csAssignment& z);

bool zk_r1cs_constraint_satisfied(const ZkR1csConstraint& c,
                                  const ZkR1csAssignment& z,
                                  std::string& error);

// Evaluate all constraints. Returns false if any fail or sizes mismatch.
bool zk_r1cs_evaluate(const ZkR1csInstance& inst,
                      const ZkR1csAssignment& z,
                      std::string& error,
                      std::size_t& failed_index);

// Toy algebraic circuit already in the schema spirit: value conservation
// in_value == out_value + change  (linear R1CS: (in) * (1) = out + change).
// Variables: z[0]=1, z[1]=in, z[2]=out, z[3]=change.
ZkR1csInstance zk_r1cs_value_conservation_circuit();

bool zk_r1cs_value_conservation_witness(std::uint64_t in_value,
                                        std::uint64_t out_value,
                                        std::uint64_t change,
                                        ZkR1csAssignment& z_out,
                                        std::string& error);

// Toy multiplicative circuit: public y, witness x with x*x = y.
// Variables: z[0]=1, z[1]=x, z[2]=y.
ZkR1csInstance zk_r1cs_square_circuit();

bool zk_r1cs_square_witness(std::uint64_t x,
                            ZkR1csAssignment& z_out,
                            ZkFieldElem& public_y_out,
                            std::string& error);

} // namespace addition
