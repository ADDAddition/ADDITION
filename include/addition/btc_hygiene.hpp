#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace addition {

enum class BtcScriptClass {
    Unknown = 0,
    P2pk,
    P2pkh,
    P2sh,
    P2wpkh,
    P2wsh,
    P2tr,
};

struct BtcScriptSample {
    std::string id;
    std::string script_hex;
    std::string address;
    std::uint64_t height{0};
};

struct BtcHygieneReport {
    std::string id;
    std::string address;
    std::uint64_t height{0};
    BtcScriptClass script_class{BtcScriptClass::Unknown};
    std::string class_name;
    bool address_reuse{false};
    bool pubkey_already_on_chain{false};
};

const char* btc_script_class_name(BtcScriptClass c);
BtcScriptClass classify_btc_script(const std::string& script_hex);
bool load_btc_hygiene_fixtures(const std::string& path, std::vector<BtcScriptSample>& out, std::string& error);
std::vector<BtcHygieneReport> classify_btc_samples(const std::vector<BtcScriptSample>& samples);
std::string hygiene_receipt_body(const BtcHygieneReport& report);
bool parse_hygiene_receipt_body(const std::string& body, BtcHygieneReport& out, std::string& error);
std::string format_hygiene_report(const BtcHygieneReport& report);

} // namespace addition
