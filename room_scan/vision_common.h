// No exception or C++ ownership crosses the private module ABI.
#pragma once
#include "vision_api.h"
#include <stdexcept>
#include <string>
#include <exception>
namespace room_vision {
extern thread_local std::string lastError;
inline void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>int checked(F&& call){try{lastError.clear();return call();}
    catch(const std::exception& error){lastError=error.what();return -1;}
    catch(...){lastError="Room vision operation failed";return -1;}}
}
