#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

//为了使用NetUnparser
#include <parser.hh>

//为了用internet checksum
#include <iostream>
#include <util.hh>

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
    , _receiver_window_sz(std::nullopt)
    , _countdown_timer(std::nullopt)
    , _current_retransmission_timeout(retx_timeout)
    , ack_checkpoint(0)
    , output_ended(false)
    , last_recv_window_rboundary(0)
    , ack_wdsz_zero_flag(false) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    // 之前大概是理解错了，这个fill_window不是发送单个的segment
    // 而是发送多个segment直到window清空
    //
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

    // 判断即将发送的segment是否超出接收方窗口的右边界
    if (_receiver_window_sz && (_next_seqno >= last_recv_window_rboundary)) {
        return;
    }

    // 用一个循环把整个窗口填满
    while (1) {
        if (output_ended) {
            return;
        }

        WrappingInt32 seqno = next_seqno();

        bool syn = 0;
        bool fin = 0;

        if (_next_seqno == 0) {
            syn = 1;
        }

        // 在建立连接时，
        // 还没有收到过ack，不知道具体的接收方窗口大小，
        // 此时需要假设窗口大小为1
        if (!_receiver_window_sz) {
            _receiver_window_sz = 1;
        }

        // 这个变量被赋值为0只会是因为上一次循环结束后已经填满了接收方窗口
        //
        // 当ack_received()方法接收到window_size为0时
        // 为了让fill_window()符合“当接收方窗口为0时，视为窗口为1，以探测接收方窗口是否开放了新的空间，避免卡死”的行为预期
        // _receiver_window_sz会被直接赋值为1
        if (*_receiver_window_sz == 0) {
            return;
        }

        uint64_t expected_payload_len = min(*_receiver_window_sz, TCPConfig::MAX_PAYLOAD_SIZE);

        string payload = _stream.read(expected_payload_len);

        // 当窗口中还有剩余空间，并且对输出流的写入已经结束时，才会设置fin标志
        if (_stream.eof() && payload.length() + syn < *_receiver_window_sz) {
            fin = 1;
            output_ended = true;
        }

        TCPSegment segment = make_segment(seqno, syn, fin, payload);

        // 当出现空的segment时，
        // 说明对输出流的写入还没到来，没能从stream中读取到有效的字符串，
        // 此时不应该发送这个空的segment
        if (segment.length_in_sequence_space() == 0) {
            return;
        }

        _segments_out.push(segment);
        _outstanding_seg.push(segment);

        // 更新剩余的接收方窗口容量
        _receiver_window_sz = *_receiver_window_sz - segment.length_in_sequence_space();

        // 更新下一个segment的起始序列号
        _next_seqno += segment.length_in_sequence_space();

        // 更新已发送、未被确认的segment的总字节数
        _bytes_in_flight += segment.length_in_sequence_space();

        // 当重传计时器未启动时，启动重传计时器
        if (!_countdown_timer) {
            _countdown_timer = _current_retransmission_timeout;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
bool TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 忽略了窗口右边界的问题
    //
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

    bool reflash_timer_flag = false;

    // 接收方ack的序列号是还没发送的序列号
    // 说明出现错误
    if (abs_ackno > _next_seqno) {
        return false;
    }

    // 接收方ack了新的字节
    // 更新ack_checkpoint
    if (abs_ackno > ack_checkpoint) {
        ack_checkpoint = abs_ackno;
    }

    // 用一个循环来从_outstanding_seg列表中弹出所有已经被完全ack的segment
    while (1) {
        if (_outstanding_seg.empty()) {
            break;
        }

        TCPSegment segment = _outstanding_seg.front();
        uint64_t seg_seqno = unwrap(segment.header().seqno, _isn, ack_checkpoint);

        if (seg_seqno + segment.length_in_sequence_space() > abs_ackno) {
            break;
        }

        _outstanding_seg.pop();

        _bytes_in_flight -= segment.length_in_sequence_space();
        reflash_timer_flag = 1;
    }

    // 当接收方ack了新的segment、并且window大小不为0时
    // 重置重传计时器
    if (reflash_timer_flag && window_size != 0) {
        _current_retransmission_timeout = _initial_retransmission_timeout;
        _countdown_timer = _current_retransmission_timeout;
        _consecutive_retransmissions = 0;
    }

    // 当windows size为0时
    // 发送新的segment时应该将窗口大小视为1，以探测接收方窗口大小是否发生变化，避免卡死
    // 同时重传计时器在接收到为0的window size时，又会有特定的行为，需要设置相应的flag以供判断
    if (window_size == 0) {
        _receiver_window_sz = 1;
        ack_wdsz_zero_flag = true;
    } else {
        _receiver_window_sz = window_size;
        ack_wdsz_zero_flag = false;
    }

    // 当接收方窗口的右边界右移时，
    // 更新当前记录的右边界，
    // 并调用fill_window()发送新的segment
    if (abs_ackno + *_receiver_window_sz > last_recv_window_rboundary) {
        last_recv_window_rboundary = abs_ackno + *_receiver_window_sz;
        fill_window();
    }

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

    // 当计时器未启动时，启动计时器
    if (!_countdown_timer) {
        _countdown_timer = _current_retransmission_timeout - ms_since_last_tick;
        return;
    }

    // 当计时器未过期时，更新计时器的剩余时间
    if (*_countdown_timer > ms_since_last_tick) {
        _countdown_timer = *_countdown_timer - ms_since_last_tick;
        return;
    }

    // 当计时器已经过期

    // 若当前接收方的window大小不为0
    // 增加连续重传计数，并使RTO时间翻倍
    if (!ack_wdsz_zero_flag) {
        _consecutive_retransmissions += 1;
        _current_retransmission_timeout = _current_retransmission_timeout * 2;
    }

    // 重新启动计时器
    _countdown_timer = _current_retransmission_timeout;

    // 当重传列表不为空时，重新发送列表中最老的segment
    if (_outstanding_seg.empty()) {
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

TCPSegment TCPSender::make_segment(const WrappingInt32 &seqno,
                                   const bool &syn,
                                   const bool &fin,
                                   const std::string &payload) {
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
    unparser.u16(segment_as_string, 0);                  // source port留空
    unparser.u16(segment_as_string, 0);                  // dest port留空
    unparser.u32(segment_as_string, seqno.raw_value());  //用传入的seqno设置seqno
    unparser.u32(segment_as_string, 0);                  // ackno留空
    unparser.u8(segment_as_string, 5 << 4);              // data offset是20个字节，即5个四字
    unparser.u8(segment_as_string,
                static_cast<uint8_t>(fin) | (static_cast<uint8_t>(syn) << 1));  // flag中只设置syn和fin，其他留空
    unparser.u16(segment_as_string, 0);                                         // window留空
    unparser.u16(segment_as_string, 0);  // checksum留空，因为在这里还没有得到能够计算校验和的完整信息
    unparser.u16(segment_as_string, 0);  // urgent pointer留空
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
