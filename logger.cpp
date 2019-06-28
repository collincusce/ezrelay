#include "logger.h"
//source found from: http://coliru.stacked-crooked.com/a/1a03f2095f0ac308
    Log::Log(enum msg_type mt, bool is_enabled)
    {
        enabled = is_enabled;
        if(enabled) {
            switch (mt) {
            case dbg:
                std::cout << "[D] ";
                break;
            case inf:
                std::cout << "[I] ";
                break;
            case wrn:
                std::cout << "[W] ";
                break;
            case err:
                std::cout << "[E] ";
                break;
            default:
                break;
            }
        }
    }

        