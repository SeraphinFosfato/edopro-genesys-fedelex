#include "server_tls.h"
#include <nlohmann/json.hpp>

namespace ygo::server_tls {

TlsField ParseTlsField(const nlohmann::json& entry) {
	if(!entry.is_object())
		return TlsField::Invalid;
	auto it = entry.find("tls");
	if(it == entry.end())
		return TlsField::Absent;
	if(!it->is_boolean())
		return TlsField::Invalid;
	return it->get<bool>() ? TlsField::Enabled : TlsField::Disabled;
}

static char Lower(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

static std::string Normalize(const std::string& name) {
	std::string out;
	out.reserve(name.size());
	for(char c : name)
		out.push_back(Lower(c));
	if(!out.empty() && out.back() == '.')
		out.pop_back();
	return out;
}

bool SameServerName(const std::string& a, const std::string& b) {
	const auto na = Normalize(a);
	return !na.empty() && na == Normalize(b);
}

bool TlsServerMatches(const std::string& configured_name, uint16_t configured_port,
					  const std::string& link_name, uint16_t link_port) {
	return configured_port == link_port && SameServerName(configured_name, link_name);
}

}
