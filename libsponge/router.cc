#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    Rule route_rule(route_prefix, prefix_length, next_hop, interface_num);
    _rules_table.push_back(route_rule);
    return;
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.

    // 当datagram的ttl已经为0、或者这一次转发之后将降到0时，
    // 需要丢弃掉这个datagram
    if ((dgram.header().ttl == 0) || (dgram.header().ttl == 1)) {
        return;
    }

    dgram.header().ttl -= 1;

    uint32_t dst_ip = dgram.header().dst;

    std::optional<uint64_t> matched_rule_num(std::nullopt);

    for (uint64_t i = 0; i < _rules_table.size(); i++) {
        if (match(_rules_table[i].route_prefix(), _rules_table[i].prefix_length(), dst_ip)) {
            if (!matched_rule_num) {
                matched_rule_num = i;
            } else if (_rules_table[i].prefix_length() > _rules_table[*matched_rule_num].prefix_length()) {
                matched_rule_num = i;
            }
        }
    }

    // 没有匹配的路由规则，
    // 选择丢弃该datagram
    if (!matched_rule_num) {
        return;
    }

    // 确定下一跳ip地址：
    //   当路由规则中有指定的下一跳地址时，使用该地址；
    //   否则认为下一跳地址位于路由规则对应网卡所在的目标子网中，以datagram中的目的地址作为下一跳地址
    Address next_hop("0.0.0.0");
    if (_rules_table[*matched_rule_num].next_hop()) {
        next_hop = *(_rules_table[*matched_rule_num].next_hop());
    } else {
        next_hop = next_hop.from_ipv4_numeric(dgram.header().dst);
    }

    _interfaces[_rules_table[*matched_rule_num].interface_num()].send_datagram(dgram, next_hop);
    return;
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}

bool Router::match(const uint32_t &route_prefix, const uint8_t &prefix_length, const uint32_t &ip) {
    if (prefix_length == 0) {
        return true;
    } else if ((route_prefix >> (32 - prefix_length)) == (ip >> (32 - prefix_length))) {
        return true;
    } else {
        return false;
    }
}
