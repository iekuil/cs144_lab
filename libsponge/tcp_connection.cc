#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active_flag) {
        return;
    }
    _time_since_last_segment_received = 0;
    _receiver.segment_received(seg);

    if (seg.header().fin) {
        _inbound_fin_received = true;
    }

    if (_inbound_fin_received) {
        if (_receiver.unassembled_bytes() == 0) {
            // 接收到了fin，并且接收到的所有segment都已经assemble
            _inbound_assembled = true;

            if(!_sender.stream_in().eof()){
                _linger_after_streams_finish = false;
            }else if(_outbound_fin_sent && _outbound_fin_acked){
                _active_flag = false;
            }
        }
    }

    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        // 当sender发出一个fin之后，
        // next_seqno不会再被更新，
        // 可以以next_seqno - 1作为fin的seqno
        if(_outbound_fin_sent && _sender.bytes_in_flight() == 0){
            // 发出的fin已经被ack了
            _outbound_fin_acked = true;
            if(_inbound_fin_received && _inbound_assembled){
                if(!_lingered_time){
                    _lingered_time = 0;
                }
            }
        }

        if (_active_flag && _sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }
    if(_active_flag){
        send_segments();
    }
}

bool TCPConnection::active() const { return _active_flag; }

size_t TCPConnection::write(const string &data) {
    uint64_t write_len = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments();
    return write_len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    if(_lingered_time){
        _lingered_time = *_lingered_time + ms_since_last_tick;
        if(*_lingered_time >= 10 * _cfg.rt_timeout){
            _active_flag = false;
        }
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segments();
}

void TCPConnection::connect() {
    if (_sender.next_seqno_absolute() != 0) {
        return;
    }
    _sender.fill_window();
    send_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_segments() {
    while (!_sender.segments_out().empty()) {
        // cout << "segment" << endl;
        TCPSegment seg = _sender.segments_out().front();
        if (_receiver.ackno()) {
            seg.header().ack = true;
            seg.header().ackno = *(_receiver.ackno());
        }
        if(seg.header().fin){
            _outbound_fin_sent = true;
        }
        _segments_out.push(seg);
        _sender.segments_out().pop();
    }
}
