#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

//为了使用NetUnparser
#include <parser.hh>

//为了用internet checksum
#include <util.hh>

#include <iostream>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _next_seqno(0)
    , _outstanding_seg()
    , _consecutive_retransmissions(0)
    , _bytes_in_flight(0)
    , _receiver_window_sz(0) 
    , _countdown_timer(std::nullopt)
    , _current_retransmission_timeout(retx_timeout) 
    , ack_checkpoint(0) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    // 访问成员变量_receiver_window_sz获取接收方当前的窗口大小
    // 访问成员变量_next_seqno、_isn获取seqno
    // 根据_next_seqno是否为零决定是否发送syn
    // 根据_stream的eof标志位决定是否发送fin
    // 在[_receiver_window_sz - syn - fin]、[_stream的可读取字节数]、[MTU]中取最小值作为payload的大小
    //      并从_stream中读取相应数量的字节数
    //
    // 调用make_segment构造TCPSegment，同时加到_segment_out和_outstanding_seg这两个队列中
    // 当倒计时未启动时，启动倒计时
    //
    // 更新_receiver_window_sz
    // 更新_next_seqno
    // 更新_bytes_in_flight

    uint64_t window = _receiver_window_sz;
    WrappingInt32 seqno = next_seqno();

    bool syn = 0;
    bool fin = 0;

    if(_next_seqno == 0){
        syn = 1;
    }

    if(window == 0){
        window = 1;
    }

    uint64_t expected_payload_len = min(window - syn - 1, TCPConfig::MAX_PAYLOAD_SIZE);
    string payload = _stream.read(expected_payload_len);

    if(_stream.eof()){
        fin = 1;
    }

    TCPSegment segment = make_segment(seqno, syn, fin, payload);

    //cout<<"in fill_window(): seqno="<<_next_seqno<<", length="<< segment.length_in_sequence_space() << endl;

    if(segment.length_in_sequence_space() == 0){
        return;
    }

    _segments_out.push(segment);
    _outstanding_seg.push(segment);

    _receiver_window_sz -= segment.length_in_sequence_space();
    _next_seqno += segment.length_in_sequence_space();
    _bytes_in_flight += segment.length_in_sequence_space();

    if(!_countdown_timer){
        _countdown_timer = _current_retransmission_timeout;
    }

}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 当这个函数被调用时，意味着成功接收到了对方发送的ack
    //
    // 根据ackno弹出_outstanding_seg中的零项或多项
    // 将RTO重置为_initial_retransmission_timeout
    // 用重置后的RTO重启重传计时器
    // 将_consecutive_retransmission重置为0
    //
    // 更新_receiver_window_sz
    // 当接收方window大小不为零的时候，调用fill_window()发送一个新的segment
    //
    // 此外，当ackno是TCPSender尚未发送的segment时，需要返回false

    uint64_t abs_ackno = unwrap(ackno, _isn, ack_checkpoint);

    if(abs_ackno > _next_seqno){
        return false;
    }

    while(1){
        if(_outstanding_seg.empty()){
            break;
        }

        TCPSegment segment = _outstanding_seg.front();
        uint64_t seg_seqno = unwrap(segment.header().seqno, _isn, ack_checkpoint);

        if(seg_seqno + segment.length_in_sequence_space() > abs_ackno){
            break;
        }

        _outstanding_seg.pop();
        _bytes_in_flight -= segment.length_in_sequence_space();
    }

    _current_retransmission_timeout = _initial_retransmission_timeout;
    _countdown_timer = _current_retransmission_timeout;
    _consecutive_retransmissions = 0;

    _receiver_window_sz = window_size;
    fill_window();

    return true;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 检查_countdown_timer
    //      若_countdown_timer的剩余计时小于ms_since_last_tick
    //          -> 重传outstanding队列中的第一个segment
    //             更新_consecutive_retransmission
    //             更新_current_retransmission_timeout(加倍)
    //             用更新后的RTO重新启动计时器
    //      若计时器没过期
    //          -> 更新计时器的剩余时间

    if(!_countdown_timer){
        _countdown_timer = _current_retransmission_timeout - ms_since_last_tick;
        return;
    }

    if(*_countdown_timer > ms_since_last_tick){
        _countdown_timer = *_countdown_timer - ms_since_last_tick;
        return;
    }

    _consecutive_retransmissions += 1;
    _current_retransmission_timeout = _current_retransmission_timeout * 2;
    _countdown_timer = _current_retransmission_timeout;

    if(_outstanding_seg.empty()){
        return;
    }

    _segments_out.push(_outstanding_seg.front());
    return;
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    // 构造一个sequence space的长度为0的segment
    //      即：不包含syn、fin且payload长度为0的segment
    // 
    std::string empty_string;
    TCPSegment empty_segment = make_segment(next_seqno(), 0, 0, empty_string);
    _segments_out.push(empty_segment);

}

TCPSegment TCPSender::make_segment(const WrappingInt32 &seqno, const bool &syn, const bool &fin, const std::string &payload) {
    //      0                   1                   2                   3
    //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |          Source Port          |       Destination Port        |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                        Sequence Number                        |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                    Acknowledgment Number                      |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |  Data |           |U|A|P|R|S|F|                               |
    // | Offset| Reserved  |R|C|S|S|Y|I|            Window             |
    // |       |           |G|K|H|T|N|N|                               |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |           Checksum            |         Urgent Pointer        |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                    Options                    |    Padding    |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    // |                             data                              |
    // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    
    // 首先需要以string的形式手搓一个header出来
    // 还要注意小端序和大端序的转换
    // 也许可以使用NetUnparser

    std::string segment_as_string;
    NetUnparser unparser;
    unparser.u16(segment_as_string, 0);  //source port留空
    unparser.u16(segment_as_string, 0);  //dest port留空
    unparser.u32(segment_as_string, seqno.raw_value()); //用传入的seqno设置seqno
    unparser.u32(segment_as_string, 0);  //ackno留空
    unparser.u8(segment_as_string, 5 << 4);  //data offset是20个字节，即5个四字
    unparser.u8(segment_as_string, static_cast<uint8_t>(fin) | (static_cast<uint8_t>(syn) << 1));    //flag中只设置syn和fin，其他留空
    unparser.u16(segment_as_string, 0);  // window留空
    unparser.u16(segment_as_string, 0);  //checksum留空，因为在这里还没有得到能够计算校验和的完整信息
    unparser.u16(segment_as_string, 0);  //urgent pointer留空
    //不设置options字段
    //由于padding的作用是保证header的长度是四字节的整数倍，
    //因此也不需要padding

    segment_as_string.append(payload);  //拼接上payload

    Buffer segment_as_buffer(std::move(segment_as_string));

    InternetChecksum check(0);
    check.add(segment_as_buffer);

    TCPSegment segment;
    segment.parse(segment_as_buffer, check.value());

    return segment;
}
