#include "common/message.h"

#include <cassert>
#include <iostream>
#include <string>
#include <utility>

int main() {
    // MessageBase 字段为 std::string（持有所有权，可移动）
    types::MessageBase base;
    base.from_uid_       = "alice";
    base.to_uid_         = "bob";
    base.chat_type_      = types::ChatTypes::single;
    base.client_msg_id_  = "msg-001";
    base.msg_type_       = types::MessageTypes::text;

    assert(base.from_uid_ == "alice");
    assert(base.to_uid_   == "bob");

    // RequestMsg 继承 MessageBase 并增加 content_
    types::RequestMsg req;
    req.from_uid_       = "alice";
    req.to_uid_         = "HiNana";
    req.chat_type_      = types::ChatTypes::group;
    req.client_msg_id_  = "msg-002";
    req.msg_type_       = types::MessageTypes::text;
    req.content_        = "hello world";

    assert(req.from_uid_ == "alice");
    assert(req.content_  == "hello world");

    // ResponseMsg 继承 MessageBase 并增加 server_seq_
    types::ResponseMsg resp;
    resp.from_uid_       = "bob";
    resp.to_uid_         = "alice";
    resp.server_seq_     = "seq-001";

    assert(resp.server_seq_ == "seq-001");

    // 移动语义：Message 按值对象建模
    types::RequestMsg moved = std::move(req);
    assert(moved.content_ == "hello world");
    // req 处于合法但未指定状态；std::string 移动后为空
    assert(req.content_.empty());

    std::cout << "test_message: PASSED\n";
    return 0;
}
