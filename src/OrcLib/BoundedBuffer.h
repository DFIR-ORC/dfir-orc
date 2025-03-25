//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <agents.h>

// bounded_buffer uses a map
#include <map>
#include <queue>

#include "MessageQueue.h"

#pragma managed(push, off)

/// <summary>
///    A BoundedBuffer implementation. Once the capacity is reached it will save the offered message
///    id and postpone. Once below capacity again the BoundedBuffer will try to reserve and consume
///    any of the postponed messages. Preference is given to previously offered messages before new ones.
///
///    NOTE: this BoundedBuffer implementation contains code that is very unique to this particular block.
///          Extreme caution should be taken if code is directly copy and pasted from this class. The BoundedBuffer
///          implementation uses a critical_section, several interlocked operations, and additional calls to async_send.
///          These are needed to not abandon a previously saved message id. Most blocks never have to deal with this
///          problem.
/// </summary>
/// <typeparam name="_Type">
///     The payload type of messages stored and propagated by the buffer.
/// </typeparam>
template <class _Type>
class BoundedBuffer
    : public Concurrency::propagator_block<
          Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
          Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>
{
public:
    using filter_method = std::function<bool(_Type const&)>;

    /// <summary>
    ///     Creates an BoundedBuffer within the default scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    BoundedBuffer(const size_t capacity)
        : _M_capacity(capacity)
        , _M_currentSize(0)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target();
    }

    /// <summary>
    ///     Creates an BoundedBuffer within the default scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    /// <param name="_Filter">
    ///     A reference to a filter function.
    /// </param>
    BoundedBuffer(const size_t capacity, filter_method const& _Filter)
        : _M_capacity(capacity)
        , _M_currentSize(0)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target();
        register_filter(_Filter);
    }

    /// <summary>
    ///     Creates an BoundedBuffer within the specified scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    /// <param name="_PScheduler">
    ///     A reference to a scheduler instance.
    /// </param>
    BoundedBuffer(const size_t capacity, Concurrency::Scheduler& _PScheduler)
        : _M_capacity(capacity)
        , _M_currentSize(0)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target(&_PScheduler);
    }

    /// <summary>
    ///     Creates an BoundedBuffer within the specified scheduler, and places it any schedule
    ///     group of the scheduler's choosing.
    /// </summary>
    /// <param name="_PScheduler">
    ///     A reference to a scheduler instance.
    /// </param>
    /// <param name="_Filter">
    ///     A reference to a filter function.
    /// </param>
    BoundedBuffer(const size_t capacity, Concurrency::Scheduler& _PScheduler, filter_method const& _Filter)
        : _M_capacity(capacity)
        , _M_currentSize(0)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target(&_PScheduler);
        register_filter(_Filter);
    }

    /// <summary>
    ///     Creates an BoundedBuffer within the specified schedule group.  The scheduler is implied
    ///     by the schedule group.
    /// </summary>
    /// <param name="_PScheduleGroup">
    ///     A reference to a schedule group.
    /// </param>
    BoundedBuffer(const size_t capacity, Concurrency::ScheduleGroup& _PScheduleGroup)
        : _M_capacity(capacity)
        , _M_currentSize(0)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target(NULL, &_PScheduleGroup);
    }

    /// <summary>
    ///     Creates an BoundedBuffer within the specified schedule group.  The scheduler is implied
    ///     by the schedule group.
    /// </summary>
    /// <param name="_PScheduleGroup">
    ///     A reference to a schedule group.
    /// </param>
    /// <param name="_Filter">
    ///     A reference to a filter function.
    /// </param>
    BoundedBuffer(const size_t capacity, Concurrency::ScheduleGroup& _PScheduleGroup, filter_method const& _Filter)
        : _M_capacity(capacity)
        , _M_currentSize(0)
    {
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::initialize_source_and_target(NULL, &_PScheduleGroup);
        register_filter(_Filter);
    }

    /// <summary>
    ///     Cleans up any resources that may have been used by the BoundedBuffer.
    /// </summary>
    ~BoundedBuffer()
    {
        // Remove all links
        Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::remove_network_links();
    }

    /// <summary>
    ///     Add an item to the BoundedBuffer.
    /// </summary>
    /// <param name="_Item">
    ///     A reference to the item to add.
    /// </param>
    /// <returns>
    ///     A boolean indicating whether the data was accepted.
    /// </returns>
    bool enqueue(_Type const& _Item) { return Concurrency::send<_Type>(this, _Item); }

    /// <summary>
    ///     Remove an item from the BoundedBuffer.
    /// </summary>
    /// <returns>
    ///     The message payload.
    /// </returns>
    _Type dequeue() { return receive<_Type>(this); }

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
    ///     result in a deadlock with the Consume call.  (in the case of the BoundedBuffer,
    ///     this lock is the m_internalLock)
    /// </remarks>
    virtual Concurrency::message_status
    propagate_message(Concurrency::message<_Type>* _PMessage, Concurrency::ISource<_Type>* _PSource)
    {
        Concurrency::message_status _Result = Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::accepted;

        // Check current capacity.
        if ((size_t)_InterlockedIncrement(&_M_currentSize) > _M_capacity)
        {
            // Postpone the message, buffer is full.
            _InterlockedDecrement(&_M_currentSize);
            _Result = Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::postponed;

            // Save off the message id from this source to later try
            // and reserve/consume when more space is free.
            {
                Concurrency::critical_section::scoped_lock scopedLock(_M_savedIdsLock);
                _M_savedSourceMsgIds[_PSource] = _PMessage->msg_id();
            }

            Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::async_send(NULL);
        }
        else
        {
            //
            // Accept the message being propagated
            // Note: depending on the source block propagating the message
            // this may not necessarily be the same message (pMessage) first
            // passed into the function.
            //
            _PMessage = _PSource->accept(_PMessage->msg_id(), this);

            if (_PMessage != NULL)
            {
                async_send(_PMessage);
            }
            else
            {
                // Didn't get a message so need to decrement.
                _InterlockedDecrement(&_M_currentSize);
                _Result = Concurrency::propagator_block<
                    Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                    Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::missed;
                Concurrency::propagator_block<
                    Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                    Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::async_send(NULL);
            }
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
        Concurrency::message_status _Result = Concurrency::propagator_block<
            Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
            Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::accepted;

        // Check current capacity.
        if ((size_t)_InterlockedIncrement(&_M_currentSize) > _M_capacity)
        {
            // Postpone the message, buffer is full.
            _InterlockedDecrement(&_M_currentSize);
            _Result = Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::postponed;

            // Save off the message id from this source to later try
            // and reserve/consume when more space is free.
            {
                Concurrency::critical_section::scoped_lock scopedLock(_M_savedIdsLock);
                _M_savedSourceMsgIds[_PSource] = _PMessage->msg_id();
            }

            Concurrency::propagator_block<
                Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::async_send(NULL);
        }
        else
        {
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
                // Didn't get a message so need to decrement.
                _InterlockedDecrement(&_M_currentSize);
                _Result = Concurrency::propagator_block<
                    Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                    Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::missed;
                Concurrency::propagator_block<
                    Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                    Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::async_send(NULL);
            }
        }

        return _Result;
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

            // Give preference to any previously postponed messages
            // before decrementing current size.
            if (!try_consume_msg())
            {
                _InterlockedDecrement(&_M_currentSize);
            }
        }

        return _Msg;
    }

    /// <summary>
    ///     Try to reserve and consume a message from list of saved message ids.
    /// </summary>
    /// <returns>
    ///     True if a message was sucessfully consumed, false otherwise.
    /// </returns>
    bool try_consume_msg()
    {
        Concurrency::runtime_object_identity _ReservedId = -1;
        Concurrency::ISource<_Type>* _PSource = NULL;

        // Walk through source links seeing if any saved ids exist.
        bool _ConsumedMsg = true;
        while (_ConsumedMsg)
        {
            auto _Iter = Concurrency::ISource<_Type>::_M_connectedSources.begin();
            {
                concurrency::critical_section::scoped_lock scopedLock(_M_savedIdsLock);
                for (; *_Iter != NULL; ++_Iter)
                {
                    _PSource = *_Iter;
                    //std::map<Concurrency::ISource<_Type>*, concurrency::runtime_object_identity>::iterator _MapIter;
                    auto _MapIter = _M_savedSourceMsgIds.find(_PSource);
                    if (_MapIter != _M_savedSourceMsgIds.end())
                    {
                        _ReservedId = _MapIter->second;
                        _M_savedSourceMsgIds.erase(_MapIter);
                        break;
                    }
                }
            }

            // Can't call into source block holding _M_savedIdsLock, that would be a recipe for disaster.
            if (_ReservedId != -1)
            {
                if (_PSource->reserve(_ReservedId, this))
                {
                    Concurrency::message<_Type>* _ConsumedMsg = _PSource->consume(_ReservedId, this);
                    async_send(_ConsumedMsg);
                    return true;
                }
                // Reserve failed go or link was removed,
                // go back and try and find a different msg id.
                else
                {
                    continue;
                }
            }

            // If this point is reached the map of source ids was empty.
            break;
        }

        return false;
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

            if (_Status
                == Concurrency::propagator_block<
                    Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                    Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::accepted)
            {
                // The target accepted the message, restart propagation.
                propagate_to_any_targets(NULL);
            }

            // If the status is anything other than accepted, then leave
            // the message queue blocked.
        }
    }

    /// <summary>
    ///     Takes the message and propagates it to all the targets of this BoundedBuffer.
    ///     This is called from async_send.
    /// </summary>
    /// <param name="_PMessage">
    ///     A pointer to a new message.
    /// </param>
    virtual void propagate_to_any_targets(Concurrency::message<_Type>* _PMessage)
    {
        // Enqueue pMessage to the internal message buffer if it is non-NULL.
        // pMessage can be NULL if this LWT was the result of a Repropagate call
        // out of a Consume or Release (where no new message is queued up, but
        // everything remaining in the bounded buffer needs to be propagated out)
        if (_PMessage != NULL)
        {
            _M_messageBuffer.enqueue(_PMessage);

            // If the incoming pMessage is not the head message, we can safely assume that
            // the head message is blocked and waiting on Consume(), Release() or a new
            // link_target() and cannot be propagated out.
            if (_M_messageBuffer.is_head(_PMessage->msg_id()))
            {
                _Propagate_priority_order();
            }
        }
        else
        {
            // While current size is less than capacity try to consume
            // any previously offered ids.
            bool _ConsumedMsg = true;
            while (_ConsumedMsg)
            {
                // Assume a message will be found to successfully consume in the
                // saved ids, if not this will be decremented afterwards.
                if ((size_t)_InterlockedIncrement(&_M_currentSize) > _M_capacity)
                {
                    break;
                }

                _ConsumedMsg = try_consume_msg();
            }

            // Decrement the current size, we broke out of the previous loop
            // because we reached capacity or there were no more messages to consume.
            _InterlockedDecrement(&_M_currentSize);
        }
    }

private:
    /// <summary>
    ///     Attempts to propagate out any messages currently in the block.
    /// </summary>
    void _Propagate_priority_order()
    {
        using namespace Concurrency;

        Concurrency::message<Concurrency::ITarget<_Type>>* _Msg = _M_messageBuffer.peek();

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
            for (auto _Iter = Concurrency::propagator_block<
                                  Concurrency::multi_link_registry<Concurrency::ITarget<_Type>>,
                                  Concurrency::multi_link_registry<Concurrency::ISource<_Type>>>::_M_connectedTargets.begin();
                 *_Iter != NULL;
                 ++_Iter)
            {
                Concurrency::message<Concurrency::ITarget<_Type>::_Target_type>* _PTarget = *_Iter;
                _Status = _PTarget->propagate(_Msg, this);

                // Ownership of message changed. Do not propagate this
                // message to any other target.
                if (_Status == accepted)
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
            if (_Status != accepted)
            {
                break;
            }

            // Get the next message
            _Msg = _M_messageBuffer.peek();
        }
    }

    /// <summary>
    ///     Message buffer used to store messages.
    /// </summary>
    MessageQueue<_Type> _M_messageBuffer;

    /// <summary>
    ///     Maximum number of messages BoundedBuffer can hold.
    /// </summary>
    const size_t _M_capacity;

    /// <summary>
    ///     Current number of messages in BoundedBuffer.
    /// </summary>
    volatile long _M_currentSize;

    /// <summary>
    ///     Lock used to guard saved message ids map.
    /// </summary>
    Concurrency::critical_section _M_savedIdsLock;

    /// <summary>
    ///     Map of source links to saved message ids.
    /// </summary>
    std::map<Concurrency::ISource<_Type>*, Concurrency::runtime_object_identity> _M_savedSourceMsgIds;

    //
    // Hide assignment operator and copy constructor
    //
    BoundedBuffer const& operator=(BoundedBuffer const&) = delete;  // no assignment operator
    BoundedBuffer(BoundedBuffer const&) = delete;  // no copy constructor
};

#pragma managed(pop)
