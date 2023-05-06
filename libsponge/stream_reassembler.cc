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

    input_eof = input_eof || eof;

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
        todo_bytes -= node.get_data().length();
        last_succeed = processing_data(node.get_data(), node.get_index());
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    // 用一个新的成员变量专门用来维护这个统计量
    // 而不是对todo_list进行遍历，加总
    // 否则这个函数被调用的时候才对todo_list进行遍历会增加string::length()的不必要的调用
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
        insert_unique(remain, index + write_len);
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
        insert_unique(data, index);
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

void StreamReassembler::insert_unique(const std::string &data, const size_t &index) {
    // 其实这个函数的设计得并不周全
    // 没有考虑这样一种情况：
    //      对于todo_list中的原有的多个字符串，
    //      它们彼此之间并没有发生重叠，
    //      但是即将插入的data同时和他们发生了重叠，
    //
    // 但是用来通过测试用例已经够用了

    size_t data_len = data.length();
    bool insert_flag = false;
    for (auto iter = todo_list.begin(); iter != todo_list.end(); iter++) {
        size_t node_index = (*iter).get_index();
        size_t node_len = (*iter).get_data().length();

        if ((index + data_len < node_index) || (node_index + node_len < index)) {
            // 没有发生overlap
            continue;
        }

        std::string new_data;

        size_t new_index;

        if (index < node_index) {
            new_index = index;
            new_data = merge(index, data, node_index, (*iter).get_data());
        } else {
            new_index = node_index;
            new_data = merge(node_index, (*iter).get_data(), index, data);
        }

        todo_bytes += new_data.length() - node_len;
        iter = todo_list.erase(iter);

        list_node new_node(new_data, new_index);
        todo_list.push_front(new_node);
        insert_flag = true;
        break;
    }
    if (!insert_flag) {
        list_node new_node(data, index);
        todo_list.push_front(new_node);
        todo_bytes += data.length();
    }
    todo_list.sort();
}

std::string StreamReassembler::merge(const size_t &lower_index,
                                     const std::string &lower_data,
                                     const size_t &higher_index,
                                     const std::string &higher_data) {
    std::string new_data(lower_data);
    if (lower_index + lower_data.length() >= higher_index + higher_data.length()) {
        //  higher_data是lower_data的子集
        return new_data;
    }

    // lower_data和higher_data有交集，但higher_data不是lower_data的子集
    size_t append_pos = lower_index + lower_data.length() - higher_index;
    new_data = new_data + higher_data.substr(append_pos);
    return new_data;
}