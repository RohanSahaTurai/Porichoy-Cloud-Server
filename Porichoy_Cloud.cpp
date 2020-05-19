#include <iostream>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <mqtt/async_client.h>

#define RECEIVED_IMAGE_NAME "temp.jpg"

const std::string SERVER_ADDRESS("142.93.62.203:1883");
const std::string CLIENT_ID("Porichoy_Cloud");
const std::string TOPIC("Image");

const int QoS = 1;  // Quality of service

/////////////////////////////////////////////////////////////////////////////////////////////////
//      Action Listener class for asynchronous actions
/////////////////////////////////////////////////////////////////////////////////////////////////
class Action_Listener : public virtual mqtt::iaction_listener
{
    private:
        std::string name_;

        void on_failure(const mqtt::token& tok) override
        {
            std::cout << name_ << " failure";

            if (tok.get_message_id() != 0)
                std::cout << " for token: [" << tok.get_message_id() << "]\n";
            
            std::cout << std::endl;
        }

        void on_success(const mqtt::token& tok) override
        {
            std::cout << name_ << " success";

            if (tok.get_message_id() != 0)
                std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
            
            auto topic = tok.get_topics();
            
            if (topic && !topic->empty())
                std::cout << "\ttoken topic: '" << (*topic)[0] << "', ..." << std::endl;
            
            std::cout << std::endl;
        }

    public:
        Action_Listener(const std::string& name) : name_(name){}
};

/////////////////////////////////////////////////////////////////////////////////////////////////
//      Callbback class derived from the mqtt callback class
/////////////////////////////////////////////////////////////////////////////////////////////////
class Callback : public virtual mqtt::callback, public virtual mqtt::iaction_listener
{
    private:
        // The MQTT client
        mqtt::async_client& client_;
        // Object for action listener class
        Action_Listener listener_;

        // private function for reconneting 
        void reconnect()
        {
            // don't start the thread immediately
            std::this_thread::sleep_for(std::chrono::milliseconds(2500));

            // use a try block and catch any exceptions
            // connect function throws an exception and doesn't not return any status
            try
            {
                std::cout << "Reconnecting...\n";
                client_.reconnect();
            }
            catch(const mqtt::exception& except)
            {
                std::cerr << "Reconnection Error: " << except.what() << '\n';
                exit(1);
            }

            std::cout<<"Reconneted!\n";
        }

        // Reconnection failure
        void on_failure(const mqtt::token& tok) override 
        {
            std::cout << "Connection attempt failed" << std::endl;
            reconnect();
	    }

        void on_success(const mqtt::token& tok) override {}

        // Callback for when the connection is lost
        void connection_lost(const std::string& cause) override
        {
            std::cout << "\nConnection lost\n";

            // print the cause if not empty
            if (!cause.empty())
                std::cout << "\tcause: "<< cause <<"\n";
            
            reconnect();
        }

        // callback if connection was successfull
        void connected(const std::string& cause) override
        {
           	std::cout << "\nConnection success" << std::endl;
            
            std::cout << "\nSubscribing to topic '" << TOPIC << "'\n"
			          << "\tfor client " << CLIENT_ID
			          << " using QoS" << QoS << "\n"
			          << "\nPress Q<Enter> to quit\n" << std::endl;
            
            client_.subscribe(TOPIC, QoS, nullptr, listener_);
        }


        // callback for when a message arrives
        void message_arrived(mqtt::const_message_ptr msg) override
        {
            size_t size = msg->get_payload().size();

            std::cout << "Message arrived" << std::endl;
		    std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
		    std::cout << "\tsize: " << size << " bytes\n" << std::endl;


            FILE* fp = fopen(RECEIVED_IMAGE_NAME, "wb");

            if (!fp)
            {
                std::cerr << "Error creating image file" << std::endl;
                exit(-1);
            }

            fwrite(msg->get_payload().data(), 1, size, fp);

            std::cout << "Image saved as " << RECEIVED_IMAGE_NAME << "\n";

            fclose(fp);
        }
    
    public:
        // Public constructor fot the call back class
        Callback(mqtt::async_client& client) 
                            : client_(client), listener_("Subscription"){}
};

int main(int argc, char* argv[])
{
    // set up the connection options
    mqtt::connect_options connectOptions; 
    
    connectOptions.set_keep_alive_interval(60);
    connectOptions.set_clean_session(true);

    // set the asynchronus client
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

    // callback functions
    Callback cb(client);
    client.set_callback(cb);

    // Start the connection.
	// When completed, the callback will subscribe to topic.

	try 
    {
		std::cout << "Connecting to the MQTT server..." << std::flush;
		client.connect(connectOptions, NULL, cb);
	}
	catch (const mqtt::exception& except) 
    {
		std::cerr << "\nERROR: Unable to connect to MQTT server: '"
			<< SERVER_ADDRESS << "'" << std::endl;
        std::cerr << "Error: " << except.what() << std::endl;
		exit(-1);
	}

    // Block till user tells to quit
    while(std::tolower(std::cin.get()) != 'q');

    // Disconnect
    try
    {
        std::cout << "\nDisconnecting from MQTT server..." << std::endl;
        client.disconnect();
    }
    catch(const mqtt::exception& e)
    {
        std::cerr << e.what() << std::endl;
        exit(-1);
    }
    
    return 0;
}