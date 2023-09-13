//
// Created by emrah on 13.09.2023.
//

#include <iostream>
#include "AzureAmqpWsWrapper.h"


int main() {
    std::cout << "Hello World in Azure AMQP WS Wrapper" << std::endl;

    std::string host;
    std::string entity;
    std::string key;
    std::string key_name;

    std::shared_ptr<AzureAmqpWsWrapper> azure_amqp_ws_wrapper = std::make_shared<AzureAmqpWsWrapper>(host, entity, key,
                                                                                                     key_name);
    std::thread([&azure_amqp_ws_wrapper](){
        for (int i = 0; i < 20; i++) {
            std::string msg = "Hello World Index:" + std::to_string(i);
            azure_amqp_ws_wrapper->add_message_to_queue(msg);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }).detach();
    azure_amqp_ws_wrapper->start_loop();

    return 0;
}