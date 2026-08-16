#include "addition/btc_hygiene.hpp"

#include "addition/crypto.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace addition {
namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    if (i > 0) {
        s.erase(0, i);
    }
    return s;
}

bool json_string_field(const std::string& obj, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    const auto pos = obj.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    auto colon = obj.find(':', pos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    ++colon;
    while (colon < obj.size() && std::isspace(static_cast<unsigned char>(obj[colon]))) {
        ++colon;
    }
    if (colon >= obj.size()) {
        return false;
    }
    if (obj[colon] == '"') {
        const auto end = obj.find('"', colon + 1);
        if (end == std::string::npos) {
            return false;
        }
        out = obj.substr(colon + 1, end - colon - 1);
        return true;
    }
    auto end = colon;
    while (end < obj.size() && (std::isdigit(static_cast<unsigned char>(obj[end])) != 0)) {
        ++end;
    }
    out = obj.substr(colon, end - colon);
    return !out.empty();
}

} // namespace

const char* btc_script_class_name(BtcScriptClass c) {
    switch (c) {
    case BtcScriptClass::P2pk:
        return "p2pk";
    case BtcScriptClass::P2pkh:
        return "p2pkh";
    case BtcScriptClass::P2sh:
        return "p2sh";
    case BtcScriptClass::P2wpkh:
        return "p2wpkh";
    case BtcScriptClass::P2wsh:
        return "p2wsh";
    case BtcScriptClass::P2tr:
        return "p2tr";
    case BtcScriptClass::Unknown:
        return "unknown";
    }
    const BtcScriptClass missing = c;
    switch (missing) {
    case BtcScriptClass::P2pk:
    case BtcScriptClass::P2pkh:
    case BtcScriptClass::P2sh:
    case BtcScriptClass::P2wpkh:
    case BtcScriptClass::P2wsh:
    case BtcScriptClass::P2tr:
    case BtcScriptClass::Unknown:
        break;
    }
    return "unknown";
}

BtcScriptClass classify_btc_script(const std::string& script_hex) {
    std::string lower;
    lower.reserve(script_hex.size());
    for (char c : script_hex) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // P2PKH: OP_DUP OP_HASH160 14 <20> OP_EQUALVERIFY OP_CHECKSIG
    if (lower.size() == 50 && lower.rfind("76a914", 0) == 0 && lower.size() >= 46 &&
        lower.substr(46) == "88ac") {
        return BtcScriptClass::P2pkh;
    }
    // P2SH: OP_HASH160 14 <20> OP_EQUAL
    if (lower.size() == 46 && lower.rfind("a914", 0) == 0 && lower.substr(44) == "87") {
        return BtcScriptClass::P2sh;
    }
    // P2WPKH: 0014 <20>
    if (lower.size() == 44 && lower.rfind("0014", 0) == 0) {
        return BtcScriptClass::P2wpkh;
    }
    // P2WSH: 0020 <32>
    if (lower.size() == 68 && lower.rfind("0020", 0) == 0) {
        return BtcScriptClass::P2wsh;
    }
    // P2TR: 5120 <32>
    if (lower.size() == 68 && lower.rfind("5120", 0) == 0) {
        return BtcScriptClass::P2tr;
    }
    // P2PK compressed: 21 <33> OP_CHECKSIG
    if (lower.size() == 70 && lower.rfind("21", 0) == 0 && lower.substr(68) == "ac") {
        return BtcScriptClass::P2pk;
    }
    // P2PK uncompressed: 41 <65> OP_CHECKSIG
    if (lower.size() == 134 && lower.rfind("41", 0) == 0 && lower.substr(132) == "ac") {
        return BtcScriptClass::P2pk;
    }
    return BtcScriptClass::Unknown;
}

bool load_btc_hygiene_fixtures(const std::string& path, std::vector<BtcScriptSample>& out, std::string& error) {
    out.clear();
    std::ifstream in(path);
    if (!in) {
        error = "cannot open fixture file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    std::size_t pos = 0;
    while (true) {
        const auto start = text.find('{', pos);
        if (start == std::string::npos) {
            break;
        }
        const auto end = text.find('}', start);
        if (end == std::string::npos) {
            break;
        }
        const auto obj = text.substr(start, end - start + 1);
        BtcScriptSample sample{};
        json_string_field(obj, "id", sample.id);
        json_string_field(obj, "script_hex", sample.script_hex);
        json_string_field(obj, "address", sample.address);
        std::string height;
        if (json_string_field(obj, "height", height)) {
            try {
                sample.height = static_cast<std::uint64_t>(std::stoull(height));
            } catch (...) {
                sample.height = 0;
            }
        }
        if (!sample.script_hex.empty()) {
            out.push_back(std::move(sample));
        }
        pos = end + 1;
    }
    if (out.empty()) {
        error = "no fixture samples parsed";
        return false;
    }
    return true;
}

std::vector<BtcHygieneReport> classify_btc_samples(const std::vector<BtcScriptSample>& samples) {
    std::unordered_map<std::string, int> addr_count;
    for (const auto& s : samples) {
        if (!s.address.empty()) {
            addr_count[s.address] += 1;
        }
    }

    std::vector<BtcHygieneReport> reports;
    reports.reserve(samples.size());
    for (const auto& s : samples) {
        BtcHygieneReport r{};
        r.id = s.id;
        r.address = s.address;
        r.height = s.height;
        r.script_class = classify_btc_script(s.script_hex);
        r.class_name = btc_script_class_name(r.script_class);
        r.address_reuse = !s.address.empty() && addr_count[s.address] > 1;
        r.pubkey_already_on_chain = (r.script_class == BtcScriptClass::P2pk);
        reports.push_back(std::move(r));
    }
    return reports;
}

std::string hygiene_receipt_body(const BtcHygieneReport& report) {
    std::ostringstream out;
    out << "ADDITION-HYGIENE-REHEARSAL|v1"
        << "|btc_addr=" << report.address
        << "|height=" << report.height
        << "|class=" << report.class_name
        << "|reuse=" << (report.address_reuse ? "1" : "0")
        << "|pubkey_on_chain=" << (report.pubkey_already_on_chain ? "1" : "0")
        << "|moves_bitcoin=0"
        << "|claim=attestation_not_bip360";
    return out.str();
}

bool parse_hygiene_receipt_body(const std::string& body, BtcHygieneReport& out, std::string& error) {
    if (body.rfind("ADDITION-HYGIENE-REHEARSAL|v1|", 0) != 0) {
        error = "not a hygiene rehearsal receipt";
        return false;
    }
    out = BtcHygieneReport{};
    std::istringstream iss(body);
    std::string part;
    while (std::getline(iss, part, '|')) {
        const auto eq = part.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = part.substr(0, eq);
        const auto value = part.substr(eq + 1);
        if (key == "btc_addr") {
            out.address = value;
        } else if (key == "height") {
            try {
                out.height = static_cast<std::uint64_t>(std::stoull(value));
            } catch (...) {
                error = "invalid receipt height";
                return false;
            }
        } else if (key == "class") {
            out.class_name = value;
            if (value == "p2pk") {
                out.script_class = BtcScriptClass::P2pk;
            } else if (value == "p2pkh") {
                out.script_class = BtcScriptClass::P2pkh;
            } else if (value == "p2sh") {
                out.script_class = BtcScriptClass::P2sh;
            } else if (value == "p2wpkh") {
                out.script_class = BtcScriptClass::P2wpkh;
            } else if (value == "p2wsh") {
                out.script_class = BtcScriptClass::P2wsh;
            } else if (value == "p2tr") {
                out.script_class = BtcScriptClass::P2tr;
            } else {
                out.script_class = BtcScriptClass::Unknown;
            }
        } else if (key == "reuse") {
            out.address_reuse = (value == "1");
        } else if (key == "pubkey_on_chain") {
            out.pubkey_already_on_chain = (value == "1");
        }
    }
    if (out.address.empty() || out.class_name.empty()) {
        error = "receipt missing address or class";
        return false;
    }
    return true;
}

std::string format_hygiene_report(const BtcHygieneReport& report) {
    std::ostringstream out;
    out << "id=" << report.id
        << " btc_addr=" << report.address
        << " height=" << report.height
        << " class=" << report.class_name
        << " reuse=" << (report.address_reuse ? "1" : "0")
        << " pubkey_on_chain=" << (report.pubkey_already_on_chain ? "1" : "0")
        << " moves_bitcoin=0"
        << " claim=attestation_not_bip360";
    return out.str();
}

} // namespace addition
