#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address), todo_list(), ARP_cache() {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // 网络层datagram转字符串，作为链路层frame的payload
    // 构造frame,
    // 利用下一跳ip检索ARP缓存表：
    //     存在表项 -> 填充frame的MAC地址，压入发送队列
    //     不存在表项 -> 将frame和下一跳ip的键值对压入待处理队列，
    //                  如果不存在对应ip的bucket（没有发送过对该ip的ARP请求），将ARP请求压入发送队列

    // 构造frame
    EthernetFrame frame = make_frame(dgram.serialize(), EthernetHeader::TYPE_IPv4);

    // 查找本地的ARP映射缓存，若存在对应的表项则直接发送frame
    optional<EthernetAddress> target_mac = search_ARPcache(next_hop);

    if (target_mac) {
        frame.header().dst = *target_mac;
        _frames_out.push(frame);
        return;
    }

    // 将frame插入todo_list
    insert_todolist(next_hop, frame);

    return;
}

//! \param[in] frame the incoming Ethernet frame
std::optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // 检查frame的目的地址：
    //     如果不是广播地址或当前网卡地址，不做任何动作，直接返回std::nullopt
    //
    // 检查frame的type：
    //     ipv4 -> 抽取payload，并尝试parse成网络层的datagram，
    //               如果成功parse，返回该datagram
    //     arp -> 抽取parload，并尝试parse成ARP的message，
    //              如果成功parse -> 在ARP缓存中刷新该 [ip地址-MAC地址对]，
    //                如果地址对中的ip是新出现的 -> 检查待处理队列，发送可发送的frame
    //            进一步检查ARP请求的类型：
    //              request -> 发送ARP响应
    //              reply -> 啥也不用干

    EthernetAddress broadcast_addr;
    broadcast_addr.fill(0xFF);

    if ((frame.header().dst != broadcast_addr) && (frame.header().dst != _ethernet_address)) {
        return std::nullopt;
    }

    if (frame.header().type == frame.header().TYPE_IPv4) {
        InternetDatagram datagram;
        if (datagram.parse(frame.payload()) == ParseResult::NoError) {
            return datagram;
        } else {
            return std::nullopt;
        }
    }

    if (frame.header().type != frame.header().TYPE_ARP) {
        return std::nullopt;
    }

    ARPMessage arp_message;
    if (arp_message.parse(frame.payload()) != ParseResult::NoError) {
        return std::nullopt;
    }

    Address sender_ip("0.0.0.0");
    sender_ip = sender_ip.from_ipv4_numeric(arp_message.sender_ip_address);

    EthernetAddress sender_mac(arp_message.sender_ethernet_address);

    insert_ARPcache(sender_ip, sender_mac);

    send_from_todolist(sender_ip, sender_mac);

    if (arp_message.opcode == arp_message.OPCODE_REPLY) {
        return std::nullopt;
    }

    if (arp_message.opcode == arp_message.OPCODE_REQUEST) {
        if (arp_message.target_ip_address != _ip_address.ipv4_numeric()) {
            return std::nullopt;
        }
        ARPMessage arp_reply = make_ARPmessage(sender_ip, sender_mac);
        EthernetFrame arp_reply_as_frame = make_frame(arp_reply.serialize(), EthernetHeader::TYPE_ARP);
        arp_reply_as_frame.header().dst = sender_mac;
        _frames_out.push(arp_reply_as_frame);
        return std::nullopt;
    }

    return std::nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // 循环遍历，逐一为ARP缓存表的表项的计时器加上ms_since_last_tick
    //     当计时器超出30 * 1000时，删除该表项

    for (auto iter = ARP_cache.begin(); iter != ARP_cache.end();) {
        (*iter).timer += ms_since_last_tick;
        if ((*iter).timer > TIME_OUT) {
            iter = ARP_cache.erase(iter);
        } else {
            iter++;
        }
    }

    // 此外还要处理todo_list中的计时器
    for (auto iter = todo_list.begin(); iter != todo_list.end(); iter++) {
        (*iter).timer += ms_since_last_tick;
    }
}

EthernetFrame NetworkInterface::make_frame(const BufferList &payload, const uint16_t &type) {
    BufferList frame_as_bufferlist(string(14, '\0'));
    frame_as_bufferlist.append(payload);

    EthernetFrame frame;
    frame.parse(frame_as_bufferlist.concatenate());

    frame.header().src = _ethernet_address;
    frame.header().type = type;

    return frame;
}

ARPMessage NetworkInterface::make_ARPmessage(const Address &target_ip, const optional<EthernetAddress> &target_mac) {
    ARPMessage message;
    message.sender_ethernet_address = _ethernet_address;
    message.sender_ip_address = _ip_address.ipv4_numeric();

    message.target_ip_address = target_ip.ipv4_numeric();

    if (target_mac) {
        message.opcode = message.OPCODE_REPLY;
        message.target_ethernet_address = *target_mac;
    } else {
        message.opcode = message.OPCODE_REQUEST;
        EthernetAddress all_zero;
        all_zero.fill(0x00);
        message.target_ethernet_address = all_zero;
    }

    return message;
}

std::optional<EthernetAddress> NetworkInterface::search_ARPcache(const Address &target_ip) {
    for (auto iter = ARP_cache.begin(); iter != ARP_cache.end(); iter++) {
        if ((*iter).ip_address == target_ip) {
            return (*iter).ethernet_address;
        }
    }

    return std::nullopt;
}

void NetworkInterface::insert_ARPcache(const Address &ip, const EthernetAddress &mac) {
    for (auto iter = ARP_cache.begin(); iter != ARP_cache.end(); iter++) {
        if ((*iter).ip_address == ip) {
            (*iter).ethernet_address = mac;
            (*iter).timer = 0;
            return;
        }
    }

    Mapping new_mapping(mac, ip, 0);
    ARP_cache.push_front(new_mapping);
    return;
}

void NetworkInterface::insert_todolist(const Address &ip, const EthernetFrame &frame) {
    for (auto iter = todo_list.begin(); iter != todo_list.end(); iter++) {
        if ((*iter).get_ip() == ip) {
            (*iter).get_todo_list().push(frame);
            if ((*iter).timer >= 5000) {
                (*iter).timer = (*iter).timer % 5000;
                send_ARP_request(ip);
            }
            return;
        }
    }

    Bucket new_bucket(ip);
    todo_list.push_front(new_bucket);
    todo_list.front().get_todo_list().push(frame);
    send_ARP_request(ip);
    return;
}

void NetworkInterface::send_from_todolist(const Address &ip, const EthernetAddress &mac) {
    for (auto iter = todo_list.begin(); iter != todo_list.end(); iter++) {
        if ((*iter).get_ip() == ip) {
            while (!(*iter).get_todo_list().empty()) {
                (*iter).get_todo_list().front().header().dst = mac;
                _frames_out.push((*iter).get_todo_list().front());
                (*iter).get_todo_list().pop();
            }
            iter = todo_list.erase(iter);
            return;
        }
    }
    return;
}

void NetworkInterface::send_ARP_request(const Address &target_ip) {
    ARPMessage arp_req = make_ARPmessage(target_ip, std::nullopt);
    EthernetFrame arp_req_frame = make_frame(arp_req.serialize(), EthernetHeader::TYPE_ARP);

    EthernetAddress all_one;
    all_one.fill(0xFF);
    arp_req_frame.header().dst = all_one;
    _frames_out.push(arp_req_frame);
    return;
}
