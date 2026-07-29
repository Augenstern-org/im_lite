#include "common/message.h"

#include "test_support.h"

#include <iostream>
#include <string>
#include <utility>

int main() {
    // Message 字段为 std::string（持有所有权，可移动）
    Message base;
    base.from_uid_      = "alice";
    base.to_uid_        = "bob";
    base.chat_type_     = types::ChatTypes::single;
    base.client_msg_id_ = "msg-001";
    base.msg_type_      = types::MessageTypes::text;

    CHECK(base.from_uid_ == "alice");
    CHECK(base.to_uid_ == "bob");

    // content_ 未赋值 —— is_valid() 要求六个字段全部非默认
    CHECK(!base.is_valid());

    Message req;
    req.from_uid_      = "alice";
    req.to_uid_        = "HiNana";
    req.chat_type_     = types::ChatTypes::group;
    req.client_msg_id_ = "msg-002";
    req.msg_type_      = types::MessageTypes::text;
    req.content_       = "hello world";

    CHECK(req.from_uid_ == "alice");
    CHECK(req.content_ == "hello world");
    CHECK(req.is_valid());

    // 枚举默认值 _init_ 单独构成非法 —— 字符串齐备也不放行
    Message no_chat_type = req;
    no_chat_type.chat_type_ = types::ChatTypes::_init_;
    CHECK(!no_chat_type.is_valid());

    Message no_msg_type = req;
    no_msg_type.msg_type_ = types::MessageTypes::_init_;
    CHECK(!no_msg_type.is_valid());

    // 默认构造的 Message 一定非法
    CHECK(!Message{}.is_valid());

    // 移动语义：Message 按值对象建模
    Message moved = std::move(req);
    CHECK(moved.content_ == "hello world");
    // req 处于合法但未指定状态；std::string 移动后为空
    CHECK(req.content_.empty());

    std::cout << "test_message: PASSED\n";
    return 0;
}
