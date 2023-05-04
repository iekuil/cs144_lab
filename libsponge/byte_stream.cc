#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// 这个鬼地方有风格要求，一定得用成员初始化列表
// 不能在函数体里面赋值
ByteStream::ByteStream(const size_t capacity)
    : buffer(capacity + 1, '\0')  // 环状队列，可写部分的最后一个元素始终是不用的
    , buf_size(capacity + 1)
    , rpointer(0)
    , wpointer(0)
    , input_end_flag(0)
    , read_count(0)
    , write_count(0)
    , unused_capacity(capacity) {  // 在发生读写时，手动改变剩余空间的数量
}

size_t ByteStream::write(const string &data) {

    size_t count;
    size_t data_len = data.length();

    // 从wpointer所指处开始写入环形队列，并更新wpointer
    // 约束：写指针没有越过读指针；当前访问的data元素下标没有超出data的总长度
    for (count = 0; (rpointer != (wpointer + 1) % buf_size) && (count < data_len); count++) {
        buffer[wpointer] = data[count];
        wpointer = (wpointer + 1) % buf_size;
    }

    // 更新总计写入数量
    write_count += count;

    // 更新可写空间的容量
    unused_capacity -= count;
    return count;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {

    std::string res;
    size_t count;

    // 从rpointer指针处开始读取数据，附加到字符串末尾
    // 约束：在buffer中所读取的区域没有越过写指针；
    for (count = 0; ((rpointer + count) % buf_size != wpointer) && (count < len); count++) {
        res.insert(res.end(), buffer[(rpointer + count) % buf_size]);
    }
    return res;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    // 更新读指针
    rpointer = (rpointer + len) % buf_size;

    // 更新可写空间容量
    unused_capacity += len;

    // 更新累计读取数量
    read_count += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string

// 这个函数在给定的模板里面一开始没有定义成成员函数ByteStream::read，
// 而是普通的全局函数read()
// 导致编译出错
std::string ByteStream::read(const size_t len) {

    std::string res = peek_output(len);
    size_t res_len = res.length();
    pop_output(res_len);

    return res;
}

void ByteStream::end_input() { input_end_flag = 1; }

bool ByteStream::input_ended() const { return input_end_flag; }

size_t ByteStream::buffer_size() const { return buf_size - 1 - unused_capacity; }

bool ByteStream::buffer_empty() const {
    return rpointer == wpointer;
}

bool ByteStream::eof() const { return buffer_empty() && input_ended(); }

size_t ByteStream::bytes_written() const { return write_count; }

size_t ByteStream::bytes_read() const { return read_count; }

size_t ByteStream::remaining_capacity() const { return unused_capacity; }
