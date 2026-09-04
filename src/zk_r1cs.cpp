#include "addition/zk_r1cs.hpp"

namespace addition {
namespace {

std::uint64_t reduce(std::uint64_t x) {
    return x % kZkR1csFieldPrime;
}

} // namespace

ZkFieldElem::ZkFieldElem(std::uint64_t raw) : v_(reduce(raw)) {}

ZkFieldElem ZkFieldElem::from_u64(std::uint64_t raw) {
    return ZkFieldElem(raw);
}

ZkFieldElem ZkFieldElem::zero() {
    return ZkFieldElem(0);
}

ZkFieldElem ZkFieldElem::one() {
    return ZkFieldElem(1);
}

ZkFieldElem ZkFieldElem::operator+(const ZkFieldElem& o) const {
    return ZkFieldElem(v_ + o.v_);
}

ZkFieldElem ZkFieldElem::operator-(const ZkFieldElem& o) const {
    const std::uint64_t lhs = v_;
    const std::uint64_t rhs = o.v_;
    if (lhs >= rhs) {
        return ZkFieldElem(lhs - rhs);
    }
    return ZkFieldElem(kZkR1csFieldPrime - (rhs - lhs));
}

ZkFieldElem ZkFieldElem::operator*(const ZkFieldElem& o) const {
    return ZkFieldElem(v_ * o.v_);
}

ZkFieldElem zk_r1cs_eval_linear(const ZkR1csLinear& lin, const ZkR1csAssignment& z) {
    ZkFieldElem acc = ZkFieldElem::zero();
    for (const auto& term : lin.terms) {
        if (term.first >= z.size()) {
            continue;
        }
        acc = acc + (term.second * z[term.first]);
    }
    return acc;
}

bool zk_r1cs_constraint_satisfied(const ZkR1csConstraint& c,
                                  const ZkR1csAssignment& z,
                                  std::string& error) {
    for (const auto& term : c.A.terms) {
        if (term.first >= z.size()) {
            error = "r1cs: A index out of range";
            return false;
        }
    }
    for (const auto& term : c.B.terms) {
        if (term.first >= z.size()) {
            error = "r1cs: B index out of range";
            return false;
        }
    }
    for (const auto& term : c.C.terms) {
        if (term.first >= z.size()) {
            error = "r1cs: C index out of range";
            return false;
        }
    }
    const ZkFieldElem a = zk_r1cs_eval_linear(c.A, z);
    const ZkFieldElem b = zk_r1cs_eval_linear(c.B, z);
    const ZkFieldElem left = a * b;
    const ZkFieldElem right = zk_r1cs_eval_linear(c.C, z);
    if (left != right) {
        error = "r1cs: constraint not satisfied (constraint_check_not_zk)";
        return false;
    }
    error.clear();
    return true;
}

bool zk_r1cs_evaluate(const ZkR1csInstance& inst,
                      const ZkR1csAssignment& z,
                      std::string& error,
                      std::size_t& failed_index) {
    failed_index = static_cast<std::size_t>(-1);
    if (inst.num_vars == 0 || z.size() != inst.num_vars) {
        error = "r1cs: assignment size mismatch";
        return false;
    }
    if (z[0] != ZkFieldElem::one()) {
        error = "r1cs: z[0] must be 1";
        return false;
    }
    for (std::size_t i = 0; i < inst.constraints.size(); ++i) {
        if (!zk_r1cs_constraint_satisfied(inst.constraints[i], z, error)) {
            failed_index = i;
            return false;
        }
    }
    error.clear();
    return true;
}

ZkR1csInstance zk_r1cs_value_conservation_circuit() {
    // (in) * (1) = out + change
    // A: z[1], B: z[0], C: z[2] + z[3]
    ZkR1csInstance inst;
    inst.num_vars = 4;
    ZkR1csConstraint c;
    c.A.terms.push_back({1, ZkFieldElem::one()});
    c.B.terms.push_back({0, ZkFieldElem::one()});
    c.C.terms.push_back({2, ZkFieldElem::one()});
    c.C.terms.push_back({3, ZkFieldElem::one()});
    inst.constraints.push_back(std::move(c));
    return inst;
}

bool zk_r1cs_value_conservation_witness(std::uint64_t in_value,
                                        std::uint64_t out_value,
                                        std::uint64_t change,
                                        ZkR1csAssignment& z_out,
                                        std::string& error) {
    if (in_value >= kZkR1csFieldPrime || out_value >= kZkR1csFieldPrime ||
        change >= kZkR1csFieldPrime) {
        error = "r1cs value conservation: value out of field";
        return false;
    }
    if ((out_value + change) % kZkR1csFieldPrime != in_value % kZkR1csFieldPrime) {
        error = "r1cs value conservation: in != out + change";
        return false;
    }
    z_out = {
        ZkFieldElem::one(),
        ZkFieldElem::from_u64(in_value),
        ZkFieldElem::from_u64(out_value),
        ZkFieldElem::from_u64(change),
    };
    error.clear();
    return true;
}

ZkR1csInstance zk_r1cs_square_circuit() {
    // x * x = y
    ZkR1csInstance inst;
    inst.num_vars = 3;
    ZkR1csConstraint c;
    c.A.terms.push_back({1, ZkFieldElem::one()});
    c.B.terms.push_back({1, ZkFieldElem::one()});
    c.C.terms.push_back({2, ZkFieldElem::one()});
    inst.constraints.push_back(std::move(c));
    return inst;
}

bool zk_r1cs_square_witness(std::uint64_t x,
                            ZkR1csAssignment& z_out,
                            ZkFieldElem& public_y_out,
                            std::string& error) {
    if (x >= kZkR1csFieldPrime) {
        error = "r1cs square: x out of field";
        return false;
    }
    const ZkFieldElem xf = ZkFieldElem::from_u64(x);
    public_y_out = xf * xf;
    z_out = {ZkFieldElem::one(), xf, public_y_out};
    error.clear();
    return true;
}

} // namespace addition
