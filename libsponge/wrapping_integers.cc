#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    WrappingInt32 tmp(n);
    return tmp + isn.raw_value();
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    WrappingInt32 wrapping_checkpoint(checkpoint);

    // 计算得到关于isn的相对偏移
    WrappingInt32 relative_n(n - isn);

    int32_t distance = relative_n - wrapping_checkpoint;

    if (distance > 0) {
        // 这里的distance是用int32_t表示的
        // 意味着以下这种情况才会出现distance大于0
        //     在数轴上 0 --(1)-- wrap_checkpoint ---(2)--- relative_n ---(3)--- (1<<32) - 1 ,
        //     从wrapped区域看，relative_n在checkpoint的右侧
        //     并且间距(2)比间距(1)+(3)要小，
        //     那么一定有(uint32_t)distance <= (1 << 31),
        //     相应的，(int32_t)distance >= 0,
        //     此时离checkpoint最近的点是右侧distance距离处，而不是向左折返(1<<32)-distance距离处
        return checkpoint + static_cast<uint64_t>(distance);
    } else {
        // 同理，
        //     只有当从checkpoint向左折返abs(distance)距离比向右偏移(1<<32)-abs(distance)的距离小时，
        //     才会出现distance小于0，
        if (checkpoint < relative_n.raw_value()) {
            // 这种情况是个例外，
            // 意味着checkpoint小于 1<<32，
            // 同时relative_n和checkpoint之间的距离又大于1<<31,
            // 此时不应该向左折返，
            // 需要向右
            return checkpoint + relative_n.raw_value();
        }
        return checkpoint - static_cast<uint64_t>(-distance);
    }
}
