//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <queue>
#include <concurrent_priority_queue.h>
#include <agents.h>

#pragma managed(push, off)

/// <summary>
///     Simple queue implementation that takes into account priority
///     using the comparison operator <.
/// </summary>
/// <typeparam name="_Type">
///     The payload type of messages stored in this queue.
/// </typeparam>
template <class _Type>
class PriorityQueue
{
public:
    /// <summary>
    ///     Constructs an initially empty queue.
    /// </summary>
    PriorityQueue()
        : _M_pHead(NULL)
        , _M_count(0)
    {
    }

    /// <summary>
    ///     Removes and deletes any messages remaining in the queue.
    /// </summary>
    ~PriorityQueue()
    {
        Concurrency::message<_Type>* _Msg = dequeue();
        while (_Msg != NULL)
        {
            delete _Msg;
            _Msg = dequeue();
        }
    }

    /// <summary>
    ///     Add an item to the queue, comparisons using the 'payload' field
    ///     will determine the location in the queue.
    /// </summary>
    /// <param name="_Msg">
    ///     Message to add.
    /// </param>
    /// <param name="fCanReplaceHead">
    ///     True if this new message can be inserted at the head.
    /// </param>
    void enqueue(Concurrency::message<_Type>* _Msg, const bool fInsertAtHead = true)
    {
        MessageNode* _Element = new MessageNode();
        _Element->_M_pMsg = _Msg;

        // Find location to insert.
        MessageNode* pCurrent = _M_pHead;
        MessageNode* pPrev = NULL;
        if (!fInsertAtHead && pCurrent != NULL)
        {
            pPrev = pCurrent;
            pCurrent = pCurrent->_M_pNext;
        }
        while (pCurrent != NULL)
        {
            if (*_Element->_M_pMsg->payload < *pCurrent->_M_pMsg->payload)
            {
                break;
            }
            pPrev = pCurrent;
            pCurrent = pCurrent->_M_pNext;
        }

        // Insert at head.
        if (pPrev == NULL)
        {
            _M_pHead = _Element;
        }
        else
        {
            pPrev->_M_pNext = _Element;
        }

        // Last item in queue.
        if (pCurrent == NULL)
        {
            _Element->_M_pNext = NULL;
        }
        else
        {
            _Element->_M_pNext = pCurrent;
        }

        ++_M_count;
    }

    /// <summary>
    ///     Dequeue an item from the head of queue.
    /// </summary>
    /// <returns>
    ///     Returns a pointer to the message found at the head of the queue.
    /// </returns>
    Concurrency::message<_Type>* dequeue()
    {
        if (_M_pHead == NULL)
        {
            return NULL;
        }

        MessageNode* _OldHead = _M_pHead;
        Concurrency::message<_Type>* _Result = _OldHead->_M_pMsg;

        _M_pHead = _OldHead->_M_pNext;

        delete _OldHead;

        if (--_M_count == 0)
        {
            _M_pHead = NULL;
        }
        return _Result;
    }

    /// <summary>
    ///     Return the item at the head of the queue, without dequeuing
    /// </summary>
    /// <returns>
    ///     Returns a pointer to the message found at the head of the queue.
    /// </returns>
    Concurrency::message<_Type>* peek() const
    {
        if (_M_count != 0)
        {
            return _M_pHead->_M_pMsg;
        }
        return NULL;
    }

    /// <summary>
    ///     Returns the number of items currently in the queue.
    /// </summary>
    /// <returns>
    ///     Size of the queue.
    /// </returns>
    size_t count() const { return _M_count; }

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
        if (_M_count != 0)
        {
            return _M_pHead->_M_pMsg->msg_id() == _MsgId;
        }
        return false;
    }

private:
    // Used to store individual message nodes.
    struct MessageNode
    {
        MessageNode()
            : _M_pMsg(NULL)
            , _M_pNext(NULL)
        {
        }
        Concurrency::message<_Type>* _M_pMsg;
        MessageNode* _M_pNext;
    };

    // A pointer to the head of the queue.
    MessageNode* _M_pHead;

    // The number of elements presently stored in the queue.
    size_t _M_count;
};

/// <summary>
///        PriorityBuffer is a buffer that uses a comparison operator on the 'payload' of each message to determine
///     order when offering to targets. Besides this it acts exactly like an unbounded_buffer.
/// </summary>
/// <typeparam name="_Type">
///     The payload type of messages stored and propagated by the buffer.
/// </typeparam>
template <class _Type>
class PriorityBuffer
    : public Concurrency::propagator_block<
          Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
          Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>
{
public:
    using filter_method = std::function<bool(_Type const&)>;

    /// <summary>
    ///     Creates an PriorityBuffer within the default scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    PriorityBuffer()
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target();
    }
    /// <summary>
    ///     Creates an PriorityBuffer within the default scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    /// <param name="_Filter">
    ///     A reference to a filter function.
    /// </param>
    PriorityBuffer(filter_method const& _Filter)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target();
        register_filter(_Filter);
    }
    /// <summary>
    ///     Creates an PriorityBuffer within the specified scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    /// <param name="_PScheduler">
    ///     A reference to a scheduler instance.
    /// </param>
    PriorityBuffer(Concurrency::Scheduler& _PScheduler)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target(&_PScheduler);
    }

    /// <summary>
    ///     Creates an PriorityBuffer within the specified scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    /// <param name="_PScheduler">
    ///     A reference to a scheduler instance.
    /// </param>
    /// <param name="_Filter">
    ///     A reference to a filter function.
    /// </param>
    PriorityBuffer(Concurrency::Scheduler& _PScheduler, filter_method const& _Filter)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target(&_PScheduler);
        register_filter(_Filter);
    }

    /// <summary>
    ///     Creates an PriorityBuffer within the specified schedule group.  The scheduler is implied
    ///     by the schedule group.
    /// </summary>
    /// <param name="_PScheduleGroup">
    ///     A reference to a schedule group.
    /// </param>
    PriorityBuffer(Concurrency::ScheduleGroup& _PScheduleGroup)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::
            initialize_source_and_target(NULL, &_PScheduleGroup);
    }

    /// <summary>
    ///     Creates an PriorityBuffer within the specified schedule group.  The scheduler is implied
    ///     by the schedule group.
    /// </summary>
    /// <param name="_PScheduleGroup">
    ///     A reference to a schedule group.
    /// </param>
    /// <param name="_Filter">
    ///     A reference to a filter function.
    /// </param>
    PriorityBuffer(Concurrency::ScheduleGroup& _PScheduleGroup, filter_method const& _Filter)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::
            initialize_source_and_target(NULL, &_PScheduleGroup);
        register_filter(_Filter);
    }

    /// <summary>
    ///     Cleans up any resources that may have been created by the PriorityBuffer.
    /// </summary>
    ~PriorityBuffer()
    {
        // Remove all links
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::remove_network_links();
    }

    /// <summary>
    ///     Add an item to the PriorityBuffer
    /// </summary>
    /// <param name="_Item">
    ///     A reference to the item to add.
    /// </param>
    /// <returns>
    ///     A boolean indicating whether the data was accepted.
    /// </returns>
    bool enqueue(_Type const& _Item) { return Concurrency::send<_Type>(this, _Item); }

    /// <summary>
    ///     Remove an item from the PriorityBuffer
    /// </summary>
    /// <returns>
    ///     The message payload.
    /// </returns>
    _Type dequeue() { return Concurrency::receive<_Type>(this); }

protected:
    /// <summary>
    ///     The main propagate() function for ITarget blocks.  Called by a source
    ///     block, generally within an asynchronous task to send messages to its targets.
    /// </summary>
    /// <param name="_PMessage">
    ///     A pointer to the message.
    /// </param>
    /// <param name="_PSource">
    ///     A pointer to the source block offering the message.
    /// </param>
    /// <returns>
    ///     An indication of what the target decided to do with the message.
    /// </returns>
    /// <remarks>
    ///     It is important that calls to propagate do *not* take the same lock on the
    ///     internal structure that is used by Consume and the LWT.  Doing so could
    ///     result in a deadlock with the Consume call.  (in the case of the PriorityBuffer,
    ///     this lock is the m_internalLock)
    /// </remarks>
    virtual Concurrency::message_status
    propagate_message(Concurrency::message<_Type>* _PMessage, Concurrency::ISource<_Type>* _PSource)
    {
        Concurrency::message_status _Result = Concurrency::accepted;
        //
        // Accept the message being propagated
        // Note: depending on the source block propagating the message
        // this may not necessarily be the same message (pMessage) first
        // passed into the function.
        //
        _PMessage = _PSource->accept(_PMessage->msg_id(), this);

        if (_PMessage != NULL)
        {
            Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::async_send(_PMessage);
        }
        else
        {
            _Result = Concurrency::missed;
        }

        return _Result;
    }

    /// <summary>
    ///     Synchronously sends a message to this block.  When this function completes the message will
    ///     already have propagated into the block.
    /// </summary>
    /// <param name="_PMessage">
    ///     A pointer to the message.
    /// </param>
    /// <param name="_PSource">
    ///     A pointer to the source block offering the message.
    /// </param>
    /// <returns>
    ///     An indication of what the target decided to do with the message.
    /// </returns>
    virtual Concurrency::message_status
    send_message(Concurrency::message<_Type>* _PMessage, Concurrency::ISource<_Type>* _PSource)
    {
        _PMessage = _PSource->accept(_PMessage->msg_id(), this);

        if (_PMessage != NULL)
        {
            Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::sync_send(_PMessage);
        }
        else
        {
            return Concurrency::missed;
        }

        return Concurrency::accepted;
    }

    /// <summary>
    ///     Accepts an offered message by the source, transferring ownership to the caller.
    /// </summary>
    /// <param name="_MsgId">
    ///     The runtime object identity of the message.
    /// </param>
    /// <returns>
    ///     A pointer to the message that the caller now has ownership of.
    /// </returns>
    virtual Concurrency::message<_Type>* accept_message(Concurrency::runtime_object_identity _MsgId)
    {
        //
        // Peek at the head message in the message buffer.  If the Ids match
        // dequeue and transfer ownership
        //
        Concurrency::message<_Type>* _Msg = NULL;

        if (_M_messageBuffer.is_head(_MsgId))
        {
            _Msg = _M_messageBuffer.dequeue();
        }

        return _Msg;
    }

    /// <summary>
    ///     Reserves a message previously offered by the source.
    /// </summary>
    /// <param name="_MsgId">
    ///     The runtime object identity of the message.
    /// </param>
    /// <returns>
    ///     A Boolean indicating whether the reservation worked or not.
    /// </returns>
    /// <remarks>
    ///     After 'reserve' is called, either 'consume' or 'release' must be called.
    /// </remarks>
    virtual bool reserve_message(Concurrency::runtime_object_identity _MsgId)
    {
        // Allow reservation if this is the head message
        return _M_messageBuffer.is_head(_MsgId);
    }

    /// <summary>
    ///     Consumes a message that was reserved previously.
    /// </summary>
    /// <param name="_MsgId">
    ///     The runtime object identity of the message.
    /// </param>
    /// <returns>
    ///     A pointer to the message that the caller now has ownership of.
    /// </returns>
    /// <remarks>
    ///     Similar to 'accept', but is always preceded by a call to 'reserve'.
    /// </remarks>
    virtual Concurrency::message<_Type>* consume_message(Concurrency::runtime_object_identity _MsgId)
    {
        // By default, accept the message
        return accept_message(_MsgId);
    }

    /// <summary>
    ///     Releases a previous message reservation.
    /// </summary>
    /// <param name="_MsgId">
    ///     The runtime object identity of the message.
    /// </param>
    virtual void release_message(Concurrency::runtime_object_identity _MsgId)
    {
        // The head message is the one reserved.
        if (!_M_messageBuffer.is_head(_MsgId))
        {
            throw Concurrency::message_not_found();
        }
    }

    /// <summary>
    ///    Resumes propagation after a reservation has been released
    /// </summary>
    virtual void resume_propagation()
    {
        // If there are any messages in the buffer, propagate them out
        if (_M_messageBuffer.count() > 0)
        {
            Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::async_send(NULL);
        }
    }

    /// <summary>
    ///     Notification that a target was linked to this source.
    /// </summary>
    /// <param name="_PTarget">
    ///     A pointer to the newly linked target.
    /// </param>
    virtual void link_target_notification(Concurrency::ITarget<_Type>* _PTarget)
    {
        // If the message queue is blocked due to reservation
        // there is no need to do any message propagation
        if (Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::_M_pReservedFor
            != NULL)
        {
            return;
        }

        Concurrency::message<_Type>* _Msg = _M_messageBuffer.peek();

        if (_Msg != NULL)
        {
            // Propagate the head message to the new target
            Concurrency::message_status _Status = _PTarget->propagate(_Msg, this);

            if (_Status == Concurrency::accepted)
            {
                // The target accepted the message, restart propagation.
                propagate_to_any_targets(NULL);
            }

            // If the status is anything other than accepted, then leave
            // the message queue blocked.
        }
    }

    /// <summary>
    /// Takes the message and propagates it to all the targets of this PriorityBuffer.
    /// </summary>
    /// <param name="_PMessage">
    ///     A pointer to a new message.
    /// </param>
    virtual void propagate_to_any_targets(Concurrency::message<_Type>* _PMessage)
    {
        // Enqueue pMessage to the internal unbounded buffer queue if it is non-NULL.
        // _PMessage can be NULL if this LWT was the result of a Repropagate call
        // out of a Consume or Release (where no new message is queued up, but
        // everything remaining in the PriorityBuffer needs to be propagated out)
        if (_PMessage != NULL)
        {
            Concurrency::message<_Type>* pPrevHead = _M_messageBuffer.peek();

            // If a reservation is held make sure to not insert this new
            // message before it.
            if (Concurrency::propagator_block<
                    Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                    Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::_M_pReservedFor
                != NULL)
            {
                _M_messageBuffer.enqueue(_PMessage, false);
            }
            else
            {
                _M_messageBuffer.enqueue(_PMessage);
            }

            // If the head message didn't change, we can safely assume that
            // the head message is blocked and waiting on Consume(), Release() or a new
            // link_target()
            if (pPrevHead != NULL && !_M_messageBuffer.is_head(pPrevHead->msg_id()))
            {
                return;
            }
        }

        // Attempt to propagate messages to all the targets
        _Propagate_priority_order();
    }

private:
    /// <summary>
    ///     Attempts to propagate out any messages currently in the block.
    /// </summary>
    void _Propagate_priority_order()
    {
        auto* _Msg = _M_messageBuffer.peek();

        // If someone has reserved the _Head message, don't propagate anymore
        if (Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::_M_pReservedFor
            != NULL)
        {
            return;
        }

        while (_Msg != NULL)
        {
            Concurrency::message_status _Status = Concurrency::declined;

            // Always start from the first target that linked.
            for (auto _Iter =
                     Concurrency::propagator_block<
                         Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                         Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::_M_connectedTargets.begin();
                 *_Iter != NULL;
                 ++_Iter)
            {
                auto* _PTarget = *_Iter;
                _Status = _PTarget->propagate(_Msg, this);

                // Ownership of message changed. Do not propagate this
                // message to any other target.
                if (_Status == Concurrency::accepted)
                {
                    break;
                }

                // If the target just propagated to reserved this message, stop
                // propagating it to others.
                if (Concurrency::propagator_block<
                        Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                        Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::_M_pReservedFor
                    != NULL)
                {
                    break;
                }
            }

            // If status is anything other than accepted, then the head message
            // was not propagated out.  Thus, nothing after it in the queue can
            // be propagated out.  Cease propagation.
            if (_Status != Concurrency::accepted)
            {
                break;
            }

            // Get the next message
            _Msg = _M_messageBuffer.peek();
        }
    }

    /// <summary>
    ///     Priority Queue used to store messages.
    /// </summary>
    PriorityQueue<_Type> _M_messageBuffer;
    //
    // Hide assignment operator and copy constructor.
    //
    PriorityBuffer const& operator=(PriorityBuffer const&) = delete;  // no assignment operator
    PriorityBuffer(PriorityBuffer const&) = delete;  // no copy constructor
};
#pragma managed(pop)
