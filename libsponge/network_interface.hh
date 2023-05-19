#ifndef SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
#define SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "tcp_over_ip.hh"
#include "tun.hh"

#include <optional>
#include <queue>

// queue不能遍历，
// 我需要一个能遍历的容器来存储待处理的frame和本地ARP缓存
#include <list>

#define TIME_OUT 30 * 1000

class Mapping {
  public:
    EthernetAddress ethernet_address;
    Address ip_address;
    size_t timer;

    Mapping(const EthernetAddress &eth_addr, const Address &ip_addr, const size_t &ms)
        : ethernet_address(eth_addr), ip_address(ip_addr), timer(ms) {}
};

class Bucket {
  private:
    Address ip_address;
    std::queue<EthernetFrame> todo_list;

  public:
    size_t timer;
    Bucket(const Address &ip_addr) : ip_address(ip_addr), todo_list(), timer(0){};
    std::queue<EthernetFrame> &get_todo_list() { return todo_list; }
    Address &get_ip() { return ip_address; };
};

//! \brief A "network interface" that connects IP (the internet layer, or network layer)
//! with Ethernet (the network access layer, or link layer).

//! This module is the lowest layer of a TCP/IP stack
//! (connecting IP with the lower-layer network protocol,
//! e.g. Ethernet). But the same module is also used repeatedly
//! as part of a router: a router generally has many network
//! interfaces, and the router's job is to route Internet datagrams
//! between the different interfaces.

//! The network interface translates datagrams (coming from the
//! "customer," e.g. a TCP/IP stack or router) into Ethernet
//! frames. To fill in the Ethernet destination address, it looks up
//! the Ethernet address of the next IP hop of each datagram, making
//! requests with the [Address Resolution Protocol](\ref rfc::rfc826).
//! In the opposite direction, the network interface accepts Ethernet
//! frames, checks if they are intended for it, and if so, processes
//! the the payload depending on its type. If it's an IPv4 datagram,
//! the network interface passes it up the stack. If it's an ARP
//! request or reply, the network interface processes the frame
//! and learns or replies as necessary.
class NetworkInterface {
  private:
    //! Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
    EthernetAddress _ethernet_address;

    //! IP (known as internet-layer or network-layer) address of the interface
    Address _ip_address;

    //! outbound queue of Ethernet frames that the NetworkInterface wants sent
    std::queue<EthernetFrame> _frames_out{};

    // 存储那些ARP缓存中没有对应地址的、待发送的frame
    std::list<Bucket> todo_list;

    // 存储ARP映射对的本地缓存
    std::list<Mapping> ARP_cache;

  public:
    //! \brief Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer) addresses
    NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address);

    //! \brief Access queue of Ethernet frames awaiting transmission
    std::queue<EthernetFrame> &frames_out() { return _frames_out; }

    //! \brief Sends an IPv4 datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination address).

    //! Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next hop
    //! ("Sending" is accomplished by pushing the frame onto the frames_out queue.)
    void send_datagram(const InternetDatagram &dgram, const Address &next_hop);

    //! \brief Receives an Ethernet frame and responds appropriately.

    //! If type is IPv4, returns the datagram.
    //! If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
    //! If type is ARP reply, learn a mapping from the "sender" fields.
    std::optional<InternetDatagram> recv_frame(const EthernetFrame &frame);

    //! \brief Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    // 构造frame
    EthernetFrame make_frame(const BufferList &payload, const uint16_t &type);

    // 构造ARP报文
    ARPMessage make_ARPmessage(const Address &target_ip, const std::optional<EthernetAddress> &target_mac);

    // 查询ARP缓存
    std::optional<EthernetAddress> search_ARPcache(const Address &target_ip);

    // 刷新ARP表项
    void insert_ARPcache(const Address &ip, const EthernetAddress &mac);

    // 插入todo_list
    void insert_todolist(const Address &ip, const EthernetFrame &frame);

    // 从todo_list中发送符合相应地址的frame
    void send_from_todolist(const Address &ip, const EthernetAddress &mac);

    // 构造并发送ARP请求
    void send_ARP_request(const Address &target_ip);
};

#endif  // SPONGE_LIBSPONGE_NETWORK_INTERFACE_HH
