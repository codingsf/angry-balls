#include <iostream>

#include "message_manager.hpp"
#include "observers_manager.hpp"
#include "json_message.hpp"
#include "game_state_manager.hpp"

#include "protocol/parse_protocol.h"

namespace ab {

ConnectionId MessageManager::AddConnection(std::weak_ptr<mio::Connection> connection) {
    connections_.push_back(connection);
    return connections_.size() - 1;
}

std::weak_ptr<mio::Connection> MessageManager::GetConnection(ConnectionId connection) {
    return connections_.at(connection);
}

void MessageManager::ReceiveMessage(std::unique_ptr<Message> message, 
        std::weak_ptr<mio::Connection> connection) {
    ConnectionId connection_id = AddConnection(connection);
    DispatchMessage(std::move(message), connection_id);
}

void MessageManager::DispatchMessage(std::unique_ptr<Message > message, ConnectionId connection_id) {
    std::shared_ptr<ObserversManager> om(observers_manager_.lock());
    if (!om) {
        return;
    }

    if (dynamic_cast<ClientSubscribeRequestMessage*>(message.get())) {
        // ignore message content
        om->AddClient(connection_id);

    } else if (dynamic_cast<ViewerSubscribeRequestMessage*>(message.get())) {
        // ignore message content
        om->AddViewer(connection_id);

    } else if (dynamic_cast<TurnMessage*>(message.get())) {
        const TurnMessage* const turn_message = dynamic_cast<TurnMessage*>(message.get());
        assert(nullptr != turn_message);

        auto gsm = game_state_manager_.lock();
        if (gsm) {
            gsm->AddTurn(turn_message->turn);
        }
    } else {
        // incorrect message type
        std::cerr << "Incorrect message\n"; 
    }
}

void MessageManager::SendMessage(const Message &message, ConnectionId connection_id) {
    mio::Buffer message_buffer = JsonResponseBuilder::BuildJsonResponse(message);
    std::shared_ptr<mio::Connection> connection = GetConnection(connection_id).lock();

    if (connection) {
        connection->addOutput(message_buffer);
    }
}

} // namespace ab
