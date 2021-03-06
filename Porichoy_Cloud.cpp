#include <iostream>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <time.h>
#include <assert.h>

#include <mqtt/async_client.h>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>


#define RECEIVED_IMAGE_NAME "temp.jpg"

// the shell script to run the face recognition from the program
// 2>&1 redirects the stderr to stdout
#define FACE_RECOGNITION_SHELL "./face_recognition Database temp.jpg 2>&1"

/////////////////////////////////////////////////////////////////////////////////////////////////
//      MQTT configuations
/////////////////////////////////////////////////////////////////////////////////////////////////
const std::string SERVER_ADDRESS("142.93.62.203:1883");
const std::string CLIENT_ID("Porichoy_Cloud");
const std::string RESULT_TOPIC("Result");

const int NB_TOPICS_SUBSCRIBED = 1;
const int QoS = 2;  // Quality of service

/* ----------- topic struct -------------*/
typedef struct topic
{
    std::string topic_name;
    void (*handler) (mqtt::const_message_ptr&, mqtt::async_client&);
}topic_t;

/* --------- Function Prototype -------------*/
void HandleImageMssg(mqtt::const_message_ptr& msg, mqtt::async_client& client_);

/////////////////////////////////////////////////////////////////////////////////////////////////
//      Topics subscribed
/////////////////////////////////////////////////////////////////////////////////////////////////
const topic_t Topics[NB_TOPICS_SUBSCRIBED] = 
{
    {
        .topic_name = std::string("Image"),
        .handler = &HandleImageMssg
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////////
//      Database Class to store MongoDB databse
/////////////////////////////////////////////////////////////////////////////////////////////////
class MongoDB
{
    private:
        //only once instance should be made throughout the whole program
        static mongocxx::instance _instance;
        //client to connect to an uri
        mongocxx::client _client = mongocxx::client(mongocxx::uri{});
        //database
        mongocxx::database _db;
        //collection
        mongocxx::collection _collection;
        //document builder
        bsoncxx::builder::stream::document _document{};
    
    public:
        //default constructor
        MongoDB()
        {
            _db = _client["Porichoy"];
            _collection = _db["Rohan"];
        }

        // constructor
        MongoDB(const char* database, const char* collection)
        {
            _db = _client[database];
            _collection = _db[collection];
        }

        // constructor
        MongoDB(const char* uri, const char* database, const char* collection)
        {
            _client = mongocxx::client(mongocxx::uri(uri));
            _db = _client[database];
            _collection = _db[collection];
        }

        // store a document
        void StoreDocument(const char* requestTime, const char* response)
        {   
            //clear the document
            _document.clear();

            //build the document
            bsoncxx::document::value doc_value = _document
                << "Request Time"  << requestTime
                << "Response"      << response
                << bsoncxx::builder::stream::finalize;
            
            _collection.insert_one(doc_value.view());

            std::cout << "Document inserted into database\n\n";
        }

};

// definition of the static instance 
mongocxx::instance MongoDB::_instance{};

/*-------------- Database Object ---------------*/
MongoDB Database;

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
            
            // Subscribe to all the topics
            for (int i = 0; i < NB_TOPICS_SUBSCRIBED; i++)
            {
                std::cout << "\nSubscribing to topic '" << Topics[i].topic_name << "'\n"
			          << "\tfor client " << CLIENT_ID
			          << " using QoS" << QoS << "\n";

                client_.subscribe(Topics[i].topic_name, QoS, nullptr, listener_);
            }

            std::cout << "\nPress Q<Enter> to quit\n" << std::endl;
        }

        // callback for when a message arrives
        void message_arrived(mqtt::const_message_ptr msg) override
        {
            std::cout << "Message arrived" << std::endl;
		    std::cout << "\ttopic: '" << msg->get_topic() << "'\n";
		    std::cout << "\tsize: " << msg->get_payload().size() << " bytes\n\n";

            // call the appropriate message handler
            for (int i = 0; i < NB_TOPICS_SUBSCRIBED; i++)
            {
                if (Topics[i].topic_name.compare(msg->get_topic()) == 0)
                {
                    (*(Topics[i].handler))(msg, client_);
                    break;
                }
            }
        }

        // publish message delivered
        void delivery_complete(mqtt::delivery_token_ptr tok) override
        {
		    std::cout << "\n\t[Delivery complete for token: "
			          << (tok ? tok->get_message_id() : -1) << "]" << std::endl;
        }
    
    public:
        // Public constructor fot the call back class
        Callback(mqtt::async_client& client) 
                            : client_(client), listener_("Subscription"){}
};

/////////////////////////////////////////////////////////////////////////////////////////////////
//      Main 
/////////////////////////////////////////////////////////////////////////////////////////////////
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

/////////////////////////////////////////////////////////////////////////////////////////////////
//      Handler for Image topic
/////////////////////////////////////////////////////////////////////////////////////////////////
void HandleImageMssg(mqtt::const_message_ptr& msg, mqtt::async_client& client_) 
{   
    time_t rawTime = time(NULL);
    struct tm *timeInfo = localtime(&rawTime);

    /* --- Save the image as jpg ---*/

    FILE* fp = fopen(RECEIVED_IMAGE_NAME, "wb");

    if (!fp)
    {
        std::cerr << "Error creating image file" << std::endl;
        exit(-1);
    }

    fwrite(msg->get_payload().data(), 1, msg->get_payload().size(), fp);

    std::cout << "Image saved as " << RECEIVED_IMAGE_NAME << "\n\n";

    fclose(fp);

    /* --- Run the face recognition from the shell --- */
    
    std::string result;
    char buffer[256];

    // open a process by creating a pipe, forking, and invoking the shell
    FILE* pipe = popen(FACE_RECOGNITION_SHELL, "r");

    if (!pipe)
    {
        std::cerr << "Error creating pipe" << std::endl;
        exit(-1);
    }

    // Read the pipe till the end of file
    while (!feof(pipe))
    {   
        if (fgets(buffer, 256, pipe) != NULL)
            result.append(buffer);
    }

    std::cout << "Face Recognition Results: " << result << "\n";

    // Close the pipe
    pclose(pipe);

    /* --- Parse the results --- */
    // TODO: Error checking for multiple faces.

    const std::string negative("NO MATCH\n");

    // no match found or result is empty
    if(result.find("unknown_person") != std::string::npos   || 
       result.find("no_persons_found") != std::string::npos  ||
       result.empty())
    {   
        client_.publish(RESULT_TOPIC, negative.data(), negative.size());
    }

    // otherwise a match has been found
    else
        client_.publish(RESULT_TOPIC, result.data(), result.size());

    std::cout << "Acknowledgment Delivered\n" << std::endl;

    /* ------------- Store into Database ---------------------*/

    char timeStr[80];

    assert(strftime(timeStr, sizeof(timeStr), "%c", timeInfo));

    // modify the result to be stored in the database
    // remove "temp.jpg,""
    result = result.erase(0, 9);
    // remove the new line character at the end
    result[result.length()-1] = '\0';

    Database.StoreDocument(timeStr, result.c_str());
}
