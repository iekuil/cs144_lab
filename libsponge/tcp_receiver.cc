#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

bool TCPReceiver::segment_received(const TCPSegment &seg) {
    // syn、fin和data可能在同一个segment里面同时出现
    bool syn = seg.header().syn;
    bool fin = seg.header().fin;

    // 得到当前seg中有效的sequence长度
    // 之后用来计算absolutely sequence no
    uint64_t seg_seq_len = seg.length_in_sequence_space();

    size_t window = window_size();

    // 首先检查initial_seqno，判断是否是一个新链接
    if (!initial_seqno) {
        if (!syn) {
            return false;
        } else {
            WrappingInt32 ins(seg.header().seqno);
            initial_seqno = ins;
            abs_seqno = 1;
        }
    }
    uint64_t seg_seqno = unwrap(seg.header().seqno, *initial_seqno, abs_seqno);

    // 4位头部长度（header length）：标识该TCP头部有多少个32bit字（4字节）。
    // 4位最大能标识15，所以TCP头部最长是60字节。
    // 这里的data_offset将以四字节为单位的doff换算成以字节为单位
    size_t data_offset = (seg.header().doff - 5) * 4;

    // 把option从payload中去掉，得到真正的data
    std::string data;
    if (data_offset < seg.payload().copy().length()) {
        data = seg.payload().copy().substr(data_offset);
    } else {
        data = seg.payload().copy();
    }

    if (window == 0) {
        window = 1;
    }

    if (seg_seq_len == 0) {
        seg_seq_len = 1;
    }

    if ((abs_seqno + window > seg_seqno) && (seg_seqno + seg.length_in_sequence_space()) > abs_seqno) {
        // 当前seg有落在window中的部分
        // 在大多数情况下，index是seg_seqno - 1
        // 当接收到的syn和data一起出现时，seg_seqno会算错index，得到一个负的index
        // 因此需要seg_seqno - 1 + syn

        _reassembler.push_substring(data, seg_seqno - 1 + syn, fin);

        // 实际上翻了实验手册之后发现压根就没提option字段和doff的事
        // 实验手册中默认syn只在开头出现，fin只在结尾出现
        // 并且不用管options字段
        // 实验手册也只要求通过加一、减一来进行absolutely seqno和stream indices之间的换算
        if (_reassembler.stream_out().input_ended()) {
            abs_seqno = _reassembler.stream_out().bytes_written() + 2;
        } else {
            abs_seqno = _reassembler.stream_out().bytes_written() + 1;
        }
        return true;
    }
    return false;
}

std::optional<WrappingInt32> TCPReceiver::ackno() const {
    if (initial_seqno) {
        return wrap(abs_seqno, *initial_seqno);
    } else {
        return std::nullopt;
    }
}

size_t TCPReceiver::window_size() const { return _reassembler.stream_out().remaining_capacity(); }
