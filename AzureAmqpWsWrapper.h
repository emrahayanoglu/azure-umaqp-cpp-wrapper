//
// Created by emrah on 12.09.2023.
//

#ifndef AZURE_UAMQP_CPP_WRAPPER_AZUREAMQPWSWRAPPER_H
#define AZURE_UAMQP_CPP_WRAPPER_AZUREAMQPWSWRAPPER_H

#include <azure_c_shared_utility/gballoc.h>
#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/sastoken.h>
#include <azure_c_shared_utility/wsio.h>
#include <azure_c_shared_utility/tlsio.h>
#include <azure_uamqp_c/amqp_management.h>
#include <azure_uamqp_c/uamqp.h>

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
#include <stdexcept>


class AzureAmqpWsWrapper {
public:
    AzureAmqpWsWrapper();

    ~AzureAmqpWsWrapper() = default;

    //void setConfiguration(const session::Configuration &value);
    void connect();

    void send(const std::string &message);

    void close();

    void set_message_callback(const std::function<void()> &function);

    //virtual void set_auth(bool value);
    void sent_message();

private:
    std::string host_{};
    std::string device_key_{};
    std::string device_id_{};
    bool auth_{false};
    //TODO maybe add better mechanism to tell thread to quit
    std::atomic<bool> stop_{false};
    unsigned int messages_pending_{0};
    std::function<void()> received_function_;

    LINK_HANDLE sender_link_;
    LINK_HANDLE receiver_link_;
    SESSION_HANDLE session_;
    CONNECTION_HANDLE connection_;
    XIO_HANDLE ws_io_;
    XIO_HANDLE sasl_io_;
    SASL_MECHANISM_HANDLE sasl_mechanism_handle_;
    CBS_HANDLE cbs_;
    MESSAGE_SENDER_HANDLE message_sender_ = nullptr;
    MESSAGE_RECEIVER_HANDLE message_receiver_ = nullptr;
    std::unique_ptr<std::thread> workerThread_{};

    static AMQP_VALUE on_message_received(const void *context, MESSAGE_HANDLE message);

    static void on_message_send_complete(void *context, MESSAGE_SEND_RESULT send_result, AMQP_VALUE delivery_state);

    void add_message_pending();

    void do_connection_work();
};


#endif //AZURE_UAMQP_CPP_WRAPPER_AZUREAMQPWSWRAPPER_H
