//
// Created by emrah on 12.09.2023.
//

#include "AzureAmqpWsWrapper.h"

AzureAmqpWsWrapper::AzureAmqpWsWrapper() {

}

AMQP_VALUE AzureAmqpWsWrapper::on_message_received(const void *context, MESSAGE_HANDLE message) {
    auto instance = (AzureAmqpWsWrapper *) (context);
    return messaging_delivery_accepted();
}

void AzureAmqpWsWrapper::on_message_send_complete(void *context, MESSAGE_SEND_RESULT send_result,
                                                  AMQP_VALUE delivery_state) {
    auto instance = static_cast<AzureAmqpWsWrapper *>(context);
}

void AzureAmqpWsWrapper::add_message_pending() {

}

void AzureAmqpWsWrapper::do_connection_work() {
    while (!stop_) {
        connection_dowork(connection_);
    }
}

void AzureAmqpWsWrapper::sent_message() {
    if (messages_pending_ > 0)
        --messages_pending_;
}

void AzureAmqpWsWrapper::connect() {
    /* create SASL MSSBCBS handler */
    sasl_mechanism_handle_ = saslmechanism_create(saslmssbcbs_get_interface(), NULL);

    std::string scope(host_+"%2fdevices%2f"+device_id_);
    //SasToken sastoken(deviceKey_, scope , saslExpirationTime);

    /* create the TLS IO */
    WSIO_CONFIG ws_io_config = { host_.c_str(), 443, "AMQPWSB10",  "/$iothub/websocket", true, "" };
    const IO_INTERFACE_DESCRIPTION* tlsio_interface = wsio_get_interface_description();
    ws_io_ = xio_create(tlsio_interface, &ws_io_config);

    /* create the SASL IO using the WS IO */
    SASLCLIENTIO_CONFIG sasl_io_config = { ws_io_, sasl_mechanism_handle_ };
    sasl_io_ = xio_create(saslclientio_get_interface_description(), &sasl_io_config);

    /* create the connection, session and link */
    connection_ = connection_create(sasl_io_, host_.c_str(), "some", nullptr, nullptr);
    session_ = session_create(connection_, nullptr, nullptr);
    session_set_incoming_window(session_, 2147483647);
    session_set_outgoing_window(session_, 65536);

    //printf("Creating cbs\n");
    cbs_ = cbs_create(session_);
    if (cbs_open_async(cbs_) == 0)
    {
        //printf("cbs open success\n");
        sastoken.produce().c_str();
        cbs_put_token(cbs_, "servicebus.windows.net:sastoken", (host_+"/devices/"+device_id_).c_str(), sastoken.produce().c_str() , on_cbs_operation_complete, this);
        while (!auth_)
        {
            connection_dowork(connection_);
        }
        //printf("Have auth_\n");
    }


    /* set incoming window to 100 for the session */
    session_set_incoming_window(session_, 100);
    AMQP_VALUE rcvsource = messaging_create_source( ("amqps://"+host_ +"/devices/"+ device_id_ +"/messages/devicebound").c_str());
    AMQP_VALUE rcvtarget = messaging_create_target("ingress-rx");
    receiver_link_ = link_create(session_, "receiver-link", role_receiver, rcvsource, rcvtarget);
    if(!receiver_link_)
        throw std::runtime_error("Error in creating receiver link");
    link_set_rcv_settle_mode(receiver_link_, receiver_settle_mode_first);
    (void)link_set_max_message_size(receiver_link_, 65536);

    AMQP_VALUE source = messaging_create_source("ingress");
    AMQP_VALUE target = messaging_create_target(("amqps://"+host_ +"/devices/"+ device_id_+ "/messages/events").c_str());
    sender_link_ = link_create(session_, "sender-link", role_sender, source, target);
    if(!sender_link_)
        throw std::runtime_error("Error in creating sender link");

    amqpvalue_destroy(source);
    amqpvalue_destroy(target);
    amqpvalue_destroy(rcvsource);
    amqpvalue_destroy(rcvtarget);

    /* create message sender */
    message_sender_ = messagesender_create(sender_link_, nullptr, nullptr);
    /* create message receiver */
    message_receiver_ = messagereceiver_create(receiver_link_, nullptr, nullptr);

    if ((message_receiver_ == nullptr) ||
        (messagereceiver_open(message_receiver_, on_message_received, &received_function_) != 0))
    {
        throw std::runtime_error("Couldn't open message receiver");
    }

    workerThread_ = std::make_unique<std::thread>(&AzureAmqpWsWrapper::do_connection_work, this);
}

void AzureAmqpWsWrapper::close() {
    stop_ = true;
    messagesender_destroy(message_sender_);
    messagereceiver_destroy(message_receiver_);
    cbs_destroy(cbs_);
    link_destroy(sender_link_);
    session_destroy(session_);
    workerThread_->join();
    connection_destroy(connection_);
    xio_destroy(sasl_io_);
    xio_destroy(ws_io_);
    saslmechanism_destroy(sasl_mechanism_handle_);
    platform_deinit();
}

void AzureAmqpWsWrapper::send(const std::string &message) {
    MESSAGE_HANDLE message_handle = message_create();

    const unsigned char *message_content{reinterpret_cast <unsigned char const *>(message.c_str())};

    BINARY_DATA binary_data = {message_content, message.size()};
    message_add_body_amqp_data(message_handle, binary_data);

    if (!message_sender_) throw std::runtime_error("MessageSender is not defined");

    if (messagesender_open(message_sender_) == 0) {
        messagesender_send_async(message_sender_, message_handle, on_message_send_complete, this, 0);
        add_message_pending();
        message_destroy(message_handle);
    } else {
        throw std::runtime_error("Couldn't open message sender");
    }
}


