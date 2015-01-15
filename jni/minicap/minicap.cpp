#include <getopt.h>

#include <iostream>
#include <sstream>
#include <stdexcept>

#include <boost/timer/timer.hpp>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "util/capster.hpp"

#define DEFAULT_DISPLAY_ID 0
#define DEFAULT_WEBSOCKET_PORT 9002

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

static void usage(const char* pname)
{
  fprintf(stderr,
    "Usage: %s [-h] [-d <display>] [-i] [-s] [-p <port>]\n"
    "  -d <display>:  Display ID. (%d)\n"
    "  -p <port>:     WebSocket server port. (%d)\n"
    "  -i:            Get display information in JSON format.\n"
    "  -s:            Take a screenshot and output it to stdout.\n"
    "  -b <size>:     Benchmark the given size.\n"
    "  -h:            Show help.\n",
    pname, DEFAULT_DISPLAY_ID, DEFAULT_WEBSOCKET_PORT
  );
}

class capster_server {
public:
  capster_server(uint32_t display_id) : m_capster(display_id) {
    m_capster.initial_update();

    // Don't log
    m_server.set_access_channels(websocketpp::log::alevel::none);

    // Initialize Asio Transport
    m_server.init_asio();
    m_server.set_reuse_addr(true);

    // Register handler callbacks
    m_server.set_socket_init_handler(bind(&capster_server::on_socket_init, this, ::_1, ::_2));
    m_server.set_message_handler(bind(&capster_server::on_message, this, ::_1, ::_2));
  }

  void run(uint16_t port) {
    // listen on specified port
    m_server.listen(port);

    // Start the server accept loop
    m_server.start_accept();

    // Start the ASIO io_service run loop
    try {
      m_server.run();
    } catch (const std::exception & e) {
      std::cout << e.what() << std::endl;
    } catch (websocketpp::lib::error_code e) {
      std::cout << e.message() << std::endl;
    } catch (...) {
      std::cout << "other exception" << std::endl;
    }
  }

  void on_socket_init(connection_hdl, boost::asio::ip::tcp::socket &s) {
    boost::asio::ip::tcp::no_delay option(true);
    s.set_option(option);
  }

  void on_message(connection_hdl hdl, server::message_ptr msg) {
    std::istringstream in(msg->get_payload());

    char cmd;
    in >> cmd;

    switch (cmd) {
      case 's': {
        unsigned int width, height;
        in >> width >> height;
        m_capster.set_desired_size(width, height);
        break;
      }
      case 'j': {
        unsigned int width, height;
        in >> width >> height;

        m_capster.set_desired_size(width, height);

        try {
          if (m_capster.update() != 0) {
            m_server.send(hdl, "secure_on", websocketpp::frame::opcode::text);
          }
          else {
            m_capster.convert();
            m_server.send(hdl, m_capster.get_data(), m_capster.get_size(),
              websocketpp::frame::opcode::BINARY);
          }
        }
        catch (websocketpp::exception& e) {
          if (e.code() == websocketpp::error::bad_connection) {
            // The connection doesn't exist anymore.
          }
          else {
            std::cout << e.what() << std::endl;
          }
        }

        break;
      }
    }
  }

private:
  server m_server;
  capster m_capster;
};

int main(int argc, char* argv[]) {
  minicap_start_thread_pool();

  const char* pname = argv[0];
  uint32_t display_id = DEFAULT_DISPLAY_ID;
  uint16_t port = DEFAULT_WEBSOCKET_PORT;
  bool show_info = false;
  bool take_screenshot = false;
  bool run_benchmark = false;
  unsigned int benchmark_w, benchmark_h;

  int opt;
  while ((opt = getopt(argc, argv, "d:p:isb:h")) != -1) {
    switch (opt) {
      case 'd':
        display_id = atoi(optarg);
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 'i':
        show_info = true;
        break;
        break;
      case 's':
        take_screenshot = true;
        break;
      case 'b': {
        char* cursor;
        run_benchmark = true;
        benchmark_w = strtol(optarg, &cursor, 10);
        benchmark_h = strtol(cursor + 1, &cursor, 10);
        break;
      }
      case '?':
        usage(pname);
        return EXIT_FAILURE;
      case 'h':
        usage(pname);
        return EXIT_SUCCESS;
    }
  }

  if (show_info) {
    try {
      minicap::display_info info = capster::get_display_info(display_id);

      int rotation;
      switch (info.orientation) {
      case minicap::ORIENTATION_0:
        rotation = 0;
        break;
      case minicap::ORIENTATION_90:
        rotation = 90;
        break;
      case minicap::ORIENTATION_180:
        rotation = 180;
        break;
      case minicap::ORIENTATION_270:
        rotation = 270;
        break;
      }

      std::cout.precision(2);
      std::cout.setf(std::ios_base::fixed, std::ios_base::floatfield);

      std::cout << "{"                                         << std::endl
                << "    \"id\": "       << display_id   << "," << std::endl
                << "    \"width\": "    << info.width   << "," << std::endl
                << "    \"height\": "   << info.height  << "," << std::endl
                << "    \"xdpi\": "     << info.xdpi    << "," << std::endl
                << "    \"ydpi\": "     << info.ydpi    << "," << std::endl
                << "    \"size\": "     << info.size    << "," << std::endl
                << "    \"density\": "  << info.density << "," << std::endl
                << "    \"fps\": "      << info.fps     << "," << std::endl
                << "    \"secure\": "   << (info.secure ? "true" : "false") << "," << std::endl
                << "    \"rotation\": " << rotation            << std::endl
                << "}"                                         << std::endl;

      return EXIT_SUCCESS;
    }
    catch (std::exception & e) {
      std::cerr << "ERROR: " << e.what() << std::endl;
      return EXIT_FAILURE;
    }
  }

  if (take_screenshot) {
    try {
      capster capster_instance(display_id);

      if (capster_instance.initial_update() != 0) {
        throw std::runtime_error("Unable to access screen");
      }

      capster_instance.convert();

      write(STDOUT_FILENO, capster_instance.get_data(), capster_instance.get_size());

      return EXIT_SUCCESS;
    }
    catch (std::exception & e) {
      std::cerr << "ERROR: " << e.what() << std::endl;
      return EXIT_FAILURE;
    }
  }

  if (run_benchmark) {
    try {
      capster capster_instance(display_id);

      if (capster_instance.initial_update() != 0) {
        throw std::runtime_error("Unable to access screen");
      }

      int times = 100;

      std::cerr << "Running benchmark on size " << benchmark_w << "x"
        << benchmark_h << " " << times << " times" << std::endl;

      capster_instance.set_desired_size(benchmark_w, benchmark_h);

      boost::timer::auto_cpu_timer t;

      while (times--) {

        if (capster_instance.update() != 0) {
          throw std::runtime_error("Unable to access screen");
        }
        else {
          capster_instance.convert();
        }
      }

      return EXIT_SUCCESS;
    }
    catch (std::exception & e) {
      std::cerr << "ERROR: " << e.what() << std::endl;
      return EXIT_FAILURE;
    }
  }

  try {
    capster_server server_instance(display_id);

    // Run the asio loop with the main thread
    server_instance.run(port);

    return EXIT_SUCCESS;
  }
  catch (std::exception & e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
