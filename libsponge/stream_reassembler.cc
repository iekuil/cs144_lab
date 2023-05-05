#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// 需要对list_node类里面的方法给出具体实现
list_node::list_node(const std::string &d, const uint64_t i) : data(d), index(i) {}

std::string &list_node::get_data() { return data; }

uint64_t &list_node::get_index() { return index; }

bool list_node::operator<(list_node another) {
    if (index < another.get_index()) {
        return true;
    } else {
        return false;
    }
}

// 这个构造函数除了初始化原有的变量之外，还要初始化todo_list
StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), todo_list(), todo_bytes(0), input_eof(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 若index是预期的下个字节的序号，通过ByteSream::remaining_capacity()检查字节流的剩余空间
    //  通过string.length()，若剩余空间充足，直接将整个string写入_outdata
    //  接下来检查todo_list中是否存在可以继续写入的字节
    // 若剩余空间不足，本地声明一个新的string，并用data未能写入的后半截进行初始化，并将这个新的string加入todo_list
    //

    // 所有可能的情况：
    //     1. data中的位置处在后面一段的位置 -> 加入todo_list
    //     2. data中的index恰好是预期的next_index -> 尝试写入bytestream， 写不完的部分加入todo_list
    //     3. data中的index小于next_index，并且 index+data.length < next_index,
    //        意味着整个data包含的所有字节都是重复的
    //        -> 不做任何操作
    //     4. data中的index小于next_index，并且 index+data.length >= next_index,
    //        意味着data和之前接受过的字节有重叠，但是也存在未接收的部分
    //        -> 对于未接收的部分进行2的操作

    // 同时在处理完data后，需要检查todo_list
    // 对于todo_list中的每一个node，都有可能发生上面的四种情况
    // 还需要结合bytestream的remaining_capacity进行判断，当剩余空间不足时，不再对后续的list_node进行操作

    input_eof = eof;

    processing_data(data, index);

    bool last_succeed = true;

    while (1) {
        if (empty()) {
            if (input_eof) {
                _output.end_input();
            }
            break;
        }
        if (!last_succeed) {
            break;
        }
        list_node node = todo_list.front();
        todo_list.pop_front();
        last_succeed = processing_data(node.get_data(), node.get_index());
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    // 纠结是要增加一个新的成员变量专门用来维护这个统计量
    // 还是对todo_list进行遍历，加总

    // 还是应该用一个变量来维护这个值，
    // 否则这个函数被调用的时候才对todo_list进行遍历似乎会增加string::length()的不必要的调用
    return todo_bytes;
}

bool StreamReassembler::empty() const {
    if (todo_bytes == 0) {
        return true;
    } else {
        return false;
    }
}

// 将data写到输出流中，
// 当输出流的剩余可写空间不足时，将data的剩余部分加入到todo_list中
bool StreamReassembler::write_to_bytestream(const std::string &data, const size_t &index) {
    size_t data_len = data.length();
    size_t write_len = _output.write(data);
    bool available = true;
    if (write_len < data_len) {
        std::string remain(data, write_len);
        list_node node(remain, index + write_len);
        todo_list.push_front(node);
        todo_list.sort();
        todo_bytes += data_len - write_len;
        available = false;
    }
    return available;
}

bool StreamReassembler::processing_data(const std::string &data, const size_t &index) {
    // 通过调用ByteStream::bytes_writen()可以得到当前总写入的字节数
    size_t next_index = _output.bytes_written();
    size_t data_len = data.length();

    // 提前接收到了顺序排在后面的字节，先加入todo_list
    if (index > next_index) {
        list_node node(data, index);
        todo_list.push_front(node);
        todo_list.sort();
        todo_bytes += data_len;
        return false;
    }

    // 当前的data的首字节恰好是期待的下一个字节
    if (index == next_index) {
        return write_to_bytestream(data, next_index);
    }

    // data中的所有字节都已经接收过了
    if (index + data_len < next_index) {
        return true;
    } else {
        std::string valid_part(data, next_index - index);
        return write_to_bytestream(valid_part, next_index);
    }
}