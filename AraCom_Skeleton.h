// AraCom_Skeleton.h
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace ara {
namespace com {

// Identifier for a service instance
using InstanceIdentifier = uint16_t;
// Identifier for a service method
using MethodId = uint16_t;
// Identifier for a service event
using EventId = uint16_t;

// Structure representing a message exchanged between client and server
struct Message {
    MethodId method; // ID of the related method for this message
    std::vector<uint8_t> payload; // Data payload of the message
};

// Skeleton interface representing the server/service side
class Skeleton {
public:
    // Start offering the service to clients
    virtual void OfferService() = 0;
    // Stop offering the service
    virtual void StopOfferService() = 0;
    // Send a response message to the client after processing a method
    virtual void SendResponse(const Message& msg) = 0;
    // Send an event to all subscribed clients
    virtual void SendEvent(EventId event, const std::vector<uint8_t>& data) = 0;
    // Virtual destructor to ensure proper resource cleanup
    virtual ~Skeleton() = default;
};

// Proxy interface representing the client side
class Proxy {
public:
    // Search for a service with a specific instance identifier
    virtual void FindService(InstanceIdentifier instance) = 0;
    // Stop searching for a service
    virtual void StopFindService(InstanceIdentifier instance) = 0;
    // Send a method call request to the server
    virtual void MethodCall(MethodId method, const std::vector<uint8_t>& req) = 0;
    // Subscribe to an event from the server, with a callback to handle event data
    virtual void SubscribeEvent(EventId event, std::function<void(const std::vector<uint8_t>&)> cb) = 0;
    // Register a callback to handle responses for a specific method
    virtual void RegisterResponseHandler(MethodId method,
        std::function<void(const std::vector<uint8_t>&)> cb) = 0;
    // Virtual destructor to ensure proper resource cleanup
    virtual ~Proxy() = default;
};

} // namespace com
} // namespace ara
