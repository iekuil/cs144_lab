#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <string>

// modern c++ 不让使用new和malloc
// 需要使用stl里面的list容器来临时存储乱序的字节
#include <list>

class list_node {
  private:
    // data存储字节流
    std::string data;

    // index存储data的首字节对应的序号
    uint64_t index;

  public:
    // 构造函数，初始化data和index
    list_node(const std::string &d, const uint64_t i);

    // 获取data
    std::string &get_data();

    // 获取index
    uint64_t &get_index();

    // 为了能够使用list的sort函数，需要重载小于号运算符
    bool operator<(list_node another);
};

typedef std::list<list_node> UNASSEMBLED_LIST;

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes

    // 已存储但未进入字节流的数据包列表
    UNASSEMBLED_LIST todo_list;

    // 未进入字节流的字节数量总计
    size_t todo_bytes;

    // 输入方的eof标志
    bool input_eof;

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;

    // 向bytestream中写入data
    bool write_to_bytestream(const std::string &data, const size_t &index);

    bool processing_data(const std::string &data, const size_t &index);
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
