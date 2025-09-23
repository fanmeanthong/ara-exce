// SomeipBinding.h
#pragma once
#include "AraCom_Skeleton.h"
#include <vsomeip/vsomeip.hpp>
#include <memory>
#include <map>
#include <thread>
#include <iostream>

#define RADAR_SERVICE_ID    0x1234 // Service ID for Radar
#define RADAR_INSTANCE_ID   0x5678 // Instance ID for Radar
#define CALIBRATE_METHOD_ID 0x42   // Method ID for Calibrate

namespace ara {
namespace com {

// Implementation of Skeleton using SOME/IP protocol
class SomeipSkeleton : public Skeleton {
    std::shared_ptr<vsomeip::application> app_; // SOME/IP application instance
    std::function<void(const Message&)> handler_; // Callback to handle incoming messages

    // Store the original request to create a correct response context
    std::map<MethodId, std::shared_ptr<vsomeip::message>> last_requests_;

public:
    // Constructor: create SOME/IP application and set message handler callback
    SomeipSkeleton(const std::string& name, std::function<void(const Message&)> cb)
        : handler_(cb) {
        app_ = vsomeip::runtime::get()->create_application(name);
    }

    // Start offering the service and register message handler for a specific method
    void OfferService() override {
        app_->init();

        // Register handler for CALIBRATE_METHOD_ID (0x42)
        app_->register_message_handler(RADAR_SERVICE_ID, RADAR_INSTANCE_ID, CALIBRATE_METHOD_ID,
            [this](const std::shared_ptr<vsomeip::message>& req) {
                // Save the original request for response context
                last_requests_[req->get_method()] = req;

                // Convert SOME/IP message to generic Message and invoke user handler
                Message m;
                m.method = req->get_method();
                auto pl = req->get_payload();
                m.payload.assign(pl->get_data(), pl->get_data() + pl->get_length());
                handler_(m);
            });

        // Offer the service to clients
        app_->offer_service(RADAR_SERVICE_ID, RADAR_INSTANCE_ID);
        // Start the SOME/IP application in a separate thread
        std::thread([&] { app_->start(); }).detach();
    }

    // Stop offering the service
    void StopOfferService() override {
        app_->stop_offer_service(RADAR_SERVICE_ID, RADAR_INSTANCE_ID);
    }

    // Send a response to the client for a specific method
    void SendResponse(const Message& msg) override {
        if (!last_requests_.count(msg.method)) {
            std::cerr << "[Server] No request found for method " << msg.method << std::endl;
            return;
        }

        auto req = last_requests_[msg.method];
        auto resp = vsomeip::runtime::get()->create_response(req);

        // Set service, instance, and method IDs for the response
        resp->set_service(RADAR_SERVICE_ID);
        resp->set_instance(RADAR_INSTANCE_ID);
        resp->set_method(msg.method);

        // Set the response payload
        auto pl = vsomeip::runtime::get()->create_payload();
        pl->set_data(msg.payload.data(), msg.payload.size());
        resp->set_payload(pl);

        // Send the response message
        app_->send(resp);
    }

    // Send an event notification to all subscribed clients
    void SendEvent(EventId event, const std::vector<uint8_t>& data) override {
        auto pl = vsomeip::runtime::get()->create_payload();
        pl->set_data(data.data(), data.size());
        app_->notify(RADAR_SERVICE_ID, RADAR_INSTANCE_ID, event, pl);
    }
};

// Implementation of Proxy using SOME/IP protocol
class SomeipProxy : public Proxy {
    std::shared_ptr<vsomeip::application> app_; // SOME/IP application instance
    std::map<EventId, std::function<void(const std::vector<uint8_t>&)>> event_callbacks_; // Event callbacks
    std::map<MethodId, std::function<void(const std::vector<uint8_t>&)>> response_callbacks_; // Response callbacks

public:
    // Constructor: create SOME/IP application
    SomeipProxy(const std::string& name) {
        app_ = vsomeip::runtime::get()->create_application(name);
    }

    // Request a service and register response handler for a specific method
    void FindService(InstanceIdentifier instance) override {
        app_->init();
        app_->request_service(RADAR_SERVICE_ID, instance);

        // Register handler for CALIBRATE_METHOD_ID response
        app_->register_message_handler(RADAR_SERVICE_ID, RADAR_INSTANCE_ID, CALIBRATE_METHOD_ID,
            [this](const std::shared_ptr<vsomeip::message>& resp) {
                auto pl = resp->get_payload();
                std::vector<uint8_t> data(pl->get_data(), pl->get_data() + pl->get_length());
                // Invoke registered response callback if available
                if (response_callbacks_.count(CALIBRATE_METHOD_ID))
                    response_callbacks_[CALIBRATE_METHOD_ID](data);
            });

        // Start the SOME/IP application in a separate thread
        std::thread([&] { app_->start(); }).detach();
    }

    // Release the requested service
    void StopFindService(InstanceIdentifier instance) override {
        app_->release_service(RADAR_SERVICE_ID, instance);
    }

    // Send a method call request to the server
    void MethodCall(MethodId method, const std::vector<uint8_t>& req) override {
        auto msg = vsomeip::runtime::get()->create_request();
        msg->set_service(RADAR_SERVICE_ID);
        msg->set_instance(RADAR_INSTANCE_ID);
        msg->set_method(method);

        // Set the request payload
        auto pl = vsomeip::runtime::get()->create_payload();
        pl->set_data(req.data(), req.size());
        msg->set_payload(pl);

        // Send the request message
        app_->send(msg);
    }

    // Subscribe to an event and register a callback to handle event data
    void SubscribeEvent(EventId event, std::function<void(const std::vector<uint8_t>&)> cb) override {
        event_callbacks_[event] = cb;
        app_->register_message_handler(RADAR_SERVICE_ID, RADAR_INSTANCE_ID, event,
            [this, event](const std::shared_ptr<vsomeip::message>& msg) {
                auto pl = msg->get_payload();
                std::vector<uint8_t> data(pl->get_data(), pl->get_data() + pl->get_length());
                event_callbacks_[event](data);
            });
        // Subscribe to the event
        app_->subscribe(RADAR_SERVICE_ID, RADAR_INSTANCE_ID, event);
    }

    // Register a callback to handle responses for a specific method
    void RegisterResponseHandler(MethodId method, std::function<void(const std::vector<uint8_t>&)> cb) override {
        response_callbacks_[method] = cb;
    }
};

} // namespace com
} // namespace ara
