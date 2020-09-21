#ifndef _CPPYPLOT_
#define _CPPYPLOT_

/* References
  * python struct: https://docs.python.org/3/library/struct.html
  * python asteval: https://newville.github.io/asteval/api.html#asteval-api
  * cpp cppzmq: https://brettviren.github.io/cppzmq-tour/
*/

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <numeric>

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include <thread>
#include <chrono>
#include <utility>
#include <filesystem>

// Eigen
#if __has_include(<Eigen/Core>)
  #include <Eigen/Core>
  #define EIGEN_AVAILABLE
#elif __has_include (<Eigen/Eigen/Core>)
  #include <Eigen/Eigen/Core>
  #define EIGEN_AVAILABLE
#endif

using namespace std::chrono_literals;
using namespace std::string_literals;

#define PYTHON_PATH "C:/Anaconda3/python.exe"
#define HOST_ADDR "tcp://127.0.0.1:5555"

template <std::size_t ... indices>
decltype(auto) build_string(const char * str, 
                            std::index_sequence<indices...>) 
{
  return std::string{str[indices]...};
}

template <std::size_t N>
constexpr decltype(auto) compile_time_str(const char(&str)[N]) 
{
  return build_string(str, std::make_index_sequence<N-1>());
}

#define STRINGIFY(X) (#X)
#define DATA_PAIR(X) std::make_pair(compile_time_str(STRINGIFY(X)), std::ref(X))
#define _p(X) DATA_PAIR(X)

namespace Cppyplot
{
  
/* ZMQ requires custom dealloc function for zero-copy */
void custom_dealloc(void* data, void* hint)
{
  (void)data;
  (void)hint; 
  return; 
}

// utility function for raw string literal parsing
std::string dedent_string(const std::string_view raw_str);

template<typename T>
struct ValType{
  const static std::size_t elem_size = 0u;
  inline const static std::string typestr{"0"};
};

template<>
struct ValType<char>{
  const static std::size_t elem_size = sizeof(char);
  inline const static std::string typestr{"c"};
};

template<>
struct ValType<signed char>{
  const static std::size_t elem_size = sizeof(signed char);
  inline const static std::string typestr{"b"};
};

template<>
struct ValType<unsigned char>{
  const static std::size_t elem_size = sizeof(unsigned char);
  inline const static std::string typestr{"B"};
};

template<>
struct ValType<short>{
  const static std::size_t elem_size = sizeof(short);
  inline const static std::string typestr{"h"};
};

template<>
struct ValType<unsigned short>{
  const static std::size_t elem_size = sizeof(unsigned short);
  inline const static std::string typestr{"H"};
};

template<>
struct ValType<int>{
  const static std::size_t elem_size = sizeof(int);
  inline const static std::string typestr{"i"};
};

template<>
struct ValType<unsigned int>{
  const static std::size_t elem_size = sizeof(unsigned int);
  inline const static std::string typestr{"I"};
};

template<>
struct ValType<long>{
  const static std::size_t elem_size = sizeof(long);
  inline const static std::string typestr{"l"};
};

template<>
struct ValType<unsigned long>{
  const static std::size_t elem_size = sizeof(unsigned long);
  inline const static std::string typestr{"L"};
};

template<>
struct ValType<long long>{
  const static std::size_t elem_size = sizeof(long long);
  inline const static std::string typestr{"q"};
};

template<>
struct ValType<unsigned long long>{
  const static std::size_t elem_size = sizeof(unsigned long long);
  inline const static std::string typestr{"Q"};
};

template<>
struct ValType<float>{
  const static std::size_t elem_size = sizeof(float);
  inline const static std::string typestr{"f"};
};

template<>
struct ValType<double>{
  const static std::size_t elem_size = sizeof(double);
  inline const static std::string typestr{"d"};
};


template<typename T>
inline std::string shape_str(const std::vector<T>& data)
{
  return "(" + std::to_string(data.size()) +",)";
}

template<typename T>
inline std::string size_str(const std::vector<T>& data)
{
   return std::to_string(data.size());
}

template<typename T>
inline void* void_ptr(const std::vector<T>& data) noexcept
{
  return (void*)data.data();
}


#if defined (EIGEN_AVAILABLE)
/*Reference: https://eigen.tuxfamily.org/dox/TopicFunctionTakingEigenTypes.html */
template<typename Derived>
inline std::string shape_str(const Eigen::EigenBase<Derived>& eigen_container)
{
  return "(" + std::to_string(eigen_container.rows()) + "," + std::to_string(eigen_container.cols()) +")";
}

template<typename Derived>
inline std::string size_str(const Eigen::EigenBase<Derived>& eigen_container)
{
  return std::to_string(eigen_container.size());
}

template<typename Derived>
inline void* void_ptr(const Eigen::EigenBase<Derived>& eigen_container)
{
  return (void*)eigen_container.derived().data();
}

#endif 

class cppyplot{
  private:
    zmq::context_t context_;
    zmq::socket_t socket_;
    std::stringstream plot_cmds_;
  public:
    cppyplot(const std::string& python_path=PYTHON_PATH, const std::string& ip_addr=HOST_ADDR)
    {
      context_ = zmq::context_t(1);
      socket_  = zmq::socket_t(context_, ZMQ_PUB);
      socket_.bind(ip_addr);
      std::this_thread::sleep_for(100ms);
      
      std::filesystem::path path(__FILE__);

      std::string server_file_spawn{"start /min "};
      server_file_spawn.append(python_path);
      server_file_spawn += " ";
      server_file_spawn += path.parent_path().string();
      server_file_spawn += "/cppyplot_server.py ";
      server_file_spawn.append(ip_addr);

      std::system(server_file_spawn.c_str());
      std::this_thread::sleep_for(1.5s);
    }

    ~cppyplot()
    {
      zmq::message_t exit_msg("exit", 4);
      socket_.send(exit_msg, zmq::send_flags::none);
    }

    inline void push(const std::string& cmds)
    { plot_cmds_ << cmds << '\n'; }

    inline void operator<<(const std::string& cmds)
    { this->push(cmds); }

    template<unsigned int N>
    void raw(const char (&input_cmds)[N])
    {
      plot_cmds_ << dedent_string(input_cmds);
    }

    template<typename T>
    inline std::string create_header(const std::string& key, const T& cont) noexcept
    {
      using elem_type = typename T::value_type;
      std::string header{"data|"};

      // variable name
      header += key;
      header.append("|");

      // container element type (float or int or ...)
      header += ValType<elem_type>::typestr;
      header.append("|");

      // total number of elements in the container
      header += size_str(cont);
      header.append("|");

      // shape of the container 
      header += shape_str(cont);

      return header;
    }

    template <typename T>
    void send_container(const std::string& key, const T& cont)
    {
      using elem_type = typename T::value_type;

      std::string data_header{create_header(key, cont)};
      zmq::message_t msg(data_header.c_str(), data_header.length());
      socket_.send(msg, zmq::send_flags::none);

      zmq::message_t payload(void_ptr(cont), ValType<elem_type>::elem_size*cont.size(), 
                              custom_dealloc, nullptr);
      socket_.send(payload, zmq::send_flags::none);
    }

    template<typename ... T>
    void data_args(const T&&... args)
    {
      (send_container(args.first, args.second), ...);

      zmq::message_t cmds(plot_cmds_.str());
      socket_.send(cmds, zmq::send_flags::none);

      zmq::message_t final("finalize", 8);
      socket_.send(final, zmq::send_flags::none);

      /* reset */
      plot_cmds_.str("");
    }
};


// utility functions
decltype(auto) non_empty_line_idx(const std::string_view in_str)
{
  int new_line_pos        = -1;
  unsigned int num_spaces = 0u;
  for (unsigned int i = 0u; i < in_str.length(); i++)
  {
    if (in_str[i] == '\n')
    { new_line_pos = i; num_spaces = 0u; }
    else if (    (in_str[i] != ' ' ) 
              && (in_str[i] != '\t'))
    { break; }
    else
    { num_spaces ++; }
  }
  return std::make_tuple(new_line_pos+1, num_spaces);
}


std::string dedent_string(const std::string_view raw_str)
{
  auto [occupied_line_start, num_spaces] = non_empty_line_idx(raw_str);
  if (num_spaces == 0u)
  { return std::string(raw_str); }
  else
  {
    std::string out_str;
    out_str.reserve(raw_str.length());
    
    int line_start = -1;
    bool process_spaces = true;
    int cur_space_count = 0;
    for (unsigned int i = static_cast<unsigned int>(occupied_line_start); i < raw_str.length(); i++)
    {
      if (process_spaces == true)
      {
        if ((raw_str[i] != ' ') && (raw_str[i] != '\t'))
        {
          process_spaces = false;
          line_start     = i;
        }
        else
        { cur_space_count++; }
      }
      else if (raw_str[i] == '\n')
      {
        if (line_start != -1)
        {
          line_start -= (cur_space_count - num_spaces);
          out_str.append(raw_str.substr(line_start, i - line_start + 1u)); 
        }
        process_spaces  = true;
        line_start      = -1;
        cur_space_count = 0;
      }
    }
    return out_str;
  }
}

}

#endif