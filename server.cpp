#include <stdio.h>
#include <ctime>
#include <iostream>
#include <string>
#include <algorithm>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <unistd.h>
#include <sys/time.h>
#include "fps.h"
#include "color.h"

using boost::this_thread::sleep;
using boost::posix_time::milliseconds;
using boost::asio::ip::udp;
using std::string;
using std::cerr;
using std::endl;

void printUsage() {
  cerr << "Usage: server [-b <brightness>][-s <sampleScale>][-r][-f <frameSize>] <port>" << endl;
}

class HeartbeatReceiver {
  udp::socket* socket;
  udp::endpoint* endpoint;
  boost::array<char, 1> buf;
  boost::posix_time::ptime last;
  bool alive_;
  bool checked_;
public:
  HeartbeatReceiver(udp::socket* socket, udp::endpoint* endpoint) :
    socket(socket),
    endpoint(endpoint),
    alive_(false),
    checked_(false)
  {}

  void run() {
    using namespace boost::posix_time;
    boost::system::error_code error;
    ptime now;

    while(!this->checked_ || this->alive_) {
      socket->receive_from(boost::asio::buffer(buf), *endpoint, 0, error);

      if(error == boost::system::errc::success) {
        last = microsec_clock::local_time();
      }
    }
  }

  bool alive() {
    ptime now = microsec_clock::local_time();
    if((now - last).total_milliseconds() < 1000) {
      this->alive_ = true;
      this->checked_ = true;
    } else {
      this->alive_ = false;
    }

    return this->alive_;
  }
};


int main(int argc, char** argv)
{
  try {
    int8_t c;
    size_t frameSize = 480;
    size_t frameRate = 200;
    float hue = 0;
    float saturation = 0;
    float lightness = 0;

    while ((c = getopt(argc, argv, "h:s:l:f:r:")) != -1) {
      switch (c) {
      case 'f':
        frameSize = boost::lexical_cast<size_t>(optarg);
        break;
      case 'r':
        frameRate = boost::lexical_cast<size_t>(optarg);
        break;
      case 'h':
        hue = boost::lexical_cast<float>(optarg);
        break;
      case 's':
        saturation = boost::lexical_cast<float>(optarg);
        break;
      case 'l':
        lightness = boost::lexical_cast<float>(optarg);
        break;
      case ':':
        printUsage();
        break;
      case '?':
        printUsage();
        break;
      }
    }

    if(argc - optind < 1)
      printUsage();

    size_t port = boost::lexical_cast<size_t>(argv[optind]);
    cerr << "listen: " << port << " ..." << endl;

    // set cin unbuffered
    std::cin.setf(std::ios_base::unitbuf);

    //prepare the udp socket
    boost::asio::io_service io_service;
    udp::socket socket(io_service, udp::endpoint(udp::v4(), port));

    //set send buffer size to 2 frames
    udp::socket::native_type native_sock = socket.native();
    int buffsize = frameSize * 2;
    setsockopt(native_sock, SOL_SOCKET, SO_SNDBUF, &buffsize, sizeof(buffsize));

    for(;;) {
      udp::endpoint endpoint;
      boost::system::error_code error;

      HeartbeatReceiver hearbeat(&socket, &endpoint);
      boost::thread(&HeartbeatReceiver::run, &hearbeat);

      if (error && error != boost::asio::error::message_size)
        throw boost::system::system_error(error);

      char readBuf[frameSize];

      cerr << "waiting..." << endl;
      while (!hearbeat.alive()) {
        sleep( milliseconds(100) );
      }

      Fps fps;
      size_t fpsPrintLimit = 100;
      float targetDur = (1000.0 / frameRate);
      float rate = 1;
      fps.start();
      cerr << "sending..." << endl;
      boost::thread transformAndSendThread;

      while (hearbeat.alive()) {
        if(transformAndSendThread.joinable())
          transformAndSendThread.join();

        boost::system::error_code ignored_error;
        std::cin.read(readBuf, frameSize);
        size_t cnt = std::cin.gcount();

        transformAndSendThread = boost::thread([&]() {
          for(size_t i = 0; i < cnt; i+=3) {
            RGB rgb(readBuf[i], readBuf[i+1], readBuf[i+2]);
            HSL hsl(rgb);
            hsl.adjustHue(hue);
            hsl.adjustSaturation(saturation);
            hsl.adjustLightness(lightness);
            rgb = RGB(hsl);
            readBuf[i] = rgb.r;
            readBuf[i+1] = rgb.g;
            readBuf[i+2] = rgb.b;
          }

          socket.send_to(boost::asio::buffer(readBuf, cnt),endpoint, 0, ignored_error);
        });

        if(fps.next() >= fpsPrintLimit) {
          float sampledRate = fps.sample();
          rate = (sampledRate + rate) / 2;
          cerr << "\rfps: " << rate;
        }

        sleep( milliseconds(targetDur));
      }

      cerr << endl << "client lost" << endl;
    }
  }
  catch (std::exception& e) {
    cerr << e.what() << endl;
  }

  return 0;
}