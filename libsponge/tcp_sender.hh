#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.

// lab3的实验手册讲的东西太少了
// 翻了lab4的实验手册才知道TCPSender需要负责设置：
//   seqno, syn , payload, fin 这几个字段
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    // 用一个队列来备份已发送未确认的segment
    // 以下几种情况需要操作这个队列：
    //   1. ack_received()方法接收到新的ackno
    //        -> 将seqno + seg.length_in_sequence_space() < ackno的seg从队列中删除
    //   2. 发送一个seg.length_in_sequence_space()不为0的seg
    //        -> 将这个seg插入到队列的末尾
    //   3. ticks()方法中发现定时器过期
    //        -> 将位于队列首的seg进行重新发送
    std::queue<TCPSegment> _outstanding_seg;

    // 记录连续重传的次数
    // 似乎每次需要对_outstanding_seg进行操作的时候也需要对这个变量进行操作
    uint64_t _consecutive_retransmissions;

    // 记录sequence space中有多少字节已发送未被确认
    // 需要和_outstanding_seg同步更新
    uint64_t _bytes_in_flight;

    // 记录接收方的窗口大小
    // 只会被fill_window()、ack_received()两个方法改变
    // 初始值应该为1
    std::optional<uint64_t> _receiver_window_sz;

    // 重传倒计时
    std::optional<uint64_t> _countdown_timer;

    // 当前的过期时间
    uint64_t _current_retransmission_timeout;

    // 用于unwrap得到absolutely ackno
    uint64_t ack_checkpoint;

    // 标志是否已经发送过fin flag
    // 在发送fin flag后，阻止新的segment被发送
    bool output_ended;

    // 上一次接收方窗口的右边界
    // 用于ack_received()中判断是否打开了新的窗口
    uint64_t last_recv_window_rboundary;

    // 标记是否接收到一个window size为0的ack
    // 用于重传计时器在到期时的行为选择
    // （当window size为0时，不会增加“连续重传”计数，也不会让RTO翻倍）
    bool ack_wdsz_zero_flag;

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    bool ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    // 无论是实验手册、接口文档还是现在这个代码模板，
    // 这个方法都是没有参数的，
    // 如果需要让这个方法访问到window的大小，
    // 只能是：
    //  1.增加一个成员变量用来维护接收方的window size，
    //  2.用一个带缺省值的参数，
    //
    // 但是如果测试用例中直接调用fill_window，
    // 第二种方法估计过不了
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}

    // 根据给定的seqno、syn、fin、payload
    // 构造一个tcp segment
    // 用到TCPSegment::parse(const Buffer buffer, const uint32_t datagram_layer_checksum)
    //  这个方法会通过“NetParser p{buffer}”来用buffer的值初始化一个netparser
    //  然后调用_header.parse(p)给TCPSegment中的header赋值，
    //    同时还剔除了整个TCP报文中除了data以外的部分
    //  接着用_payload = p.buffer()把剩下的data赋值给TCPSegment中的payload部分
    //
    // 所以首先要构造一个包含了header信息的string
    // 然后把payload拼接到后面
    // 再用这个string初始化一个buffer
    // 再把这个buffer传给TCPSegment::parse
    //
    // 其中seqno将来自于_next_seqno和_isn
    // syn将来自对_next_seqno == 0条件的检查
    // fin将来自_stream的eof标志
    // payload将会从_stream.read()方法读取相应的字节形成string
    TCPSegment make_segment(const WrappingInt32 &seqno, const bool &syn, const bool &fin, const std::string &payload);
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
