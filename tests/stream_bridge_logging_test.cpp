#include "bridge/stream_bridge_log.h"
#include <cstdio>
#include <chrono>
#include <stdexcept>
#include <string>
namespace log=stream_bridge::logging;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){
    try {
        const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("sfnp-log-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        const auto defaults=root/"defaults",overridePath=root/"override";
        struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{root};
        std::ofstream(defaults)<<"Important\n";log::configureCold(defaults,overridePath);
        check(log::enabled(log::Level::Error)&&log::enabled(log::Level::Warning)&&log::enabled(log::Level::Important)&&!log::debug(),"main defaults retain important/errors without diagnostics");
        std::ofstream(defaults)<<"Debug\n";log::configureCold(defaults,overridePath);
        check(log::debug(),"dev defaults retain existing diagnostics");
        std::ofstream(overridePath)<<"Error\n";log::configureCold(defaults,overridePath);
        check(log::enabled(log::Level::Error)&&!log::enabled(log::Level::Warning),"user override wins over build channel");
        std::ofstream(overridePath)<<"garbage";log::configureCold(defaults,overridePath);
        check(log::debug(),"invalid override preserves valid package default");
        std::ofstream(defaults)<<std::string(10000,'D');log::configureCold(defaults,overridePath);
        check(!log::debug()&&log::enabled(log::Level::Important),"oversized files fall back quietly without native failure");
        for(const auto* value:{"","off","0","Debug payload","Debug\nextra"}){log::Level parsed{};check(!log::parse(value,parsed),"invalid values never disable errors or authorize other input");}
#if defined(_MSC_VER)
        check(::_putenv_s("SFNP_LOG_LEVEL","Debug")==0,"owned Windows environment override");log::configureCold(defaults,overridePath);
        check(log::debug(),"bounded Windows CRT environment read preserves valid overrides");
        check(::_putenv_s("SFNP_LOG_LEVEL",std::string(512,'D').c_str())==0,"owned oversized Windows environment value");log::configureCold(defaults,overridePath);
        check(!log::debug()&&log::enabled(log::Level::Important),"oversized Windows environment value preserves fallback without allocation or invalid-parameter failure");
        check(::_putenv_s("SFNP_LOG_LEVEL","")==0,"owned Windows test environment restored");
#endif
        std::puts("Native logging defaults, overrides and bounded malformed input PASS");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
