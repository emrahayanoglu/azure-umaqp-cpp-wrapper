//
// Created by emrah on 12.09.2023.
//

#include "AzureAmqpWsWrapper.h"

static const int wsPort = 443;

AzureAmqpWsWrapper::AzureAmqpWsWrapper(const std::string &host,
                                       const std::string &entity,
                                       const std::string &key,
                                       const std::string &key_name) : host_(host),
                                                                      entity_(entity),
                                                                      key_(key),
                                                                      key_name_(key_name),
                                                                      client_state_(AmqpState::AMQP_STATE_NOT_STARTED),
                                                                      sender_state_(
                                                                              AmqpSenderReceiverState::AMQP_STATE_NOT_READY),
                                                                      receiver_state_(
                                                                              AmqpSenderReceiverState::AMQP_STATE_NOT_READY) {

}

bool AzureAmqpWsWrapper::add_message_to_queue(const std::string &msg) {
    std::unique_lock lk(message_queue_lk_);
    message_queue_.push(msg);
    return true;
}

void AzureAmqpWsWrapper::start_loop() {
    if (!connect()) throw std::runtime_error("Could not prepare the connection params of AMQP over WebSocket");
    while (!stop_) {
        if (client_state_ == AmqpState::AMQP_STATE_ERROR ||
            sender_state_ == AmqpSenderReceiverState::AMQP_STATE_ERROR ||
            receiver_state_ == AmqpSenderReceiverState::AMQP_STATE_ERROR)
            close();
        if (client_state_ == AmqpState::AMQP_STATE_STARTED &&
            sender_state_ == AmqpSenderReceiverState::AMQP_STATE_READY)
            send();
        connection_dowork(connection_);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

bool AzureAmqpWsWrapper::connect() {
    if (platform_init() != 0) {
        return false;
    }

    /* create the TLS IO */
    ws_io_ = create_wsio(host_);
    if (!ws_io_) {
        //LOG
        return false;
    }

    /* create SASL MSSBCBS handler */
    sasl_mechanism_handle_ = saslmechanism_create(saslmssbcbs_get_interface(), nullptr);
    if (!sasl_mechanism_handle_) {
        //LOG
        return false;
    }

    /* create the SASL IO using the WS IO */
    SASLCLIENTIO_CONFIG sasl_io_config = {ws_io_, sasl_mechanism_handle_};
    sasl_io_ = xio_create(saslclientio_get_interface_description(), &sasl_io_config);
    if (!sasl_io_) {
        //LOG
        return false;
    }

    /* create the connection, session and link */
    connection_ = connection_create(sasl_io_, host_.c_str(), "amqp-conn", nullptr, nullptr);
    if (!connection_) {
        //LOG
        return false;
    }

    session_ = session_create(connection_, nullptr, nullptr);
    if (!session_) {
        //LOG
        return false;
    }

    session_set_incoming_window(session_, 2147483647);
    session_set_outgoing_window(session_, 65536);

    cbs_ = cbs_create(session_);
    if (!cbs_) {
        //LOG
        return false;
    }
    if (cbs_open_async(cbs_, on_cbs_open_complete, this, on_cbs_error, this) != 0) {
        //LOG
        return false;
    }

    auto sastoken = create_sas_token(host_, entity_, key_, key_name_);

    if (!sastoken) {
        //LOG
        return false;
    }

    std::string uri = "sb://" + host_ + "/" + entity_;
    if (!cbs_put_token_async(cbs_, "servicebus.windows.net:sastoken", uri.c_str(), STRING_c_str(sastoken),
                             on_cbs_token_complete, this)) {
        //LOG
        return false;
    }

    return true;
}

bool AzureAmqpWsWrapper::create_message_sender() {
    std::string uri = "amqps://" + host_ + "/" + entity_;

    auto source = messaging_create_source("ingress");
    auto target = messaging_create_target(uri.c_str());

    sender_link_ = link_create(session_,
                               "sender-link",
                               role_sender,
                               source,
                               target);
    if (!sender_link_) {
        //LOG
        amqpvalue_destroy(source);
        amqpvalue_destroy(target);
        return false;
    }

    link_set_max_message_size(sender_link_, 65536);

    amqpvalue_destroy(source);
    amqpvalue_destroy(target);

    message_sender_ = messagesender_create(sender_link_, on_message_sender_state_changed, this);
    if (!message_sender_) {
        //LOG
        return false;
    }

    if (messagesender_open(message_sender_) != 0) {
        //LOG
        return false;
    }

    return true;
}

bool AzureAmqpWsWrapper::create_message_receiver() {
    auto rcvsource = messaging_create_source(
            ("amqps://" + host_ + "/devices/" + "device-id" + "/messages/devicebound").c_str());
    auto rcvtarget = messaging_create_target("ingress-rx");
    receiver_link_ = link_create(session_, "receiver-link", role_receiver, rcvsource, rcvtarget);
    if (!receiver_link_) {
        //LOG
        amqpvalue_destroy(rcvsource);
        amqpvalue_destroy(rcvtarget);
        return false;
    }
    link_set_rcv_settle_mode(receiver_link_, receiver_settle_mode_first);
    (void) link_set_max_message_size(receiver_link_, 65536);

    amqpvalue_destroy(rcvsource);
    amqpvalue_destroy(rcvtarget);

    message_receiver_ = messagereceiver_create(receiver_link_, on_message_receiver_state_changed, this);
    if (!message_receiver_) {
        //LOG
        return false;
    }

    if (messagereceiver_open(message_receiver_, on_message_received, this) != 0) {
        //LOG
        return false;
    }

    return true;
}

void AzureAmqpWsWrapper::close() {
    stop_ = true;
    messagesender_destroy(message_sender_);
    messagereceiver_destroy(message_receiver_);
    cbs_destroy(cbs_);
    link_destroy(sender_link_);
    session_destroy(session_);
    connection_destroy(connection_);
    xio_destroy(sasl_io_);
    xio_destroy(ws_io_);
    saslmechanism_destroy(sasl_mechanism_handle_);
    platform_deinit();
}

bool AzureAmqpWsWrapper::send() {
    std::string msg;
    {
        std::unique_lock lk(message_queue_lk_);
        if (message_queue_.empty()) return false;
        msg = message_queue_.front();
        message_queue_.pop();
    }


    MESSAGE_HANDLE message_handle = message_create();

    BINARY_DATA binary_data;
    binary_data.bytes = (const unsigned char *) msg.c_str();
    binary_data.length = msg.size();

    if (message_add_body_amqp_data(message_handle, binary_data) != 0) {
        //LOG
        return false;
    }

    if (!message_sender_) {
        //LOG
        return false;
    }

    if (!messagesender_send_async(message_sender_, message_handle, on_message_send_complete, this, 0)) {
        message_destroy(message_handle);
        //LOG
        return false;
    }
    message_destroy(message_handle);
    return true;
}

XIO_HANDLE AzureAmqpWsWrapper::create_wsio(const std::string &host) {
    TLSIO_CONFIG tlsIOConfig;
    tlsIOConfig.hostname = host.c_str();
    tlsIOConfig.port = wsPort;
    tlsIOConfig.underlying_io_interface = nullptr;
    tlsIOConfig.underlying_io_parameters = nullptr;

    WSIO_CONFIG wsIOConfig;
    wsIOConfig.hostname = host.c_str();
    wsIOConfig.port = wsPort;
    wsIOConfig.protocol = "AMQPWSB10";
    wsIOConfig.resource_name = "/$servicebus/websocket";
    wsIOConfig.underlying_io_interface = platform_get_default_tlsio();
    wsIOConfig.underlying_io_parameters = &tlsIOConfig;

    return xio_create(wsio_get_interface_description(), &wsIOConfig);
}

STRING_HANDLE AzureAmqpWsWrapper::create_sas_token(const std::string &host,
                                                   const std::string &entity_name,
                                                   const std::string &key,
                                                   const std::string &key_name) {
    std::string uri = "sb://" + host + "/" + entity_name;

    STRING_HANDLE sas_key_name = STRING_construct(key_name.c_str());
    BUFFER_HANDLE buffer = BUFFER_create((unsigned char *) key.c_str(), key.length());
    STRING_HANDLE sas_key = Azure_Base64_Encode(buffer);
    STRING_HANDLE encoded_uri = URL_EncodeString(uri.c_str());

    STRING_HANDLE sasToken = SASToken_Create(sas_key, encoded_uri, sas_key_name, time(nullptr) + 3600);

    STRING_delete(encoded_uri);
    STRING_delete(sas_key);
    BUFFER_delete(buffer);
    STRING_delete(sas_key_name);

    return sasToken;
}

AMQP_VALUE AzureAmqpWsWrapper::on_message_received(const void *context, MESSAGE_HANDLE message) {
    auto client = (AzureAmqpWsWrapper *) (context);
    return messaging_delivery_accepted();
}

void AzureAmqpWsWrapper::on_message_receiver_state_changed(const void *context, MESSAGE_RECEIVER_STATE new_state,
                                                           MESSAGE_RECEIVER_STATE previous_state) {
    auto client = (AzureAmqpWsWrapper *) (context);
    switch (new_state) {

        case MESSAGE_RECEIVER_STATE_IDLE:
        case MESSAGE_RECEIVER_STATE_OPENING:
        case MESSAGE_RECEIVER_STATE_CLOSING:
            //LOG
            break;
        case MESSAGE_RECEIVER_STATE_OPEN:
            client->receiver_state_ = AmqpSenderReceiverState::AMQP_STATE_READY;
            break;
        case MESSAGE_RECEIVER_STATE_ERROR:
        case MESSAGE_RECEIVER_STATE_INVALID:
            client->receiver_state_ = AmqpSenderReceiverState::AMQP_STATE_ERROR;
            break;
    }
}

void AzureAmqpWsWrapper::on_message_send_complete(void *context, MESSAGE_SEND_RESULT send_result,
                                                  AMQP_VALUE delivery_state) {
    auto client = static_cast<AzureAmqpWsWrapper *>(context);
    switch (send_result) {
        case MESSAGE_SEND_RESULT_INVALID:
        case MESSAGE_SEND_ERROR:
        case MESSAGE_SEND_TIMEOUT:
        case MESSAGE_SEND_CANCELLED:
            //LOG
            break;
        case MESSAGE_SEND_OK:
            // This might be our confirmation to remove message from the queue
            break;
    }
}

void AzureAmqpWsWrapper::on_cbs_open_complete(void *context, CBS_OPEN_COMPLETE_RESULT result) {
    auto client = static_cast<AzureAmqpWsWrapper *>(context);
    switch (result) {
        case CBS_OPEN_OK:
            //LOG
            client->client_state_ = AmqpState::AMQP_STATE_STARTING;
            break;
        case CBS_OPEN_ERROR:
        case CBS_OPEN_CANCELLED:
        case CBS_OPEN_COMPLETE_RESULT_INVALID:
            //LOG
            client->client_state_ = AmqpState::AMQP_STATE_ERROR;
            break;
    }
}

void AzureAmqpWsWrapper::on_cbs_error(void *context) {
    auto client = static_cast<AzureAmqpWsWrapper *>(context);
    //LOG
    client->client_state_ = AmqpState::AMQP_STATE_ERROR;
}

void AzureAmqpWsWrapper::on_cbs_token_complete(void *context, CBS_OPERATION_RESULT result, unsigned int status_code,
                                               const char *desc) {
    auto client = static_cast<AzureAmqpWsWrapper *>(context);
    switch (result) {
        case CBS_OPERATION_RESULT_OK:
            if (client->client_state_ != AmqpState::AMQP_STATE_STARTING) return;
            client->client_state_ = AmqpState::AMQP_STATE_STARTED;
            client->create_message_sender();
            //client->create_message_receiver();
            break;
        case CBS_OPERATION_RESULT_INVALID:
        case CBS_OPERATION_RESULT_CBS_ERROR:
        case CBS_OPERATION_RESULT_INSTANCE_CLOSED:
        case CBS_OPERATION_RESULT_OPERATION_FAILED:
            //LOG
            client->client_state_ = AmqpState::AMQP_STATE_ERROR;
            break;
    }
}

void AzureAmqpWsWrapper::on_message_sender_state_changed(void *context, MESSAGE_SENDER_STATE new_state,
                                                         MESSAGE_SENDER_STATE prev_state) {
    auto client = static_cast<AzureAmqpWsWrapper *>(context);
    switch (new_state) {
        case MESSAGE_SENDER_STATE_INVALID:
        case MESSAGE_SENDER_STATE_ERROR:
            //LOG
            client->sender_state_ = AmqpSenderReceiverState::AMQP_STATE_ERROR;
            break;
        case MESSAGE_SENDER_STATE_IDLE:
        case MESSAGE_SENDER_STATE_OPENING:
        case MESSAGE_SENDER_STATE_CLOSING:
            //LOG
            break;
        case MESSAGE_SENDER_STATE_OPEN:
            client->sender_state_ = AmqpSenderReceiverState::AMQP_STATE_READY;
            break;
    }
}


