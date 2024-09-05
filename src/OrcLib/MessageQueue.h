//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <memory>

#include <agents.h>

#pragma managed(push, off)

/// <summary>
///     Simple queue class for storing messages.
/// </summary>
/// <typeparam name="_Type">
///     The payload type of messages stored in this queue.
/// </typeparam>
template <class _Type>
class MessageQueue
{
public:
    typedef Concurrency::message<_Type> _Message;

    /// <summary>
    ///     Constructs an initially empty queue.
    /// </summary>
    MessageQueue() {}

    /// <summary>
    ///     Removes and deletes any messages remaining in the queue.
    /// </summary>
    ~MessageQueue()
    {
        _Message* _Msg = dequeue();
        while (_Msg != NULL)
        {
            delete _Msg;
            _Msg = dequeue();
        }
    }

    /// <summary>
    ///     Add an item to the queue.
    /// </summary>
    /// <param name="_Msg">
    ///     Message to add.
    /// </param>
    void enqueue(_Message* _Msg) { _M_queue.push(_Msg); }

    /// <summary>
    ///     Dequeue an item from the head of queue.
    /// </summary>
    /// <returns>
    ///     Returns a pointer to the message found at the head of the queue.
    /// </returns>
    _Message* dequeue()
    {
        _Message* _Msg = NULL;

        if (!_M_queue.empty())
        {
            _Msg = _M_queue.front();
            _M_queue.pop();
        }

        return _Msg;
    }

    /// <summary>
    ///     Return the item at the head of the queue, without dequeuing
    /// </summary>
    /// <returns>
    ///     Returns a pointer to the message found at the head of the queue.
    /// </returns>
    _Message* peek() const
    {
        _Message* _Msg = NULL;

        if (!_M_queue.empty())
        {
            _Msg = _M_queue.front();
        }

        return _Msg;
    }

    /// <summary>
    ///     Returns the number of items currently in the queue.
    /// </summary>
    /// <returns>
    /// Size of the queue.
    /// </returns>
    size_t count() const { return _M_queue.size(); }

    /// <summary>
    ///     Checks to see if specified msg id is at the head of the queue.
    /// </summary>
    /// <param name="_MsgId">
    ///     Message id to check for.
    /// </param>
    /// <returns>
    ///     True if a message with specified id is at the head, false otherwise.
    /// </returns>
    bool is_head(const Concurrency::runtime_object_identity _MsgId) const
    {
        _Message* _Msg = peek();
        if (_Msg != NULL)
        {
            return _Msg->msg_id() == _MsgId;
        }
        return false;
    }

private:
    std::queue<_Message*> _M_queue;
};

#pragma managed(pop)
