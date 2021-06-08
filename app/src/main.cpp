#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>
#include <csignal>
#include <functional>
#include <regex>
#include <fstream>
#include <chrono>
#include <unistd.h>
#include "mqtt/async_client.h"
#include "mqtt/topic.h"
#include "base64.h"
#include "json.hpp"
using json = nlohmann::json;

// #include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc/imgproc.hpp>
// #include <opencv2/videoio.hpp>
// #include <opencv2/highgui.hpp>

extern "C" {
#include "stb_image.h"
}
#undef GPU // avoid conflict with sl::MEM::GPU

std::string getOrDefault(std::string name, std::string value)
{
    if(const char* env_p = std::getenv(name.c_str()))
        return env_p;
    else
        return value;
}
// The client name on the broker
const std::string CLIENT_ID("camera_cppcv");
// The broker/server address
const std::string SERVER_ADDRESS("tcp://"+getOrDefault("MQTT_SERVICE_HOST", "mqtt.kube-system")+":"+getOrDefault("MQTT_SERVICE_PORT", "1883"));
// The topic name for output 0
const std::string MQTT_OUT_0(getOrDefault("MQTT_OUT_0", "camera/images"));
// The QoS to use for publishing and subscribing
const int QOS = 1;
// Interval between images
const std::chrono::duration<int, std::milli> IMAGE_INTERVAL((int)(std::stod(getOrDefault("IMAGE_INTERVAL", "1.0"))*1000));
// Image capture width
const int IMAGE_WIDTH(std::stoi(getOrDefault("IMAGE_WIDTH", "800")));
// Image capture height
const int IMAGE_HEIGHT(std::stoi(getOrDefault("IMAGE_HEIGHT", "600")));
// Device
const int DEVICE_ID(std::stoi(getOrDefault("DEVICE_ID", "0")));


/////////////////////////////////////////////////////////////////////////////
// for convenience
using json = nlohmann::json;

const int MAX_BUFFERED_MSGS = 1024;	// Amount of off-line buffering
const std::string PERSIST_DIR { "data-persist" };

std::function<void(int)> shutdown_handler;
void signalHandler( int signum ) {
    std::cout << "Interrupt signal (" << signum << ") received.\n" << std::flush;
    shutdown_handler(signum);
}

class action_listener : public virtual mqtt::iaction_listener
{
	std::string name_;

	void on_failure(const mqtt::token& tok) override {
		std::cout << name_ << " failure";
		if (tok.get_message_id() != 0)
			std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
		std::cout << std::endl;
	}

	void on_success(const mqtt::token& tok) override {
		std::cout << name_ << " success";
		if (tok.get_message_id() != 0)
			std::cout << " for token: [" << tok.get_message_id() << "]" << std::endl;
		auto top = tok.get_topics();
		if (top && !top->empty())
			std::cout << "\ttoken topic: '" << (*top)[0] << "', ..." << std::endl;
		std::cout << std::endl;
	}

public:
	action_listener(const std::string& name) : name_(name) {}
};

class callback : public virtual mqtt::callback,
					public virtual mqtt::iaction_listener

{
	// Counter for the number of connection retries
	int nretry_;
	// The MQTT client
	mqtt::async_client& cli_;
	// Options to use if we need to reconnect
	mqtt::connect_options& connOpts_;
	// An action listener to display the result of actions.
	action_listener subListener_;

	// This deomonstrates manually reconnecting to the broker by calling
	// connect() again. This is a possibility for an application that keeps
	// a copy of it's original connect_options, or if the app wants to
	// reconnect with different options.
	// Another way this can be done manually, if using the same options, is
	// to just call the async_client::reconnect() method.
	void reconnect() {
		std::this_thread::sleep_for(std::chrono::milliseconds(2500));
		try {
			cli_.connect(connOpts_, nullptr, *this);
		}
		catch (const mqtt::exception& exc) {
			std::cerr << "Error: " << exc.what() << std::endl;
			exit(1);
		}
	}

	// Re-connection failure
	void on_failure(const mqtt::token& tok) override {
		std::cout << "Connection attempt failed" << std::endl;
		if (++nretry_ > 10)
			exit(1);
		reconnect();
	}

	// (Re)connection success
	// Either this or connected() can be used for callbacks.
	void on_success(const mqtt::token& tok) override {}

	// (Re)connection success
	void connected(const std::string& cause) override {
		std::cout << "\nConnection success" << std::endl;
	}

	// Callback for when the connection is lost.
	// This will initiate the attempt to manually reconnect.
	void connection_lost(const std::string& cause) override {
		std::cout << "\nConnection lost" << std::endl;
		if (!cause.empty())
			std::cout << "\tcause: " << cause << std::endl;

		std::cout << "Reconnecting..." << std::endl;
		nretry_ = 0;
		reconnect();
	}

	// Callback for when a message arrives.
	void message_arrived(mqtt::const_message_ptr msg) override {
		std::cout << "Message arrived" << std::endl;
		std::cout << "\ttopic: '" << msg->get_topic() << "'" << std::endl;
	}

	void delivery_complete(mqtt::delivery_token_ptr token) override {}

public:
	callback(mqtt::async_client& cli, mqtt::connect_options& connOpts) //, std::vector<std::string>& objNames)
				: nretry_(0), cli_(cli), connOpts_(connOpts), subListener_("Subscription") {}
};

std::string gstreamer_pipeline (int capture_width, int capture_height, int display_width, int display_height, int framerate, int flip_method) {
    return "nvarguscamerasrc ! video/x-raw(memory:NVMM), width=(int)" + std::to_string(capture_width) + ", height=(int)" +
           std::to_string(capture_height) + ", format=(string)NV12, framerate=(fraction)" + std::to_string(framerate) +
           "/1 ! nvvidconv flip-method=" + std::to_string(flip_method) + " ! video/x-raw, width=(int)" + std::to_string(display_width) + ", height=(int)" +
           std::to_string(display_height) + ", format=(string)BGRx ! videoconvert ! video/x-raw, format=(string)BGR ! appsink";
}

int main(int argc, char* argv[])
{
    mqtt::connect_options connOpts;
	connOpts.set_keep_alive_interval(20);
	connOpts.set_clean_start(true);
	//connOpts.set_automatic_reconnect(true);
	connOpts.set_mqtt_version(MQTTVERSION_3_1_1);

	mqtt::async_client cli(SERVER_ADDRESS, CLIENT_ID); //, MAX_BUFFERED_MSGS, PERSIST_DIR);

    // register signal SIGINT and signal handler and graceful disconnect
    signal(SIGINT, signalHandler);
    shutdown_handler = [&](int signum) {
        // Disconnect
        try {
            std::cout << "Disconnecting from the MQTT broker..." << std::flush;
            cli.stop_consuming();
            cli.disconnect()->wait();
            std::cout << "OK" << std::endl;
        }
        catch (const mqtt::exception& exc) {
            std::cerr << exc.what() << std::endl;
            return 1;
        }

        exit(signum);
    };

    callback cb(cli, connOpts); //, objNames);
    cli.set_callback(cb);

	// Start the connection.
	try {
		std::cout << "Connecting to the MQTT broker at '" << SERVER_ADDRESS << "'..." << std::flush;
		cli.connect(connOpts, nullptr, cb);
	}
	catch (const mqtt::exception& exc) {
		std::cerr << "\nERROR: Unable to connect. "
			<< exc.what() << std::endl;
		return 1;
	}

//	int capture_width = 800 ;
//    int capture_height = 600 ;
//    int display_width = 800 ;
//    int display_height = 600 ;
//    int framerate = 30 ;
//    int flip_method = 0 ;

//    std::string pipeline = gstreamer_pipeline(capture_width,
//	capture_height,
//	display_width,
//	display_height,
//	framerate,
//	flip_method);
//    std::cout << "Using pipeline: \n\t" << pipeline << "\n";

//    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
    cv::VideoCapture cap(DEVICE_ID);
    cap.set(cv::CAP_PROP_FRAME_WIDTH, IMAGE_WIDTH);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, IMAGE_HEIGHT);
    if(!cap.isOpened()) {
        std::cerr << "Failed to open camera." << std::endl;
        return (-1);
    }

    cv::Mat img;
    while(true)
    {
    	if (!cap.read(img)) {
            std::cerr << "Capture read error" << std::endl;
            break;
        }
        std::vector<uchar> buf;
        cv::imencode(".jpg", img, buf);
        auto *enc_msg = reinterpret_cast<unsigned char*>(buf.data());
        std::string encoded = "data:image/jpeg;base64," + base64_encode(enc_msg, buf.size());
        mqtt::message_ptr pubmsg = mqtt::make_message(MQTT_OUT_0, encoded);
        pubmsg->set_qos(QOS);
        cli.publish(pubmsg);
        std::cout << "Image published..." << std::endl;

        std::this_thread::sleep_for(IMAGE_INTERVAL);
    }

    cap.release();
    return 0;
}
