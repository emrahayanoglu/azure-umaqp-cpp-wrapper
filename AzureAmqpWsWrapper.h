//
// Created by emrah on 12.09.2023.
//

#ifndef AZURE_UAMQP_CPP_WRAPPER_AZUREAMQPWSWRAPPER_H
#define AZURE_UAMQP_CPP_WRAPPER_AZUREAMQPWSWRAPPER_H

#include <azure_c_shared_utility/azure_base64.h>
#include <azure_c_shared_utility/buffer_.h>
#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/strings.h>
#include <azure_c_shared_utility/sastoken.h>
#include <azure_c_shared_utility/wsio.h>
#include <azure_c_shared_utility/tlsio.h>
#include <azure_c_shared_utility/urlencode.h>
#include <azure_uamqp_c/uamqp.h>

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
#include <stdexcept>
#include <condition_variable>
#include <queue>

enum class AmqpState {
    AMQP_STATE_NOT_STARTED,
    AMQP_STATE_STARTING,
    AMQP_STATE_STARTED,
    AMQP_STATE_ERROR
};

enum class AmqpSenderReceiverState {
    AMQP_STATE_NOT_READY,
    AMQP_STATE_READY,
    AMQP_STATE_ERROR
};


class AzureAmqpWsWrapper {
public:
    AzureAmqpWsWrapper(const std::string &host,
                       const std::string &entity,
                       const std::string &key,
                       const std::string &key_name);

    ~AzureAmqpWsWrapper() = default;

    bool connect();

    void close();

    void start_loop();

    bool add_message_to_queue(const std::string &msg);

    /// Helper Functions
    static XIO_HANDLE create_wsio(const std::string &host);

    static STRING_HANDLE create_sas_token(const std::string &host,
                                          const std::string &entity_name,
                                          const std::string &key,
                                          const std::string &key_name);

    bool create_message_sender();

    bool create_message_receiver();

private:
    /// Connection Materials
    std::string host_{};
    std::string entity_{};
    std::string key_{};
    std::string key_name_{};

    /// State of the Client
    AmqpState client_state_;
    AmqpSenderReceiverState sender_state_;
    AmqpSenderReceiverState receiver_state_;

    /// Message Queue
    std::queue<std::string> message_queue_;
    std::mutex message_queue_lk_;

    std::atomic<bool> stop_{false};
    std::function<void()> received_function_;

    LINK_HANDLE sender_link_ = nullptr;
    LINK_HANDLE receiver_link_ = nullptr;
    SESSION_HANDLE session_ = nullptr;
    CONNECTION_HANDLE connection_ = nullptr;
    XIO_HANDLE ws_io_ = nullptr;
    XIO_HANDLE sasl_io_ = nullptr;
    SASL_MECHANISM_HANDLE sasl_mechanism_handle_ = nullptr;
    CBS_HANDLE cbs_ = nullptr;
    MESSAGE_SENDER_HANDLE message_sender_ = nullptr;
    MESSAGE_RECEIVER_HANDLE message_receiver_ = nullptr;

    /// uAMQP Callbacks
    static AMQP_VALUE on_message_received(const void *context, MESSAGE_HANDLE message);

    static void on_message_receiver_state_changed(const void *context, MESSAGE_RECEIVER_STATE new_state,
                                           MESSAGE_RECEIVER_STATE previous_state);

    static void on_message_send_complete(void *context, MESSAGE_SEND_RESULT send_result, AMQP_VALUE delivery_state);

    static void
    on_message_sender_state_changed(void *context, MESSAGE_SENDER_STATE new_state, MESSAGE_SENDER_STATE prev_state);

    static void on_cbs_open_complete(void *context, CBS_OPEN_COMPLETE_RESULT result);

    static void on_cbs_error(void *context);

    static void
    on_cbs_token_complete(void *context, CBS_OPERATION_RESULT result, unsigned int status_code, const char *desc);

    bool send();
};


#endif //AZURE_UAMQP_CPP_WRAPPER_AZUREAMQPWSWRAPPER_H
